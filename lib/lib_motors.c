/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 */

/*
 * Controla los dos motores a traves del puente H L298N. Todo lo que el resto
 * del programa hace con las llantas termina en motores_set().
 *
 * Lo que define arriba:
 *
 * - IN1=5, IN2=6 (motor derecho) e IN3=23, IN4=24 (motor izquierdo): los GPIO.
 * - IZQ_AVANCE, IZQ_RETROCESO, DER_AVANCE, DER_RETROCESO: nombres que dicen
 *   que pin mueve que llanta hacia donde. Si alguien cablea al reves, se
 *   cambia aqui y nada mas.
 * - OPTO_INVERTIDO 1: los optoacopladores PC817 invierten la senal, asi que el
 *   codigo la invierte de vuelta.
 * - PINES[] y N_PINES: la lista de los 4 pines para configurarlos en un ciclo.
 * - g_vel_izq, g_vel_der: la ultima orden aplicada; la lee la odometria.
 *
 * Para entender: #define N_PINES ((int)(sizeof(PINES) / sizeof(PINES[0]))) es
 * un truco clasico de C para saber cuantos elementos tiene un arreglo: tamano
 * total en bytes dividido por el tamano de un elemento.
 *
 * Para entender: #if MOTOR_VELOCIDAD_VARIABLE ... #else ... #endif lo decide
 * el compilador, no el programa al correr. Como MOTOR_VELOCIDAD_VARIABLE vale
 * 0 (en lib_motors.h), solo se compila la parte del #else: velocidad fija, sin
 * PWM. La otra rama queda en el codigo por si algun dia se quiere PWM.
 */

/* Interfaz publica del modulo: constantes MOTOR_* y prototipos motores_*(). */
#include "lib_motors.h"
/* Cliente de pigpiod: set_mode, gpio_write y las funciones de PWM. En el
   simulador este include resuelve contra sim/pigpiod_if2.h. */
#include <pigpiod_if2.h>

/* Entradas del L298N. Los jumpers ENA/ENB del modulo van puestos: el puente
   esta siempre habilitado y todo se controla con las entradas de sentido.
   Para avanzar, IN1 activa con IN2 en bajo; para retroceder, al reves; con
   las dos en bajo el L298N frena el motor en vez de soltarlo.

   Con MOTOR_VELOCIDAD_VARIABLE (lib_motors.h) la entrada activa lleva PWM:
   en la parte baja de cada ciclo las dos quedan iguales y el motor frena, asi
   que la velocidad sigue al ciclo de trabajo. Sin ella, la entrada activa
   queda fija en alto y el motor va al maximo. */

/* Salida A del L298N */
/* IN1 = GPIO 5 (pin fisico 29) e IN2 = GPIO 6 (pin fisico 31), en BCM. */
#define IN1  5
#define IN2  6

/* Salida B del L298N */
/* IN3 = GPIO 23 (pin fisico 16) e IN4 = GPIO 24 (pin fisico 18). */
#define IN3 23
#define IN4 24

/* Que motor quedo en cada salida del L298N en el robot armado: el derecho en
   la A (IN1/IN2) y el izquierdo en la B (IN3/IN4). Con la asignacion al reves
   el robot avanza y retrocede bien, pero cada giro sale hacia el otro lado. */
#define IZQ_AVANCE    IN3
#define IZQ_RETROCESO IN4
#define DER_AVANCE    IN1
#define DER_RETROCESO IN2

/* Frecuencia de la PWM, en Hz. Solo se usa con MOTOR_VELOCIDAD_VARIABLE. */
#define PWM_FREQ 1000

/* Las cuatro senales cruzan al L298N por un PC817 en emisor comun, que las
   invierte: GPIO en alto = entrada del L298N en bajo. Se compensa aqui, el
   unico punto donde la biblioteca toca los motores. Sin compensar, cada motor
   giraria al reves. Ver docs/hardware-aislamiento.md.
   En 0 solo para probar en banco con el L298N conectado sin optoacopladores. */
#define OPTO_INVERTIDO 1

/* Los cuatro pines juntos, para configurarlos con un solo ciclo en
   motores_init(). */
