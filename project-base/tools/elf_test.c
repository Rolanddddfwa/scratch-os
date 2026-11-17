/*
 * Prova sul PC il controllo degli ELF del kernel (kernel/elf.c).
 *
 * kernel/elf.c non usa nulla del kernel, quindi si compila anche con
 * il gcc del PC. Questo programma legge i file indicati, chiama
 * elf_check() come farà il loader, e stampa il piano di caricamento
 * oppure il motivo del rifiuto. Per i file accettati esegue anche un
 * caricamento finto in un array da 4096 byte, per provare
 * elf_next_segment() ed elf_next_abs32().
 *
 * Uso (dalla cartella del progetto, oppure `make elf-test`):
 *
 *     gcc -std=gnu17 -Wall -Wextra -I. -o build/elf_test tools/elf_test.c kernel/elf.c
 *     build/elf_test build/programs/thumb_ptr.elf
 *     build/elf_test --base 0x20003000 build/programs/thumb_ptr.elf
 *     build/elf_test --bad build/broken/bad_magic.elf
 *
 * Senza --bad lo stato di uscita è 0 se tutti i file sono accettati;
 * con --bad è 0 se tutti i file sono rifiutati.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/elf.h"

#define SLOT_SIZE 4096u

static const char *const result_names[] = {
    [ELF_OK]             = "ELF_OK",
    [ELF_BAD_HEADER]     = "ELF_BAD_HEADER",
    [ELF_BAD_SEGMENT]    = "ELF_BAD_SEGMENT",
    [ELF_TOO_LARGE]      = "ELF_TOO_LARGE",
    [ELF_BAD_ENTRY]      = "ELF_BAD_ENTRY",
    [ELF_BAD_SECTION]    = "ELF_BAD_SECTION",
    [ELF_BAD_RELOC]      = "ELF_BAD_RELOC",
    [ELF_BAD_RELOC_TYPE] = "ELF_BAD_RELOC_TYPE",
};

/* Legge tutto il file in memoria. Ritorna 0 se non ci riesce. */
static uint8_t *read_file( const char *path, uint32_t *size )
{
    FILE *f = fopen( path, "rb" );
    if ( f == NULL )
        return NULL;
    fseek( f, 0, SEEK_END );
    long length = ftell( f );
    fseek( f, 0, SEEK_SET );

    uint8_t *data = malloc( length > 0 ? ( size_t ) length : 1 );
    if ( data != NULL && fread( data, 1, ( size_t ) length, f ) != ( size_t ) length )
    {
        free( data );
        data = NULL;
    }
    fclose( f );
    *size = ( uint32_t ) length;
    return data;
}

/* Fa su un array ciò che il loader farà sullo slot: copia i segmenti,
 * azzera il BSS, somma la base alle parole da correggere. Stampa una
 * riga per segmento e ritorna il numero di parole corrette. */
static unsigned fake_load( const struct elf_plan *plan, uint32_t slot_base )
{
    static uint8_t slot[SLOT_SIZE];
    struct elf_segment seg;
    uint32_t cursor = 0;

    while ( elf_next_segment( plan, &cursor, &seg ) )
    {
        memcpy( slot + seg.addr, seg.data, seg.file_size );
        memset( slot + seg.addr + seg.file_size, 0, seg.mem_size - seg.file_size );
        printf( "    segmento: indirizzo %u, %u byte dal file, %u di BSS\n",
                ( unsigned ) seg.addr, ( unsigned ) seg.file_size,
                ( unsigned ) ( seg.mem_size - seg.file_size ) );
    }

    struct elf_reloc_cursor rc = { 0, 0 };
    uint32_t addr;
    unsigned count = 0;
    while ( elf_next_abs32( plan, &rc, &addr ) )
    {
        uint32_t word;
        memcpy( &word, slot + addr, 4 );    /* il PC è little-endian come il Cortex-M3 */
        word += slot_base;
        memcpy( slot + addr, &word, 4 );
        count++;
    }
    return count;
}

int main( int argc, char **argv )
{
    uint32_t slot_base = 0x20002000u;       /* slot 0 */
    int expect_bad = 0;
    int failures = 0;

    for ( int i = 1; i < argc; i++ )
    {
        if ( strcmp( argv[i], "--bad" ) == 0 )
        {
            expect_bad = 1;
            continue;
        }
        if ( strcmp( argv[i], "--base" ) == 0 && i + 1 < argc )
        {
            slot_base = ( uint32_t ) strtoul( argv[++i], NULL, 0 );
            continue;
        }

        uint32_t size;
        uint8_t *file = read_file( argv[i], &size );
        if ( file == NULL )
        {
            printf( "%s: non riesco a leggerlo\n", argv[i] );
            failures++;
            continue;
        }

        struct elf_plan plan;
        int result = elf_check( file, size, slot_base, &plan );
        if ( result == ELF_OK )
        {
            printf( "%s: ACCETTATO, ingresso 0x%x, fine %u/%u, dati da %u\n",
                    argv[i], ( unsigned ) plan.entry, ( unsigned ) plan.mem_end,
                    ELF_PROGRAM_LIMIT, ( unsigned ) plan.data_start );
            unsigned words = fake_load( &plan, slot_base );
            printf( "    %u parole corrette sommando 0x%x\n", words, ( unsigned ) slot_base );
        }
        else if ( result == ELF_BAD_RELOC_TYPE )
        {
            printf( "%s: RIFIUTATO, %s (tipo %u)\n", argv[i], result_names[result],
                    ( unsigned ) plan.reloc_type );
        }
        else
        {
            printf( "%s: RIFIUTATO, %s\n", argv[i], result_names[result] );
        }

        if ( ( result == ELF_OK ) == expect_bad )
            failures++;                     /* il contrario di ciò che ci si aspettava */
        free( file );
    }

    if ( failures != 0 )
        printf( "elf_test: %d file con esito inatteso\n", failures );
    return failures == 0 ? 0 : 1;
}
