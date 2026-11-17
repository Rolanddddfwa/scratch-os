/*
 * Il loader: vedi kernel/loader.h.
 *
 * Mette insieme i pezzi degli altri moduli, in quest'ordine:
 *
 *   1. kernel/image.c   trova il file del programma nell'immagine;
 *   2. kernel/task.c    dice qual è il primo slot libero;
 *   3. kernel/elf.c     controlla il file e prepara il piano;
 *   4. qui              copia il programma nello slot e corregge gli
 *                       indirizzi (rilocazione);
 *   5. kernel/task.c    riceve il confine codice/dati (per il W^X) e
 *                       crea il task.
 *
 * La regola più importante: fino al passo 3 compreso non si scrive
 * nulla. Se un controllo fallisce si ritorna l'errore e ogni slot
 * resta com'era. Dal passo 4 in poi non può più fallire niente.
 *
 * Tutto questo gira dentro la SVC 13, in modo privilegiato. Lo slot
 * scelto è libero, quindi nessuna regione della MPU lo copre e il
 * kernel può scriverci.
 */

#include <stdint.h>
#include "kernel/loader.h"
#include "kernel/image.h"
#include "kernel/elf.h"
#include "kernel/task.h"
#include "kernel/mpu.h"
#include "kernel/kprintf.h"

/* Primo indirizzo del primo slot: lo definisce il linker script
 * (scripts/mps2_m3.ld). Gli slot sono 4, uno dopo l'altro. */
extern uint8_t __program_slots_start[];

#define SLOT_SIZE   4096u       /* byte di uno slot */
#define STACK_SIZE  1024u       /* i byte in cima allo slot, per lo stack */

/* I tre moduli devono essere d'accordo su come è diviso uno slot. Se
 * qualcuno cambia una costante, la compilazione si ferma qui. */
_Static_assert( ELF_PROGRAM_LIMIT + STACK_SIZE == SLOT_SIZE,
                "programma + stack devono riempire lo slot" );
_Static_assert( ELF_PROTECT_STEP == SLOT_SIZE / MPU_SUBREGIONS,
                "il passo del confine deve essere una sottoregione della MPU" );

/* Il numero del primo slot libero (0..3), oppure -1 se sono tutti
 * occupati. */
static int first_free_slot( void )
{
    for ( unsigned slot = 0; slot < PROGRAM_SLOT_COUNT; slot++ )
    {
        if ( task_program_slot_free( slot ) )
            return ( int ) slot;
    }
    return -1;
}

/* Copia nello slot il programma descritto dal piano. Il piano è già
 * stato controllato: ogni segmento sta nel file e nei primi 3072 byte
 * dello slot. */
static void copy_program( const struct elf_plan *plan, uint8_t *slot )
{
    /* Prima azzera tutto lo slot. Così il BSS vale zero, come richiede
     * il C, e il nuovo programma non trova i dati lasciati dal
     * programma che occupava lo slot prima di lui. */
    for ( uint32_t i = 0; i < SLOT_SIZE; i++ )
        slot[i] = 0;

    /* Poi copia i byte di ogni segmento dal file (in FLASH) allo slot.
     * La parte di un segmento oltre file_size è il BSS: resta a zero. */
    struct elf_segment seg;
    uint32_t cursor = 0;
    while ( elf_next_segment( plan, &cursor, &seg ) )
    {
        for ( uint32_t i = 0; i < seg.file_size; i++ )
            slot[seg.addr + i] = seg.data[i];
    }
}

/* Rilocazione: il programma è collegato come se partisse dall'indirizzo
 * 0, ma ora sta a `base`. Ogni parola che contiene un indirizzo
 * completo (R_ARM_ABS32) va corretta sommando la base. elf_check ha già
 * verificato che ogni parola stia nel programma, sia allineata a 4,
 * compaia una volta sola e che la somma non superi i 32 bit. */
static void relocate_program( const struct elf_plan *plan, uint32_t base )
{
    struct elf_reloc_cursor cursor = { 0, 0 };
    uint32_t addr;

    while ( elf_next_abs32( plan, &cursor, &addr ) )
    {
        uint32_t *word = ( uint32_t * ) ( base + addr );
        *word += base;
    }
}

int loader_run( const char *name )
{
    /* 1. Il file nell'immagine. */
    if ( image_count() < 0 )
        return -1;                          /* immagine non valida */

    const uint8_t *file;
    uint32_t size;
    if ( image_find( name, &file, &size ) != 0 )
        return -2;                          /* nessun programma con quel nome */

    /* 2. Il primo slot libero, nell'ordine 0, 1, 2, 3. */
    int slot = first_free_slot();
    if ( slot < 0 )
        return -3;
    uint32_t base = ( uint32_t ) __program_slots_start + ( uint32_t ) slot * SLOT_SIZE;

    /* 3. Il controllo completo del file. La base serve a verificare
     * che le somme della rilocazione non vadano in overflow. Per un
     * tipo di rilocazione non ammesso si stampa prima quale. */
    struct elf_plan plan;
    int result = elf_check( file, size, base, &plan );
    if ( result == ELF_BAD_RELOC_TYPE )
        kprintf( "loader: unsupported relocation type %u\n", plan.reloc_type );
    if ( result != ELF_OK )
        return -4;

    /* Il confine tra codice e dati, per il W^X. È l'ultima cosa che
     * può dire di no, e non ha ancora toccato lo slot. */
    if ( task_program_set_code_size( ( unsigned ) slot, plan.data_start ) != 0 )
        return -4;

    /* 4. Da qui in poi lo slot cambia. */
    copy_program( &plan, ( uint8_t * ) base );
    relocate_program( &plan, base );

    /* 5. Il task. L'ingresso è l'indirizzo Thumb del piano spostato
     * alla base (il bit 0 resta a 1, perché la base è un multiplo di
     * 4096). Lo stack parte dalla fine dello slot. executable = 1:
     * insieme al confine appena impostato dà il W^X. La funzione
     * stampa la riga "loader: ..." e ritorna l'id. */
    int id = task_program_create( ( unsigned ) slot, name,
                                  ( void ( * )( void ) ) ( base + plan.entry ),
                                  ( uint32_t * ) ( base + SLOT_SIZE ), 1 );
    return id < 0 ? -4 : id;
}