static const unsigned PINES[] = { IN1, IN2, IN3, IN4 };
/* Cantidad de pines de la tabla, calculada por el compilador. */
#define N_PINES ((int)(sizeof(PINES) / sizeof(PINES[0])))

/* Sesion con pigpiod que entrega motores_init(). En -1 el modulo no esta
   inicializado y motores_set() no hace nada. */
static int g_pi = -1;

/* Ultima orden dada a cada motor, con signo. La lee la odometria. */
static int g_vel_izq = 0;
static int g_vel_der = 0;

/* Deja en la ENTRADA DEL L298N un ciclo de trabajo de 0 (siempre en bajo) a
   MOTOR_PWM_MAX (siempre en alto), compensando el optoacoplador. Con
   velocidad fija, cualquier ciclo distinto de cero es "siempre en alto". */
static void entrada_l298n(unsigned gpio, int duty) {
    /*
     * Pone una entrada del L298N en alto o en bajo, compensando el
     * optoacoplador.
     *
     * Con la version que usa el robot (velocidad fija):
     *
     * 1. int nivel = duty > 0; - cualquier valor mayor que cero cuenta como 1.
     * 2. nivel = !nivel; - se invierte, porque el PC817 lo volvera a invertir.
     * 3. gpio_write(g_pi, gpio, nivel); - se escribe en el pin.
     *
     * Quiero que al L298N le llegue   El GPIO se escribe en
     * -----------------------------   ---------------------
     * 1 (activo)                      0
     * 0 (inactivo)                    1
     *
     * Para entender: !x en C es "no x": convierte 0 en 1 y cualquier otro
     * valor en 0.
     */

/* Variante con PWM: el ciclo de trabajo se le pasa a pigpiod. */
#if MOTOR_VELOCIDAD_VARIABLE
  #if OPTO_INVERTIDO
    /* El optoacoplador invierte: para que al L298N le llegue "duty", al GPIO
       hay que pedirle el complemento. */
    duty = MOTOR_PWM_MAX - duty;
  #endif
    /* pigpiod genera la PWM en el pin con ese ciclo de trabajo. */
    set_PWM_dutycycle(g_pi, gpio, (unsigned)duty);
/* Variante de velocidad fija, la que usa el robot: solo hay alto o bajo. */
#else
    /* Cualquier ciclo mayor que cero se convierte en un 1 logico. */
    int nivel = duty > 0;
  #if OPTO_INVERTIDO
    /* La misma compensacion del optoacoplador, ahora sobre el nivel. */
    nivel = !nivel;
  #endif
    /* gpio_write tambien apaga la PWM si una version anterior del servidor
       la dejo corriendo en pigpiod. */
    gpio_write(g_pi, gpio, (unsigned)nivel);
#endif
}

/* Guarda la sesion con pigpiod, configura los cuatro pines y deja el robot
   frenado. pi: sesion que devuelve pigpio_start(). */
void motores_init(int pi) {
    /*
     * Prepara los motores. Recibe la sesion de pigpiod y la guarda en g_pi.
     *
     * Recorre los 4 pines y los pone como salida (set_mode(..., PI_OUTPUT)).
     * En la variante con PWM tambien fijaria la frecuencia (1000 Hz) y el
     * rango (0-255), pero esa parte no se compila. Termina con
     * motores_detener(), para que las llantas queden frenadas desde el primer
     * momento.
     */

    /* Desde aqui el resto del modulo puede hablar con pigpiod. */
    g_pi = pi;

    /* Recorre IN1..IN4. */
    for (int i = 0; i < N_PINES; i++) {
        /* Cada entrada del L298N es una salida de la Raspberry Pi. */
        set_mode(g_pi, PINES[i], PI_OUTPUT);
#if MOTOR_VELOCIDAD_VARIABLE
        /* Con velocidad variable se fijan la frecuencia y el rango de la PWM:
           un ciclo de 0 a MOTOR_PWM_MAX cubre de 0 a 100 %. */
        set_PWM_frequency(g_pi, PINES[i], PWM_FREQ);
        set_PWM_range(g_pi, PINES[i], MOTOR_PWM_MAX);
#endif
    }

    /* Estado seguro al arrancar: las cuatro entradas en bajo, motores frenados. */
    motores_detener();
}

