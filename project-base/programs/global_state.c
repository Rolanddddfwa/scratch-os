/* Variabili globali: una con valore iniziale (.data), una a zero
 * (.bss) e una tabella costante (.rodata), raggiunte attraverso
 * puntatori. Ogni puntatore contiene un indirizzo completo, che il
 * loader deve correggere sommando la base dello slot (R_ARM_ABS32).
 * Se la rilocazione o la copia dei dati fossero sbagliate, i numeri
 * stampati sarebbero diversi. */

#include "lib/printf.h"

/* Non sono "static": così il compilatore non può sostituirle con i loro
 * valori e deve davvero leggerle dalla memoria. */
const int table[4] = { 2, 4, 16, 256 };         /* costanti: con il codice */
int global = 37;                                /* dato iniziale: copiato dal file */
int bss;                                        /* a zero: azzerato dal loader */

/* Puntatori con valore iniziale: tre parole da rilocare nei dati. */
int *global_ptr = &global;
int *bss_ptr = &bss;
const int *table_ptr = &table[2];

int main( void )
{
    int g = *global_ptr;
    int b = *bss_ptr;
    int t = *table_ptr;

    printf( "globals_bss_const: global=%d bss=%d table=%d sum=%d\n",
            g, b, t, g + b + t );
    return 0;
}
