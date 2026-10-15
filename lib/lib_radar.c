/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 */

/*
 * El radar: un hilo que mueve el servo de lado a lado y mide en 7 angulos (0,
 * 30, 60, 90, 120, 150, 180). Es el modulo con mas estado compartido.
 *
 * Variables principales:
 *
 * - g_tabla[7]: la ultima lectura de cada angulo. La casilla es angulo / 30 (0
 *   grados -> 0, 90 grados -> 3, 180 grados -> 6). Cada Entrada tiene la
 *   RadarLectura (angulo, distancia, seq, posicion del robot) y la hora en que
 *   se tomo.
 * - g_seq: contador de lecturas. Cada lectura nueva recibe el numero
 *   siguiente; seq = 0 significa "nunca medido".
 * - g_frente: copia congelada de la ultima lectura al frente, para el tiempo
 *   antes de chocar.
 * - g_vivo, g_pausado, g_angulo, g_hilo.
 * - g_lock (candado) y g_cond (variable de condicion).
 *
 * Para entender: Una variable de condicion (pthread_cond_t) sirve para que un
 * hilo duerma hasta que otro le avise. El que espera hace
 * pthread_cond_wait(&cond, &lock); el que avisa hace
 * pthread_cond_broadcast(&cond). Aqui se usa para despertar a quien espera un
 * barrido nuevo y para pausar/reanudar el hilo.
 *
 * Para entender: volatile int g_vivo le dice al compilador "esta variable la
 * cambia otro hilo; leela de memoria cada vez". Sin eso, el compilador podria
 * optimizar el while (g_vivo) y no ver el cambio.
 */

/* Tipos RadarLectura y RadarChoque, constantes RADAR_* y prototipos. */
#include "lib_radar.h"
/* Odometria: la pose y la velocidad del robot en el instante de medir. */
#include "lib_odom.h"
/* HC-SR04: sensor_init() y sensor_leer_distancia(). */
#include "lib_sensors.h"
/* Servo que orienta el sensor. */
#include "lib_servo.h"

/* ETIMEDOUT, el codigo con que vence pthread_cond_timedwait(). */
#include <errno.h>
/* sin, cos, fabs y fmod. */
#include <math.h>
/* gpio_write, para dejar el TRIG en bajo al detener. */
#include <pigpiod_if2.h>
/* Hilo de barrido, mutex y variable de condicion. */
#include <pthread.h>
/* printf y fprintf. */
#include <stdio.h>
/* memset. */
#include <string.h>
/* clock_gettime y struct timespec. */
#include <time.h>
/* usleep. */
#include <unistd.h>

/* Convierte grados a radianes, para sin() y cos(). */
#define RADIANES(gr) ((gr) * M_PI / 180.0)

/* Una entrada de la tabla del barrido: la lectura que ve el cliente y el
   instante en que se tomo, con el que despues se calcula su edad. */
typedef struct {
    RadarLectura    l;
    struct timespec t;
} Entrada;

/* Protege todo el estado del modulo: la tabla, la lectura frontal, el angulo
   y las banderas. */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
/* Avisa de cada lectura nueva y de cada cambio de pausa o de fin. */
static pthread_cond_t  g_cond;
/* 1 cuando g_cond ya fue creada: se crea una sola vez, en radar_iniciar(). */
static int             g_cond_lista = 0;

/* El unico HC-SR04, montado sobre el servo. */
static SensorUltrasonico g_sensor;
/* Sesion con pigpiod. */
static int               g_pi = -1;
/* Hilo que barre el servo y dispara el sensor. */
static pthread_t         g_hilo;
/* 1 mientras el hilo debe seguir corriendo. volatile: lo escribe un hilo y
   lo lee otro sin tomar el candado. */
static volatile int      g_vivo = 0;
/* 1 si el barrido esta en pausa. */
static int               g_pausado = 0;
/* Angulo al que apunta el servo en este momento. */
static int               g_angulo  = RADAR_FRENTE;

/* Ultima lectura de cada angulo: el indice 0 es 0 grados y el 6, 180. */
static Entrada  g_tabla[RADAR_N_ANGULOS];
/* Contador de lecturas: cada una recibe el numero siguiente. */
static uint32_t g_seq = 0;