/* Lo que de verdad recibe el motor: la orden saturada, o el maximo con su
   signo si la velocidad es fija. */
static int efectiva(int v) {
    /*
     * Convierte la velocidad pedida en la que de verdad recibe el motor.
     *
     * Con velocidad fija solo importa el signo:
     *
     *     v > 0  ->  255    (adelante a tope)
     *     v < 0  -> -255    (atras a tope)
     *     v == 0 ->    0    (freno)
     *
     * Para entender: a ? b : c es el operador ternario: "si a, entonces b, si
     * no c". Aqui hay dos encadenados: v > 0 ? 255 : v < 0 ? -255 : 0.
     */

#if MOTOR_VELOCIDAD_VARIABLE
    /* Satura en el tope positivo y en el negativo; dentro del rango no cambia. */
    if (v >  MOTOR_PWM_MAX) return  MOTOR_PWM_MAX;
    if (v < -MOTOR_PWM_MAX) return -MOTOR_PWM_MAX;
    return v;
#else
    /* Solo importa el signo: maximo hacia adelante, maximo hacia atras o cero. */
    return v > 0 ? MOTOR_PWM_MAX : v < 0 ? -MOTOR_PWM_MAX : 0;
#endif
}

/* Un motor: el signo elige cual de sus dos entradas lleva la PWM y la otra
   queda en bajo. Con 0 las dos quedan en bajo, que con el puente habilitado
   es freno. */
static void motor(unsigned in_avance, unsigned in_retroceso, int vel) {
    /*
     * Aplica una velocidad con signo a un motor, que tiene dos pines.
     *
     * 1. magnitud = valor absoluto de la velocidad.
     * 2. El pin de avance recibe la magnitud solo si la velocidad es positiva;
     *    si no, 0.
     * 3. El pin de retroceso recibe la magnitud solo si es negativa; si no, 0.
     *
     * Asi, con velocidad 0 los dos pines quedan en 0, que en el L298N
     * significa freno (el motor se detiene en seco, no queda suelto).
     */

    /* Valor absoluto de la velocidad: el sentido ya lo da el signo. */
    int magnitud = vel < 0 ? -vel : vel;
    /* La entrada de avance lleva la magnitud solo si la velocidad es positiva. */
    entrada_l298n(in_avance,    vel > 0 ? magnitud : 0);
    /* La de retroceso, solo si es negativa. Con vel = 0 las dos quedan en 0. */
    entrada_l298n(in_retroceso, vel < 0 ? magnitud : 0);
}

/* Primitiva del control diferencial: recibe la velocidad con signo de cada
   motor, la lleva a lo que el motor puede recibir, la aplica y la recuerda.
   vel_izq, vel_der: de -MOTOR_PWM_MAX a MOTOR_PWM_MAX. */
void motores_set(int vel_izq, int vel_der) {
    /*
     * La funcion principal del modulo: fija la velocidad de cada motor por
     * separado (control diferencial). Todas las demas funciones de movimiento
     * la usan.
     *
     * 1. Si no hay sesion (g_pi < 0), no hace nada.
     * 2. Pasa cada velocidad por efectiva(): 210 se vuelve 255, por ejemplo.
     * 3. Llama a motor() para el izquierdo y el derecho.
     * 4. Guarda lo aplicado en g_vel_izq y g_vel_der.
     *
     * Ejemplo: motores_set(-200, 200) -> izquierdo atras a tope, derecho
     * adelante a tope -> el robot gira sobre su eje hacia la izquierda.
     */

    /* Sin sesion con pigpiod no hay a quien mandarle la orden. */
    if (g_pi < 0) return;

    /* Lo que cada motor recibe de verdad: saturado, o el maximo si la velocidad
       es fija. */
    vel_izq = efectiva(vel_izq);
    vel_der = efectiva(vel_der);

    /* Aplica la orden a los dos pines de cada motor. */
    motor(IZQ_AVANCE, IZQ_RETROCESO, vel_izq);
    motor(DER_AVANCE, DER_RETROCESO, vel_der);

    /* Guarda lo aplicado para motores_get(): la odometria integra estos valores. */
    g_vel_izq = vel_izq;
    g_vel_der = vel_der;
}

