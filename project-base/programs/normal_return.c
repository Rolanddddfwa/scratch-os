/* Finisce ritornando da main(): è _start (rt/crt0.s) a chiamare poi
 * sys_exit al posto del programma. */

#include "lib/printf.h"

int main( void )
{
    printf( "normal_return: returning from main\n" );
    return 0;
}