/* La ultima lectura frontal, congelada para el tiempo de choque. */
static struct {
    int             medida;         /* ya hubo al menos una lectura frontal */
    int             hay;
    double          distancia_cm;
    double          velocidad_cm_s;
    double          avance_cm;      /* odom_avance_cm() al medir */
    double          rumbo_grados;
    struct timespec t;
} g_frente;

/* Segundos transcurridos desde el instante t, con el reloj monotono. */
static double segundos_desde(const struct timespec *t) {
    /*
     * Cuantos segundos pasaron desde un instante t.
     *
     * Para entender: struct timespec guarda un instante en dos partes:
     * segundos (tv_sec) y nanosegundos (tv_nsec).
     * clock_gettime(CLOCK_MONOTONIC, &t) da la hora de un reloj que nunca
     * retrocede (no le afecta que se cambie la hora del sistema).
     *
     * La resta es (segundos de diferencia) + (nanosegundos de diferencia) /
     * 1e9.
     */

    struct timespec ahora;
    /* Instante actual. */
    clock_gettime(CLOCK_MONOTONIC, &ahora);
    /* Diferencia de segundos enteros mas la de nanosegundos. */
    return (ahora.tv_sec - t->tv_sec) + (ahora.tv_nsec - t->tv_nsec) / 1e9;
}

/* Diferencia entre dos rumbos, en grados, sin el salto de 360 a 0. */
static double diferencia_rumbo(double a, double b) {
    /*
     * La diferencia entre dos rumbos de brujula, entre 0 y 180, sin el
     * problema del salto 359 grados -> 0 grados.
     *
     *     |fmod(a - b + 540, 360) - 180|
     *
     * Ejemplo: a = 350 grados, b = 10 grados. La resta directa da 340 grados,
     * pero en realidad estan a 20 grados. La formula: 350 - 10 + 540 = 880;
     * 880 mod 360 = 160; |160 - 180| = 20. El +540 asegura que el numero no
     * sea negativo antes del modulo.
     */

    /* Se suma 540 antes del modulo para que el resultado quede entre 0 y 180
       aunque la resta sea negativa. */
    return fabs(fmod(a - b + 540.0, 360.0) - 180.0);
}

/* Duerme ms milisegundos; con cero o un valor negativo no espera. */
static void dormir_ms(int ms) {
    /*
     * Duerme ms milisegundos (usleep recibe microsegundos, por eso x 1000).
     * Con 0 o negativo no espera.
     */

    if (ms > 0) usleep((useconds_t)ms * 1000);
}

/* Dispara el sensor y guarda la lectura en la tabla, junto con la pose del
   robot en ese instante. angulo: posicion actual del servo. */
static void medir(int angulo) {
    /*
     * Dispara el sensor y guarda la lectura en la tabla, con la posicion del
     * robot en ese instante.
     *
     * 1. sensor_leer_distancia() - fuera del candado, porque puede tardar
     *    hasta 100 ms y no conviene bloquear a nadie mientras.
     * 2. Pide a la odometria la posicion (odom_get), el avance acumulado y la
     *    velocidad.
     * 3. Toma la hora.
     * 4. Con el candado tomado, llena la casilla g_tabla[angulo / 30]: angulo,
     *    distancia, seq = ++g_seq, posicion y hora.
     * 5. Si el angulo es 90 (frente), tambien llena g_frente con la distancia,
     *    la velocidad, el avance y el rumbo. hay = d > 0 indica si hubo eco.
     * 6. pthread_cond_broadcast despierta a quien este esperando un barrido.
     *
     * Para entender: ++g_seq incrementa primero y despues usa el valor. Si
     * g_seq era 41, la lectura recibe 42.
     */

    /* Medicion. Va fuera del candado: puede tardar decenas de milisegundos. */
    double d = sensor_leer_distancia(&g_sensor);

    /* Pose, avance acumulado y velocidad del robot al medir. */
    double x, y, rumbo;
    odom_get(&x, &y, &rumbo);
    double avance = odom_avance_cm();
    double v      = odom_velocidad_cm_s();

    /* Instante de la medicion, para calcular despues su edad. */
    struct timespec ahora;
    clock_gettime(CLOCK_MONOTONIC, &ahora);

    /* Desde aqui se escribe estado compartido. */
    pthread_mutex_lock(&g_lock);
    /* Casilla de la tabla que corresponde a este angulo. */
    Entrada *e = &g_tabla[angulo / RADAR_PASO_GRADOS];
    /* Llena la lectura: angulo, distancia, numero de secuencia y pose. */
    e->l.angulo       = angulo;
    e->l.distancia_cm = d;
    e->l.seq          = ++g_seq;
    e->l.x_cm         = x;
    e->l.y_cm         = y;
    e->l.rumbo_grados = rumbo;
    e->t              = ahora;

    /* Solo la lectura frontal actualiza el tiempo de choque. Sin eco al
       frente no hay obstaculo con el que chocar: se borra el anterior. */
    if (angulo == RADAR_FRENTE) {
        /* Congela la lectura frontal con lo necesario para proyectarla
           despues:
           si hubo eco, la distancia, la velocidad, el avance acumulado, el
           rumbo y
           el instante. */
        g_frente.medida         = 1;
        g_frente.hay            = d > 0.0;
        g_frente.distancia_cm   = d;
        g_frente.velocidad_cm_s = v;
        g_frente.avance_cm      = avance;
        g_frente.rumbo_grados   = rumbo;
        g_frente.t              = ahora;
    }

    /* Despierta a quien espera una lectura nueva (radar_esperar_barrido). */
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_lock);
}