/* Devuelve la ultima orden aplicada a cada motor. Cualquiera de los dos
   punteros puede ser NULL si solo interesa el otro. */
void motores_get(int *vel_izq, int *vel_der) {
    /*
     * Devuelve la ultima orden de cada motor a traves de punteros.
     *
     * Para entender: En C una funcion solo puede devolver un valor con return.
     * Para devolver dos, recibe la direccion de dos variables del que llama y
     * escribe en ellas: *vel_izq = g_vel_izq;. Quien llama hace int a, b;
     * motores_get(&a, &b);. El & saca la direccion de la variable; el *
     * escribe en esa direccion.
     *
     * El if (vel_izq) permite pasar NULL cuando solo interesa uno de los dos.
     */

    if (vel_izq) *vel_izq = g_vel_izq;
    if (vel_der) *vel_der = g_vel_der;
}

/* Frena los dos motores: es motores_set() con velocidad cero. */
void motores_detener(void) {
    /* Frena los dos motores: es motores_set(0, 0). */

    motores_set(0, 0);
}

/* Movimientos basicos: cada uno es una combinacion de signos sobre
   motores_set(). Avanzar, los dos hacia adelante; retroceder, los dos hacia
   atras; girar a la izquierda, izquierdo atras y derecho adelante, sobre el
   propio eje; girar a la derecha, al reves. */
/* Los dos motores hacia adelante: motores_set(v, v). */
void motores_avanzar(int velocidad)         { motores_set( velocidad,  velocidad); }
/* Los dos motores hacia atras: motores_set(-v, -v). */
void motores_retroceder(int velocidad)      { motores_set(-velocidad, -velocidad); }
/*
 * Gira sobre su eje a la izquierda: izquierdo atras, derecho adelante,
 * motores_set(-v, v).
 */
void motores_girar_izquierda(int velocidad) { motores_set(-velocidad,  velocidad); }
/*
 * Gira sobre su eje a la derecha: izquierdo adelante, derecho atras,
 * motores_set(v, -v).
 */
void motores_girar_derecha(int velocidad)   { motores_set( velocidad, -velocidad); }

/* Avanza describiendo una curva. velocidad: la del motor exterior.
   giro: de -100 (izquierda) a 100 (derecha); 0 es recto. */
void motores_curva(int velocidad, int giro) {
    /*
     * Pensada para avanzar en curva. Primero limita giro entre -100 y 100.
     *
     * Con velocidad fija (la que se compila) no puede hacer curvas suaves,
     * porque bajar una llanta la dejaria igual al maximo. Entonces:
     *
     * - giro > 0 -> gira a la derecha sobre su eje.
     * - giro < 0 -> gira a la izquierda sobre su eje.
     * - giro == 0 -> avanza recto.
     *
     * La rama con PWM (no compilada) bajaria la velocidad de la llanta
     * interior en el porcentaje del giro.
     */

    /* Limita el giro al rango valido. */
    if (giro >  100) giro =  100;
    if (giro < -100) giro = -100;

#if MOTOR_VELOCIDAD_VARIABLE
    /* El motor del lado hacia el que se gira baja de velocidad; el otro
       mantiene la del parametro. Con giro = 100 el interior queda detenido. */
    /* Velocidad del motor interior: a "velocidad" se le resta el porcentaje
       que indica el valor absoluto del giro. */
    int interior = velocidad - (velocidad * (giro < 0 ? -giro : giro)) / 100;

    /* Giro positivo es a la derecha, asi que el motor interior es el derecho;
       con giro negativo, el izquierdo. */
    if (giro >= 0) motores_set(velocidad, interior);
    else           motores_set(interior,  velocidad);
#else
    /* Sin velocidad variable no hay curvas de radio variable: bajar la llanta
       interior la dejaria igual al maximo. Se gira sobre el eje, con una
       llanta hacia adelante y la otra hacia atras. */
    /* El signo del giro elige el sentido; con giro = 0 se avanza recto. */
    if (giro > 0)      motores_girar_derecha(velocidad);
    else if (giro < 0) motores_girar_izquierda(velocidad);
    else               motores_avanzar(velocidad);
#endif
}
