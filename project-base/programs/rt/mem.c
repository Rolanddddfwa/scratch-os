/*
 * Funzioni di aiuto per il compilatore.
 *
 * Anche senza libreria C, GCC può generare da solo chiamate a memcpy
 * e memset (per copiare una struct, per azzerare un array, ...). Ogni
 * programma è collegato per conto suo, senza il kernel e senza libc,
 * quindi deve portarsi dietro queste funzioni. Se un programma non le
 * usa, il linker le scarta (--gc-sections) e non occupano spazio.
 */

#include <stddef.h>

void *memcpy( void *dst, const void *src, size_t n )
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    while ( n-- > 0 )
        *d++ = *s++;
    return dst;
}

void *memset( void *dst, int value, size_t n )
{
    unsigned char *d = dst;

    while ( n-- > 0 )
        *d++ = ( unsigned char ) value;
    return dst;
}
