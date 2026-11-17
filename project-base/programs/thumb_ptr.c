/* Un puntatore a funzione tenuto in una variabile.
 *
 * Sul Cortex-M l'indirizzo di una funzione ha il bit 0 a 1: significa
 * "codice Thumb". Il loader somma la base dello slot (un multiplo di
 * 4096) a quell'indirizzo, quindi il bit 0 deve restare a 1. Se
 * andasse perso, la chiamata attraverso il puntatore darebbe un fault. */

#include <stdint.h>
#include "lib/printf.h"

static int answer( void )
{
    return 42;
}

/* Valore iniziale = indirizzo di answer: una parola da rilocare. */
static int ( *volatile function_pointer )( void ) = answer;

int main( void )
{
    int ( *f )( void ) = function_pointer;
    unsigned thumb = ( uintptr_t ) f & 1u;      /* il bit Thumb */

    printf( "thumb_function_pointer: thumb=%u result=%d\n", thumb, f() );
    return 0;
}
