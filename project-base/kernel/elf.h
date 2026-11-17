/*
 * Controllo di un programma ELF prima di caricarlo (assignment 6.3).
 *
 * elf_check() legge il file dove si trova, nell'immagine in FLASH, e
 * verifica tutto ciò che il loader userà: non scrive nulla, né nel
 * file né nello slot. Se il file è valido riempie un "piano di
 * caricamento"; il loader lo percorre poi con elf_next_segment() e
 * elf_next_abs32() per copiare il programma e correggere gli indirizzi.
 *
 * Questo modulo non usa nulla del kernel: solo <stdint.h>. Per questo
 * si può compilare e provare anche sul PC (tools/elf_test.c).
 */
#ifndef ELF_H
#define ELF_H

#include <stdint.h>

/* Byte di uno slot riservati a codice, dati e BSS. I 1024 byte che
 * restano sono lo stack. */
#define ELF_PROGRAM_LIMIT 3072u

/* Passo con cui la MPU può separare il codice dai dati: uno slot da
 * 4096 byte è diviso in 8 sottoregioni da 512 byte. La parte
 * scrivibile di un programma deve cominciare a un multiplo di questo
 * valore. */
#define ELF_PROTECT_STEP 512u

/* Risultato di elf_check(). Per la shell sono tutti lo stesso errore
 * (-4, "program cannot be loaded"); i nomi diversi servono alle prove
 * e a capire che cosa non va in un file. */
enum elf_result
{
    ELF_OK = 0,
    ELF_BAD_HEADER,         /* non è un ELF del tipo richiesto, o tabelle fuori dal file */
    ELF_BAD_SEGMENT,        /* un segmento da caricare non è valido */
    ELF_TOO_LARGE,          /* il programma non sta nei 3072 byte */
    ELF_BAD_ENTRY,          /* l'ingresso non è un indirizzo Thumb nel codice */
    ELF_BAD_SECTION,        /* una sezione usata non è valida, o è di un tipo vietato */
    ELF_BAD_RELOC,          /* una rilocazione punta fuori, o usa un simbolo non definito */
    ELF_BAD_RELOC_TYPE,     /* tipo di rilocazione non ammesso: vedi plan->reloc_type */
};

/* Il piano di caricamento. Gli indirizzi sono contati dall'inizio del
 * programma: il loader vi somma la base dello slot. */
struct elf_plan
{
    const uint8_t *file;    /* il file ELF, nell'immagine */
    uint32_t entry;         /* ingresso: indirizzo Thumb, con il bit 0 a 1 */
    uint32_t mem_end;       /* fine del programma in memoria (<= 3072) */
    uint32_t data_start;    /* primo indirizzo scrivibile: prima c'è solo
                             * codice. È un multiplo di 512, per la MPU */
    uint32_t reloc_type;    /* se il risultato è ELF_BAD_RELOC_TYPE: il tipo trovato */

    /* Le due tabelle del file, già controllate: le usano le funzioni
     * elf_next_*. Il loader non le tocca. */
    uint32_t ph_offset;     /* program header: posizione nel file... */
    uint32_t ph_count;      /* ...e numero di record */
    uint32_t sh_offset;     /* section header: posizione nel file... */
    uint32_t sh_count;      /* ...e numero di record */
};

/* Un pezzo da caricare (un segmento PT_LOAD). */
struct elf_segment
{
    const uint8_t *data;    /* i byte da copiare, dentro il file */
    uint32_t addr;          /* dove vanno, contando dall'inizio dello slot */
    uint32_t file_size;     /* quanti byte copiare */
    uint32_t mem_size;      /* quanti byte occupa: quelli oltre file_size
                             * sono il BSS e vanno azzerati */
};

/* Posizione di elf_next_abs32() nell'elenco delle rilocazioni. Prima
 * della prima chiamata deve valere { 0, 0 }. */
struct elf_reloc_cursor
{
    uint32_t section;       /* la sezione di rilocazioni in esame */
    uint32_t record;        /* il prossimo record di quella sezione */
};

/* Controlla il file ELF di `size` byte che comincia a `file`, pensando
 * di caricarlo nello slot che comincia a `slot_base`. Ritorna ELF_OK e
 * riempie *plan, oppure uno degli errori di enum elf_result. Non scrive
 * in memoria nient'altro che *plan. */
int elf_check( const uint8_t *file, uint32_t size, uint32_t slot_base,
               struct elf_plan *plan );

/* Scorre i segmenti da caricare di un piano valido. *cursor deve
 * valere 0 prima della prima chiamata. Ritorna 1 e riempie *seg, oppure
 * 0 quando i segmenti sono finiti. */
int elf_next_segment( const struct elf_plan *plan, uint32_t *cursor,
                      struct elf_segment *seg );

/* Scorre le parole da correggere (rilocazioni R_ARM_ABS32) di un piano
 * valido. Ritorna 1 e scrive in *addr l'indirizzo della parola,
 * contato dall'inizio dello slot e multiplo di 4; oppure 0 quando sono
 * finite. A ognuna il loader somma la base dello slot. */
int elf_next_abs32( const struct elf_plan *plan,
                    struct elf_reloc_cursor *cursor, uint32_t *addr );

#endif /* ELF_H */
