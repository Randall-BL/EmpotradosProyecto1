/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */


//Script para cifrado y descifrado de password

#include "sha256.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

// Rotacion a la derecha
#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

// Constantes de Sha256
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

typedef struct {
    uint32_t state[8];
    uint8_t  buf[64];
    uint64_t bitlen;
    uint32_t buflen;
} Sha256Ctx;


// Funcion para transformar cifrado sha256
static void sha256_transform(Sha256Ctx *ctx, const uint8_t data[64]) {
    uint32_t a,b,c,d,e,f,g,h,t1,t2,m[64];
    int i;

    for (i = 0; i < 16; i++) {
        m[i] = ((uint32_t)data[i*4]     << 24)
             | ((uint32_t)data[i*4 + 1] << 16)
             | ((uint32_t)data[i*4 + 2] <<  8)
             | ((uint32_t)data[i*4 + 3]);
    }
    for (; i < 64; i++) {
        uint32_t s0 = ROTR(m[i-15],7) ^ ROTR(m[i-15],18) ^ (m[i-15] >> 3);
        uint32_t s1 = ROTR(m[i-2], 17)^ ROTR(m[i-2], 19) ^ (m[i-2]  >> 10);
        m[i] = m[i-16] + s0 + m[i-7] + s1;
    }

    a=ctx->state[0]; b=ctx->state[1]; c=ctx->state[2]; d=ctx->state[3];
    e=ctx->state[4]; f=ctx->state[5]; g=ctx->state[6]; h=ctx->state[7];

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

    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

static void sha256_init(Sha256Ctx *ctx) {
    ctx->bitlen = 0; ctx->buflen = 0;
    ctx->state[0]=0x6a09e667; ctx->state[1]=0xbb67ae85;
    ctx->state[2]=0x3c6ef372; ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f; ctx->state[5]=0x9b05688c;
    ctx->state[6]=0x1f83d9ab; ctx->state[7]=0x5be0cd19;
}

static void sha256_update(Sha256Ctx *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ctx->buf[ctx->buflen++] = data[i];
        if (ctx->buflen == 64) {
            sha256_transform(ctx, ctx->buf);
            ctx->bitlen += 512;
            ctx->buflen  = 0;
        }
    }
}

static void sha256_final(Sha256Ctx *ctx, uint8_t hash[32]) {
    uint32_t i = ctx->buflen;
    ctx->buf[i++] = 0x80;
    if (ctx->buflen < 56) {
        while (i < 56) ctx->buf[i++] = 0;
    } else {
        while (i < 64) ctx->buf[i++] = 0;
        sha256_transform(ctx, ctx->buf);
        memset(ctx->buf, 0, 56);
    }
    ctx->bitlen += (uint64_t)ctx->buflen * 8;
    ctx->buf[63] = (uint8_t)(ctx->bitlen);
    ctx->buf[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->buf[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->buf[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->buf[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->buf[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->buf[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->buf[56] = (uint8_t)(ctx->bitlen >> 56);
    sha256_transform(ctx, ctx->buf);
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
    Sha256Ctx ctx;
    uint8_t   hash[32];
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)input, len);
    sha256_final(&ctx, hash);
    for (int i = 0; i < 32; i++)
        snprintf(out + i*2, 3, "%02x", hash[i]);
    out[64] = '\0';
}