/* Cuerpo del hilo de barrido: mueve el servo, espera, mide y pasa al angulo
   siguiente, hasta que radar_detener() baja g_vivo. */
static void *hilo_radar(void *arg) {
    /*
     * El cuerpo del hilo del radar. Corre mientras g_vivo sea 1.
     *
     * En cada vuelta:
     *
     * 1. Pausa: si g_pausado, duerme en pthread_cond_wait hasta que lo
     *    despierten. Se usa while y no if porque un hilo puede despertarse "de
     *    mas"; el while vuelve a revisar.
     * 2. espera = servo_mover(angulo) - mueve el servo (la rampa bloquea un
     *    rato) y publica el angulo en g_angulo.
     * 3. dormir_ms(espera) - espera a que el servo se asiente (100 ms si
     *    servo_mover fallo).
     * 4. medir(angulo).
     * 5. Calcula el siguiente angulo. Si el proximo paso se sale de 0-180,
     *    invierte el sentido (paso = -paso).
     *
     * Recorrido: 90 -> 120 -> 150 -> 180 -> 150 -> ... -> 0 -> 30 -> ... como
     * un limpiaparabrisas. Cada posicion tarda unos 0,2 s.
     *
     * Para entender: Una funcion de hilo en C siempre tiene la forma void
     * *f(void *arg). (void)arg; le dice al compilador "se que no uso este
     * parametro" para que no avise.
     */

    /* El argumento del hilo no se usa. */
    (void)arg;
    /* El barrido empieza mirando al frente. */
    int angulo = RADAR_FRENTE;
    /* Avance entre posiciones; cambia de signo en los extremos. */
    int paso   = RADAR_PASO_GRADOS;

    while (g_vivo) {
        /* Mientras el barrido este en pausa, el hilo duerme en la condicion. */
        pthread_mutex_lock(&g_lock);
        while (g_pausado && g_vivo) pthread_cond_wait(&g_cond, &g_lock);
        pthread_mutex_unlock(&g_lock);
        /* Si lo despertaron para terminar, sale del ciclo. */
        if (!g_vivo) break;

        /* Mover, esperar a que el servo llegue y deje de vibrar, y medir.
           El propio viaje del servo separa los disparos del HC-SR04 mas de
           los 60 ms que pide para no oir el eco del pulso anterior. */
        /* Ordena el movimiento; devuelve los ms que faltan para que quede
           quieto. */
        int espera = servo_mover(angulo);
        /* Publica el angulo nuevo para radar_angulo_actual(). */
        pthread_mutex_lock(&g_lock);
        g_angulo = angulo;
        pthread_mutex_unlock(&g_lock);
        /* Espera lo que indico el servo; si fallo, 100 ms por defecto. */
        dormir_ms(espera >= 0 ? espera : 100);

        /* Con el sensor ya quieto, mide. */
        medir(angulo);

        /* Barrido de vaiven: al llegar a un extremo se invierte el sentido. */
        if (angulo + paso > 180 || angulo + paso < 0) paso = -paso;
        /* Siguiente angulo del barrido. */
        angulo += paso;
    }
    return NULL;
}

/* Configura el sensor y el servo, limpia el estado y lanza el hilo.
   Descripcion completa en lib_radar.h. */
