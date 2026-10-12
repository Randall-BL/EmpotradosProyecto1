/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *

 */

/* Guarda de inclusion: el contenido se procesa una sola vez por archivo. */
#ifndef SHA256_H
#define SHA256_H

/* Enteros de ancho fijo. */
#include <stdint.h>
/* size_t. */
#include <stddef.h>

// Produce hex string lowecase de 64-character
/* input: datos a resumir. len: su largo en bytes. out: recibe 64 digitos
   hexadecimales en minuscula y el terminador. */
void sha256_hex(const char *input, size_t len, char out[65]);

#endif