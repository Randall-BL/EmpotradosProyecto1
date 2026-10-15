/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *

 */

/*
 * Implementacion propia de SHA-256, la funcion de "resumen" (hash) con la que
 * se guardan las contrasenas.
 *
 * Un hash convierte cualquier texto en 64 caracteres hexadecimales. No se
 * puede revertir: del hash no se recupera la contrasena. Para validar un login
 * se calcula el hash de lo que escribio el usuario y se compara con el
 * guardado.
 *
 * - ROTR(x, n): rota los bits de x n lugares a la derecha (lo que sale por un
 *   lado entra por el otro).
 * - K[64]: 64 constantes fijadas por el estandar.
 * - Sha256Ctx: el estado del calculo (8 numeros de 32 bits, un bloque de 64
 *   bytes en construccion y el largo procesado).
 *
 * No hace falta entender las matematicas internas: es una traduccion directa
 * del estandar FIPS 180-4.
 */


//Script para cifrado y descifrado de password

/* SHA-256 es una funcion de resumen de un solo sentido: no se descifra. La
   contrasena recibida se resume y se compara con el resumen guardado. */
#include "sha256.h"
/* memset. */
#include <string.h>
/* snprintf, para pasar el resumen a hexadecimal. */
#include <stdio.h>
/* Enteros de 8, 32 y 64 bits. */
#include <stdint.h>

// Rotacion a la derecha
#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

// Constantes de Sha256
/* Las fija el estandar FIPS 180-4: los primeros 32 bits de la parte
   fraccionaria de la raiz cubica de los primeros 64 numeros primos. */
static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

/* Estado del calculo mientras se van entregando datos. */
typedef struct {
    /* Los ocho valores de 32 bits del resumen parcial. */
    uint32_t state[8];
    /* Bloque de 64 bytes en construccion. */
    uint8_t  buf[64];
    /* Bits procesados hasta ahora. */
    uint64_t bitlen;
    /* Bytes ocupados del bloque en construccion. */
    uint32_t buflen;
} Sha256Ctx;