int radar_iniciar(int pi) {
    /*
     * Arranca el radar. Si ya corre (g_vivo), no hace nada.
     *
     * 1. Crea la variable de condicion una sola vez, configurada con el reloj
     *    monotonico (para que las esperas con tiempo limite no se alarguen si
     *    cambia la hora).
     * 2. Guarda la sesion y llama a sensor_init (TRIG 17, ECHO 27) y
     *    servo_init.
     * 3. Con el candado: limpia la tabla (memset a 0), pone cada casilla con
     *    su angulo y distancia -1, limpia g_frente, y reinicia g_seq, pausa y
     *    angulo.
     * 4. g_vivo = 1 antes de crear el hilo, porque el hilo lo revisa apenas
     *    arranca.
     * 5. pthread_create(&g_hilo, NULL, hilo_radar, NULL) lanza el hilo. Si
     *    falla, vuelve g_vivo a 0 y devuelve -1.
     *
     * Para entender: pthread_create(&id, NULL, funcion, argumento) crea un
     * hilo que ejecuta funcion(argumento) en paralelo. Devuelve 0 si salio
     * bien.
     *
     * Para entender: memset(p, 0, n) llena n bytes con ceros. Es la forma
     * rapida de "limpiar" una estructura o arreglo.
     */

    /* Ya esta corriendo: no se lanza un segundo hilo. */
    if (g_vivo) return 0;

    /* La variable de condicion se crea una sola vez. */
    if (!g_cond_lista) {
        /* El reloj monotono evita que un ajuste de hora (NTP) alargue o
           acorte las esperas de radar_esperar_barrido(). */
        /* Atributos de la condicion; solo se usan para elegir el reloj. */
        pthread_condattr_t atr;
        pthread_condattr_init(&atr);
        pthread_condattr_setclock(&atr, CLOCK_MONOTONIC);
        pthread_cond_init(&g_cond, &atr);
        pthread_condattr_destroy(&atr);
        g_cond_lista = 1;
    }

    /* Guarda la sesion y configura los dos dispositivos del radar. */
    g_pi = pi;
    sensor_init(pi, &g_sensor, RADAR_TRIG_GPIO, RADAR_ECHO_GPIO);
    servo_init(pi);

    /* Estado inicial, bajo el candado. */
    pthread_mutex_lock(&g_lock);
    /* Tabla en cero: seq = 0 significa "nunca medido". */
    memset(g_tabla, 0, sizeof(g_tabla));
    /* Cada casilla conoce su angulo y arranca sin eco (-1). */
    for (int i = 0; i < RADAR_N_ANGULOS; i++) {
        g_tabla[i].l.angulo       = i * RADAR_PASO_GRADOS;
        g_tabla[i].l.distancia_cm = -1.0;
    }
    /* Todavia no hay lectura frontal. */
    memset(&g_frente, 0, sizeof(g_frente));
    /* Contador de lecturas, pausa y angulo en sus valores iniciales. */
    g_seq     = 0;
    g_pausado = 0;
    g_angulo  = RADAR_FRENTE;
    pthread_mutex_unlock(&g_lock);

    /* La bandera se sube antes de crear el hilo, que la consulta al entrar. */
    g_vivo = 1;
    /* Lanza el hilo; si falla, se baja la bandera y se informa. */
    if (pthread_create(&g_hilo, NULL, hilo_radar, NULL) != 0) {
        g_vivo = 0;
        fprintf(stderr, "[radar] no se pudo crear el hilo de barrido\n");
        return -1;
    }
    printf("[radar] barriendo de 0 a 180 grados en pasos de %d\n", RADAR_PASO_GRADOS);
    return 0;
}

/* Detiene el hilo y deja el hardware en reposo. */
void radar_detener(void) {
    /*
     * Detiene el radar.
     *
     * 1. Con el candado: g_vivo = 0 y broadcast, por si el hilo esta dormido
     *    en pausa.
     * 2. pthread_join(g_hilo, NULL) - espera a que el hilo termine la medicion
     *    en curso y salga.
     * 3. Suelta el servo y deja TRIG en 0.
     *
     * Para entender: pthread_join bloquea hasta que el hilo indicado termine.
     * Es la forma correcta de apagar un hilo: avisarle con una bandera y
     * esperarlo.
     */

    /* Si no esta corriendo no hay nada que detener. */
    if (!g_vivo) return;

    /* Baja la bandera y despierta al hilo, por si duerme en la pausa. */
    pthread_mutex_lock(&g_lock);
    g_vivo = 0;
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_lock);
    /* Espera a que el hilo termine la medicion en curso y salga. */
    pthread_join(g_hilo, NULL);

    /* El servo queda sin pulsos y el TRIG en bajo. */
    servo_liberar();
    gpio_write(g_pi, RADAR_TRIG_GPIO, 0);
}

/* Pausa (1) o reanuda (0) el barrido. */
void radar_pausar(int pausado) {
    /*
     * Pone g_pausado en 1 o 0 y despierta al hilo para que vea el cambio. Se
     * usa en las pruebas para mover el servo a mano.
     */

    pthread_mutex_lock(&g_lock);
    /* Normaliza a 0 o 1 y despierta al hilo para que vea el cambio. */
    g_pausado = pausado ? 1 : 0;
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_lock);
}

/* Devuelve el angulo al que apunta el servo, leido bajo el candado. */
int radar_angulo_actual(void) {
    /* Devuelve hacia donde apunta el servo, leido con el candado tomado. */

    pthread_mutex_lock(&g_lock);
    int a = g_angulo;
    pthread_mutex_unlock(&g_lock);
    return a;
}

/* Copia en out la ultima lectura de cada angulo. Devuelve cuantas copio. */
int radar_lecturas(RadarLectura *out, int max) {
    /*
     * Copia las lecturas de la tabla al arreglo out del que llama (como maximo
     * max). A cada una le calcula la edad en ese momento: segundos desde que
     * se tomo, o -1 si nunca se midio (seq == 0). Devuelve cuantas copio.
     *
     * Se copia en vez de devolver un puntero a la tabla para que quien llama
     * trabaje con una foto fija, sin que el hilo del radar la cambie mientras
     * la lee.
     */

    /* Sin destino o sin espacio no se copia nada. */
    if (!out || max <= 0) return 0;
    /* No se copian mas lecturas de las que caben ni de las que existen. */
    int n = max < RADAR_N_ANGULOS ? max : RADAR_N_ANGULOS;

    pthread_mutex_lock(&g_lock);
    /* Copia cada lectura y le calcula la edad en este momento. */
    for (int i = 0; i < n; i++) {
        out[i] = g_tabla[i].l;
        /* Con seq = 0 el angulo nunca se midio: la edad es -1. */
        out[i].edad_s = g_tabla[i].l.seq ? segundos_desde(&g_tabla[i].t) : -1.0;
    }
    pthread_mutex_unlock(&g_lock);
    return n;
}

/* Devuelve la ultima distancia de un angulo, en cm. */
double radar_distancia(int angulo) {
    /*
     * Devuelve la ultima distancia de un angulo. Solo acepta los 7 angulos del
     * barrido: si angulo % 30 != 0 (no es multiplo de 30) o esta fuera de
     * 0-180, devuelve -1.
     *
     * Para entender: % es el resto de la division: 120 % 30 = 0, 100 % 30 =
     * 10.
     */

    /* Solo existen los angulos del barrido: de 0 a 180 en pasos de 30. */
    if (angulo < 0 || angulo > 180 || angulo % RADAR_PASO_GRADOS != 0) return -1.0;

    /* Lee la casilla de ese angulo bajo el candado. */
    pthread_mutex_lock(&g_lock);
    double d = g_tabla[angulo / RADAR_PASO_GRADOS].l.distancia_cm;
    pthread_mutex_unlock(&g_lock);
    return d;
}

/* Devuelve el numero de la ultima lectura tomada. */
uint32_t radar_seq(void) {
    /*
     * Devuelve el numero de la ultima lectura. Sirve como "marca": se guarda y
     * despues se pregunta si ya hay lecturas mas nuevas.
     */

    pthread_mutex_lock(&g_lock);
    uint32_t s = g_seq;
    pthread_mutex_unlock(&g_lock);
    return s;
}

/* Con g_lock tomado. */
/* Devuelve 1 si todos los angulos tienen una lectura posterior a desde_seq. */
static int completo(uint32_t desde_seq) {
    /*
     * Interna. Revisa las 7 casillas: si alguna tiene seq menor o igual a
     * desde_seq, el barrido todavia no esta completo y devuelve 0. Se llama
     * con el candado ya tomado.
     */

    /* Basta un angulo sin lectura nueva para que el barrido no este
       completo. */
    for (int i = 0; i < RADAR_N_ANGULOS; i++)
        if (g_tabla[i].l.seq <= desde_seq) return 0;
    return 1;
}