// Funcion para transformar cifrado sha256
static void sha256_transform(Sha256Ctx *ctx, const uint8_t data[64]) {
    /*
     * Procesa un bloque de 64 bytes.
     *
     * 1. Convierte los 64 bytes en 16 numeros de 32 bits.
     * 2. Genera 48 numeros mas a partir de esos, con rotaciones y XOR (^).
     * 3. Copia el estado a 8 variables a-h y hace 64 rondas: en cada una
     *    mezcla las variables con un numero del paso 2 y una constante K[i].
     * 4. Suma el resultado al estado.
     */

    /* Variables de trabajo a..h, dos temporales y el plan de mensaje de 64
       palabras. */
    uint32_t a,b,c,d,e,f,g,h,t1,t2,m[64];
    int i;

    /* Las primeras 16 palabras son el bloque mismo, leido en big-endian. */
    for (i = 0; i < 16; i++) {
        m[i] = ((uint32_t)data[i*4]     << 24)
             | ((uint32_t)data[i*4 + 1] << 16)
             | ((uint32_t)data[i*4 + 2] <<  8)
             | ((uint32_t)data[i*4 + 3]);
    }
    /* Las 48 restantes se derivan de las anteriores, con rotaciones y
       desplazamientos. */
    for (; i < 64; i++) {
        uint32_t s0 = ROTR(m[i-15],7) ^ ROTR(m[i-15],18) ^ (m[i-15] >> 3);
        uint32_t s1 = ROTR(m[i-2], 17)^ ROTR(m[i-2], 19) ^ (m[i-2]  >> 10);
        m[i] = m[i-16] + s0 + m[i-7] + s1;
    }

    /* Parte del resumen parcial. */
    a=ctx->state[0]; b=ctx->state[1]; c=ctx->state[2]; d=ctx->state[3];
    e=ctx->state[4]; f=ctx->state[5]; g=ctx->state[6]; h=ctx->state[7];

    /* 64 rondas: cada una mezcla el estado con una palabra del plan y una
       constante. */
    for (i = 0; i < 64; i++) {
        uint32_t S1  = ROTR(e,6)  ^ ROTR(e,11) ^ ROTR(e,25);
        uint32_t ch  = (e & f) ^ (~e & g);
        uint32_t S0  = ROTR(a,2)  ^ ROTR(a,13) ^ ROTR(a,22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t1 = h + S1 + ch + K[i] + m[i];
        t2 = S0 + maj;
        h=g; g=f; f=e; e=d+t1;
        d=c; c=b; b=a; a=t1+t2;
    }

    /* Suma el resultado de las rondas al resumen parcial. */
    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

/* Estado inicial: contadores en cero y los ocho valores de arranque del
   estandar. */
static void sha256_init(Sha256Ctx *ctx) {
    /*
     * Pone los contadores en 0 y el estado con los 8 valores iniciales del
     * estandar.
     */

    ctx->bitlen = 0; ctx->buflen = 0;
    ctx->state[0]=0x6a09e667; ctx->state[1]=0xbb67ae85;
    ctx->state[2]=0x3c6ef372; ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f; ctx->state[5]=0x9b05688c;
    ctx->state[6]=0x1f83d9ab; ctx->state[7]=0x5be0cd19;
}

/* Agrega datos al calculo, byte por byte. Cada vez que se completa un
   bloque de 64 bytes, lo procesa. */
static void sha256_update(Sha256Ctx *ctx, const uint8_t *data, size_t len) {
    /*
     * Agrega bytes al bloque en construccion. Cada vez que junta 64, lo
     * procesa con sha256_transform y empieza otro.
     */

    for (size_t i = 0; i < len; i++) {
        /* Copia el byte al bloque en construccion. */
        ctx->buf[ctx->buflen++] = data[i];
        /* Bloque completo: se procesa, se suman sus 512 bits y se empieza
           otro. */
        if (ctx->buflen == 64) {
            sha256_transform(ctx, ctx->buf);
            ctx->bitlen += 512;
            ctx->buflen  = 0;
        }
    }
}

/* Cierra el calculo: aplica el relleno del estandar y entrega los 32 bytes
   del resumen. */
static void sha256_final(Sha256Ctx *ctx, uint8_t hash[32]) {
    /*
     * Cierra el calculo con el "relleno" del estandar: agrega un byte 0x80,
     * ceros, y en los ultimos 8 bytes el largo total del mensaje en bits. Si
     * no cabe en el bloque actual, procesa uno extra. Al final convierte los 8
     * numeros del estado en 32 bytes.
     */

    /* Posicion donde terminan los datos en el bloque. */
    uint32_t i = ctx->buflen;
    /* El relleno empieza con un bit en 1: el byte 0x80. */
    ctx->buf[i++] = 0x80;
    /* Si quedan al menos 8 bytes para el largo, se completa con ceros hasta
       el
       byte 56. */
    if (ctx->buflen < 56) {
        while (i < 56) ctx->buf[i++] = 0;
    } else {
        /* Si no caben, se llena este bloque, se procesa y se sigue con otro
           en cero. */
        while (i < 64) ctx->buf[i++] = 0;
        sha256_transform(ctx, ctx->buf);
        memset(ctx->buf, 0, 56);
    }
    /* Largo total del mensaje, en bits. */
    ctx->bitlen += (uint64_t)ctx->buflen * 8;
    /* Ese largo se escribe en los ultimos 8 bytes, en big-endian. */
    ctx->buf[63] = (uint8_t)(ctx->bitlen);
    ctx->buf[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->buf[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->buf[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->buf[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->buf[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->buf[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->buf[56] = (uint8_t)(ctx->bitlen >> 56);
    /* Procesa el ultimo bloque. */
    sha256_transform(ctx, ctx->buf);
    /* Pasa cada palabra de 32 bits del estado a 4 bytes, en big-endian. */
    for (i = 0; i < 4; i++) {
        hash[i]    = (uint8_t)(ctx->state[0] >> (24 - i*8));
        hash[i+4]  = (uint8_t)(ctx->state[1] >> (24 - i*8));
        hash[i+8]  = (uint8_t)(ctx->state[2] >> (24 - i*8));
        hash[i+12] = (uint8_t)(ctx->state[3] >> (24 - i*8));
        hash[i+16] = (uint8_t)(ctx->state[4] >> (24 - i*8));
        hash[i+20] = (uint8_t)(ctx->state[5] >> (24 - i*8));
        hash[i+24] = (uint8_t)(ctx->state[6] >> (24 - i*8));
        hash[i+28] = (uint8_t)(ctx->state[7] >> (24 - i*8));
    }
}

// Funcion publica
void sha256_hex(const char *input, size_t len, char out[65]) {
    /*
     * La unica funcion publica: recibe un texto y devuelve su hash en 64
     * caracteres hex.
     *
     * init -> update con todo el texto -> final -> cada byte se escribe como
     * dos digitos con snprintf(out + i*2, 3, "%02x", hash[i]).
     *
     * Ejemplo: "password1" -> 0b14d501a594442a... (el que esta en la tabla de
     * usuarios de auth.c).
     *
     * Para entender: "%02x" imprime un numero en hexadecimal con al menos 2
     * digitos, rellenando con 0: 11 -> 0b.
     */

    /* Contexto del calculo y resumen binario de 32 bytes. */
    Sha256Ctx ctx;
    uint8_t   hash[32];
    /* Inicia, entrega toda la entrada y cierra. */
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)input, len);
    sha256_final(&ctx, hash);
    /* Cada byte del resumen se escribe como dos digitos hexadecimales. */
    for (int i = 0; i < 32; i++)
        snprintf(out + i*2, 3, "%02x", hash[i]);
    /* Terminador de cadena. */
    out[64] = '\0';
}