/* Version publica de completo(): toma el candado y consulta. */
int radar_barrido_completo(uint32_t desde_seq) {
    /*
     * Version publica de completo(): toma el candado, pregunta y lo suelta. La
     * usa evadir() en el servidor.
     */

    pthread_mutex_lock(&g_lock);
    int c = completo(desde_seq);
    pthread_mutex_unlock(&g_lock);
    return c;
}

/* Bloquea hasta que todos los angulos se midan de nuevo o venza el plazo. */
int radar_esperar_barrido(int timeout_ms) {
    /*
     * Duerme hasta que los 7 angulos tengan una lectura nueva, o hasta que
     * pase timeout_ms.
     *
     * 1. Calcula la hora limite absoluta: ahora + timeout. Si los nanosegundos
     *    pasan de mil millones, suma 1 al segundo y resta mil millones (el
     *    "acarreo").
     * 2. Toma el candado y guarda desde = g_seq.
     * 3. Mientras no este completo, duerme en pthread_cond_timedwait. Cada
     *    medir() lo despierta con su broadcast; entonces vuelve a revisar.
     * 4. Si la espera vence (ETIMEDOUT), revisa una ultima vez y devuelve 0 o
     *    -1.
     */

    /* Sin variable de condicion el radar nunca se inicio. */
    if (!g_cond_lista) return -1;

    /* Instante limite absoluto: ahora mas timeout_ms, en el reloj monotono. */
    struct timespec limite;
    clock_gettime(CLOCK_MONOTONIC, &limite);
    limite.tv_sec  += timeout_ms / 1000;
    limite.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    /* Si los nanosegundos pasan de un segundo, se acarrea. */
    if (limite.tv_nsec >= 1000000000L) { limite.tv_sec++; limite.tv_nsec -= 1000000000L; }

    pthread_mutex_lock(&g_lock);
    /* Marca de partida: se esperan lecturas con numero mayor que este. */
    uint32_t desde = g_seq;
    /* Resultado; queda en 0 mientras no venza el plazo. */
    int r = 0;
    /* Duerme en la condicion hasta que una lectura nueva complete el
       barrido. */
    while (!completo(desde)) {
        /* Vencio el plazo: una ultima comprobacion antes de dar error. */
        if (pthread_cond_timedwait(&g_cond, &g_lock, &limite) == ETIMEDOUT) {
            r = completo(desde) ? 0 : -1;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return r;
}

/* Calcula el estado del tiempo antes de chocar. Ver lib_radar.h. */
void radar_estado_choque(RadarChoque *out) {
    /*
     * Calcula el tiempo antes de chocar con lo que hay al frente y lo deja en
     * la estructura out.
     *
     * 1. Copia g_frente con el candado tomado, y calcula su edad.
     * 2. Llena out con lo medido y pone ttc_s = -1 (sin riesgo) por defecto.
     * 3. Sale sin calcular si: no hubo eco al frente, o la lectura tiene mas
     *    de 2 s.
     * 4. Sale si el robot giro mas de 20 grados desde la lectura: ese
     *    obstaculo ya no esta al frente.
     * 5. Proyecta: d_ahora = d - (avance de ahora - avance al medir).
     * 6. Si d_ahora <= 0 -> ttc = 0 (ya llego). Si la velocidad es mayor que 2
     *    cm/s -> ttc = d_ahora / v. Si no, queda en -1.
     *
     * Ejemplo: midio 50 cm con avance 100 cm; ahora el avance es 104 cm y va a
     * 4 cm/s -> (50 - 4) / 4 = 11,5 s.
     *
     * En el robot real (~2 cm/s) la condicion v > 2 casi nunca se cumple, asi
     * que el tiempo de choque casi siempre queda en -1.
     */

    /* Sin destino no hay donde escribir. */
    if (!out) return;

    /* Copia la lectura frontal congelada, bajo el candado. */
    pthread_mutex_lock(&g_lock);
    int    hay     = g_frente.hay;
    double d       = g_frente.distancia_cm;
    double v_medir = g_frente.velocidad_cm_s;
    double avance0 = g_frente.avance_cm;
    double rumbo0  = g_frente.rumbo_grados;
    /* Edad de esa lectura; -1 si todavia no hubo ninguna. */
    double edad    = g_frente.medida ? segundos_desde(&g_frente.t) : -1.0;
    pthread_mutex_unlock(&g_lock);

    /* Llena la salida. El tiempo de choque parte en -1: sin riesgo. */
    out->hay_obstaculo  = hay;
    out->distancia_cm   = d;
    out->velocidad_cm_s = v_medir;
    out->edad_s         = edad;
    out->ttc_s          = -1.0;

    /* Sin obstaculo, o con una lectura demasiado vieja, no hay estimacion. */
    if (!hay || edad > RADAR_TTC_VIGENCIA_S) return;

    /* Si el robot giro desde la lectura, ese obstaculo ya no esta al frente. */
    double rumbo;
    odom_get(NULL, NULL, &rumbo);
    if (diferencia_rumbo(rumbo, rumbo0) > RADAR_TTC_GIRO_MAX_GRADOS) return;

    /* La distancia se proyecta con lo que el robot avanzo desde la lectura;
       el tiempo, con la velocidad de ahora: si frena, el riesgo desaparece
       sin esperar a la proxima vez que el sensor mire al frente. */
    double d_ahora = d - (odom_avance_cm() - avance0);
    double v       = odom_velocidad_cm_s();

    /* Si ya se llego al obstaculo, el tiempo es cero; si el robot avanza, es
       distancia sobre velocidad; si esta casi quieto, queda en -1. */
    if (d_ahora <= 0.0)             out->ttc_s = 0.0;
    else if (v > RADAR_V_MIN_CM_S)  out->ttc_s = d_ahora / v;
}

/* Atajo: devuelve solo los segundos del estado de choque. */
double radar_tiempo_choque(void) {
    /* Atajo: llama a radar_estado_choque y devuelve solo los segundos. */

    RadarChoque c;
    radar_estado_choque(&c);
    return c.ttc_s;
}

/* Calcula el origen y la direccion del rayo de una lectura, en coordenadas
   del mundo. Cualquiera de los tres punteros de salida puede ser NULL. */
void radar_rayo(const RadarLectura *l, double *origen_x_cm, double *origen_y_cm,
                double *rumbo_rayo_grados)
{
    /*
     * Dice desde donde y hacia donde apuntaba el sensor cuando tomo una
     * lectura, en coordenadas del mundo. Lo usa el mapa.
     *
     * - Origen: el sensor esta 10 cm adelante del centro del robot, en la
     *   direccion del rumbo:
     *
     *     x = x_robot + 10 x sin(rumbo)
     *     y = y_robot + 10 x cos(rumbo)
     *
     * - Direccion: rumbo del robot + (90 - angulo del servo), llevado a 0-360.
     *
     * Ejemplo: robot mirando al norte (0 grados), servo a 0 grados (derecha)
     * -> rayo hacia 0 + 90 = 90 grados (este). Servo a 180 grados -> 0 - 90 =
     * -90 -> 270 grados (oeste).
     *
     * Para entender: Se usa seno para x y coseno para y porque el rumbo es de
     * brujula (0 grados = norte = eje y), al reves que en matematicas, donde 0
     * grados es el eje x.
     */

    /* Sin lectura no hay rayo. */
    if (!l) return;
    /* Rumbo del robot al medir, en radianes. */
    double r = RADIANES(l->rumbo_grados);

    /* El sensor gira sobre el eje del servo, adelante del centro del robot.
       Un angulo de servo de 90 mira al rumbo del robot; 0, noventa grados a
       la derecha (rumbo de brujula mayor); 180, noventa a la izquierda. */
    /* El sensor esta RADAR_EJE_ADELANTE_CM adelante del centro del robot, en
       la direccion del rumbo: x crece con el seno y y con el coseno. */
    if (origen_x_cm) *origen_x_cm = l->x_cm + RADAR_EJE_ADELANTE_CM * sin(r);
    if (origen_y_cm) *origen_y_cm = l->y_cm + RADAR_EJE_ADELANTE_CM * cos(r);
    /* Rumbo del rayo: el del robot mas la desviacion del servo respecto del
       frente, llevado al rango de 0 a 360. */
    if (rumbo_rayo_grados)
        *rumbo_rayo_grados = fmod(l->rumbo_grados + (RADAR_FRENTE - l->angulo) + 360.0, 360.0);
}
