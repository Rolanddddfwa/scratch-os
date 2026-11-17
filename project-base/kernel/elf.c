/*
 * Controllo di un programma ELF prima di caricarlo: vedi kernel/elf.h
 * e la sezione 6.3 dell'assignment.
 *
 * Un file ELF descrive il suo contenuto con tre tabelle:
 *
 *   header del file (52 byte)     dice dove sono le altre due tabelle
 *                                 e qual è l'indirizzo di ingresso
 *   program header (32 byte l'uno) i "segmenti": che cosa caricare in
 *                                 memoria e a quale indirizzo
 *   section header (40 byte l'uno) le "sezioni": tra queste ci sono gli
 *                                 elenchi delle rilocazioni e la
 *                                 tabella dei simboli
 *
 * Il file arriva dall'immagine e può essere rotto in qualunque modo.
 * La regola è una sola: ogni numero letto dal file viene controllato
 * prima di essere usato come posizione o come dimensione. Così il
 * kernel non legge mai fuori dal file e il loader non scrive mai fuori
 * dallo slot.
 *
 * Ogni numero è little-endian. Gli offset si contano dal primo byte
 * del file.
 */

#include <stdint.h>
#include "kernel/elf.h"

/* --- Costanti del formato ELF a 32 bit ---------------------------- */

/* Dimensioni dei record. */
#define EH_SIZE         52u     /* header del file */
#define PH_SIZE         32u     /* un program header */
#define SH_SIZE         40u     /* un section header */
#define SYM_SIZE        16u     /* un simbolo */
#define REL_SIZE        8u      /* una rilocazione REL */

/* Header del file: posizione dei campi. */
#define EH_CLASS        4u      /* 1 byte: 1 = 32 bit */
#define EH_DATA         5u      /* 1 byte: 1 = little-endian */
#define EH_IDVERSION    6u      /* 1 byte: 1 */
#define EH_TYPE         16u     /* 2 byte: 2 = eseguibile (ET_EXEC) */
#define EH_MACHINE      18u     /* 2 byte: 40 = ARM */
#define EH_VERSION      20u     /* 4 byte: 1 */
#define EH_ENTRY        24u     /* 4 byte: indirizzo di ingresso */
#define EH_PHOFF        28u     /* 4 byte: dove sono i program header */
#define EH_SHOFF        32u     /* 4 byte: dove sono i section header */
#define EH_EHSIZE       40u     /* 2 byte: dimensione di questo header */
#define EH_PHENTSIZE    42u     /* 2 byte: dimensione di un program header */
#define EH_PHNUM        44u     /* 2 byte: quanti program header */
#define EH_SHENTSIZE    46u     /* 2 byte: dimensione di un section header */
#define EH_SHNUM        48u     /* 2 byte: quanti section header */

#define ET_EXEC         2u
#define EM_ARM          40u

/* Program header: posizione dei campi. */
#define PH_TYPE         0u
#define PH_OFFSET       4u      /* dove sono i byte nel file */
#define PH_VADDR        8u      /* indirizzo in memoria */
#define PH_FILESZ       16u     /* byte presenti nel file */
#define PH_MEMSZ        20u     /* byte occupati in memoria */
#define PH_FLAGS        24u

#define PT_LOAD         1u      /* segmento da caricare */
#define PT_DYNAMIC      2u      /* collegamento dinamico: vietato */
#define PT_INTERP       3u      /* collegamento dinamico: vietato */
#define PF_X            1u      /* si esegue */
#define PF_W            2u      /* si scrive */

/* Section header: posizione dei campi. */
#define SH_TYPE         4u
#define SH_FLAGS        8u
#define SH_ADDR         12u     /* indirizzo in memoria */
#define SH_OFFSET       16u     /* dove sono i byte nel file */
#define SH_SIZEOF       20u     /* dimensione in byte */
#define SH_LINK         24u     /* per una sezione REL: la tabella dei simboli */
#define SH_INFO         28u     /* per una sezione REL: la sezione da correggere */
#define SH_ENTSIZE      36u     /* dimensione di un record della sezione */

#define SHT_SYMTAB      2u      /* tabella dei simboli */
#define SHT_RELA        4u      /* rilocazioni con addendo: vietate */
#define SHT_DYNAMIC     6u      /* collegamento dinamico: vietato */
#define SHT_NOBITS      8u      /* BSS: nessun byte nel file */
#define SHT_REL         9u      /* rilocazioni */
#define SHF_ALLOC       2u      /* la sezione viene caricata in memoria */

/* Simbolo e rilocazione: posizione dei campi. */
#define SYM_SHNDX       14u     /* 2 byte: sezione del simbolo; 0 = non definito */
#define REL_OFFSET      0u      /* indirizzo del punto da correggere */
#define REL_INFO        4u      /* tipo (8 bit bassi) e numero del simbolo */

/* I soli tre tipi di rilocazione ammessi nel contenuto caricato. */
#define R_ARM_ABS32         2u  /* parola con un indirizzo completo */
#define R_ARM_THM_CALL      10u /* istruzione bl: salto relativo */
#define R_ARM_THM_JUMP24    30u /* istruzione b.w: salto relativo */

/* --- Funzioni di aiuto --------------------------------------------- */

/* Legge un numero a 16 bit little-endian. */
static uint32_t read_u16( const uint8_t *p )
{
    return ( uint32_t ) p[0] | ( uint32_t ) p[1] << 8;
}

/* Legge un numero a 32 bit little-endian. */
static uint32_t read_u32( const uint8_t *p )
{
    return ( uint32_t ) p[0]
         | ( uint32_t ) p[1] << 8
         | ( uint32_t ) p[2] << 16
         | ( uint32_t ) p[3] << 24;
}

/* L'intervallo [offset, offset + len) sta tutto dentro [0, limit)?
 * Scritto come `len <= limit - offset`, così offset + len non può
 * andare in overflow. */
static int fits( uint32_t offset, uint32_t len, uint32_t limit )
{
    return offset <= limit && len <= limit - offset;
}

/* Indirizzo del program header numero `index`. */
static const uint8_t *segment_at( const struct elf_plan *plan, uint32_t index )
{
    return plan->file + plan->ph_offset + index * PH_SIZE;
}

/* Indirizzo del section header numero `index`. */
static const uint8_t *section_at( const struct elf_plan *plan, uint32_t index )
{
    return plan->file + plan->sh_offset + index * SH_SIZE;
}

/* Il program header `ph` è un segmento da caricare, e non vuoto? */
static int is_load_segment( const uint8_t *ph )
{
    return read_u32( ph + PH_TYPE ) == PT_LOAD
        && read_u32( ph + PH_MEMSZ ) != 0;
}

/* La sezione `sh` è un elenco di rilocazioni per una sezione caricata?
 * Si usa solo dopo che check_rel_section() ha controllato sh_info. */
static int is_loaded_rel_section( const struct elf_plan *plan, const uint8_t *sh )
{
    if ( read_u32( sh + SH_TYPE ) != SHT_REL )
        return 0;
    const uint8_t *target = section_at( plan, read_u32( sh + SH_INFO ) );
    return ( read_u32( target + SH_FLAGS ) & SHF_ALLOC ) != 0;
}

/* --- 1. Header del file -------------------------------------------- */

/* Controlla l'identità del file e le due tabelle. Se tutto va bene
 * scrive nel piano dove sono le tabelle e quanti record hanno. */
static int check_header( const uint8_t *file, uint32_t size, struct elf_plan *plan )
{
    if ( size < EH_SIZE )                       /* nemmeno l'header intero */
        return ELF_BAD_HEADER;

    /* I primi 4 byte: 0x7f 'E' 'L' 'F'. */
    if ( file[0] != 0x7f || file[1] != 'E' || file[2] != 'L' || file[3] != 'F' )
        return ELF_BAD_HEADER;
    /* 32 bit, little-endian, versione 1. */
    if ( file[EH_CLASS] != 1 || file[EH_DATA] != 1 || file[EH_IDVERSION] != 1 )
        return ELF_BAD_HEADER;
    /* Eseguibile, per ARM, versione 1. */
    if ( read_u16( file + EH_TYPE ) != ET_EXEC
         || read_u16( file + EH_MACHINE ) != EM_ARM
         || read_u32( file + EH_VERSION ) != 1 )
        return ELF_BAD_HEADER;
    /* I record devono avere le dimensioni standard: tutto il resto del
     * codice calcola le posizioni con queste. */
    if ( read_u16( file + EH_EHSIZE ) != EH_SIZE
         || read_u16( file + EH_PHENTSIZE ) != PH_SIZE
         || read_u16( file + EH_SHENTSIZE ) != SH_SIZE )
        return ELF_BAD_HEADER;

    uint32_t ph_offset = read_u32( file + EH_PHOFF );
    uint32_t ph_count  = read_u16( file + EH_PHNUM );
    uint32_t sh_offset = read_u32( file + EH_SHOFF );
    uint32_t sh_count  = read_u16( file + EH_SHNUM );

    /* Le due tabelle devono stare tutte dentro il file. I conteggi
     * sono a 16 bit, quindi count * dimensione non va in overflow. */
    if ( ph_count == 0 || !fits( ph_offset, ph_count * PH_SIZE, size ) )
        return ELF_BAD_HEADER;
    if ( !fits( sh_offset, sh_count * SH_SIZE, size ) )
        return ELF_BAD_HEADER;

    plan->ph_offset = ph_offset;
    plan->ph_count  = ph_count;
    plan->sh_offset = sh_offset;
    plan->sh_count  = sh_count;
    return ELF_OK;
}

/* --- 2. Segmenti e ingresso ---------------------------------------- */

/* Controlla i segmenti da caricare e l'indirizzo di ingresso. Se tutto
 * va bene scrive nel piano entry, mem_end e data_start.
 *
 * I segmenti devono essere in ordine di indirizzo e non sovrapporsi:
 * prima quelli non scrivibili (codice e costanti), poi quelli
 * scrivibili (dati e BSS). Così lo slot ha un solo confine tra "si
 * esegue" e "si scrive", che è ciò che la MPU sa proteggere. */
static int check_segments( const uint8_t *file, uint32_t size, struct elf_plan *plan )
{
    uint32_t entry = read_u32( file + EH_ENTRY );
    uint32_t entry_addr = entry & ~1u;      /* l'indirizzo senza il bit Thumb */

    uint32_t loads = 0;                     /* segmenti da caricare trovati */
    uint32_t prev_end = 0;                  /* fine del segmento precedente */
    uint32_t data_start = 0;                /* inizio del primo segmento scrivibile */
    int writable_seen = 0;
    int entry_ok = 0;

    for ( uint32_t i = 0; i < plan->ph_count; i++ )
    {
        const uint8_t *ph = segment_at( plan, i );
        uint32_t type = read_u32( ph + PH_TYPE );

        if ( type == PT_DYNAMIC || type == PT_INTERP )
            return ELF_BAD_SEGMENT;         /* collegamento dinamico */
        if ( type != PT_LOAD )
            continue;                       /* altri tipi: non si caricano */

        uint32_t offset = read_u32( ph + PH_OFFSET );
        uint32_t vaddr  = read_u32( ph + PH_VADDR );
        uint32_t filesz = read_u32( ph + PH_FILESZ );
        uint32_t memsz  = read_u32( ph + PH_MEMSZ );
        uint32_t flags  = read_u32( ph + PH_FLAGS );

        /* I byte da copiare devono esistere nel file. */
        if ( filesz > memsz || !fits( offset, filesz, size ) )
            return ELF_BAD_SEGMENT;
        /* La destinazione deve stare nei 3072 byte del programma. */
        if ( !fits( vaddr, memsz, ELF_PROGRAM_LIMIT ) )
            return ELF_TOO_LARGE;
        if ( memsz == 0 )
            continue;                       /* segmento vuoto: nulla da fare */

        /* In ordine e senza sovrapposizioni. */
        if ( vaddr < prev_end )
            return ELF_BAD_SEGMENT;

        if ( flags & PF_W )
        {
            if ( flags & PF_X )             /* scrivibile ed eseguibile: mai */
                return ELF_BAD_SEGMENT;
            if ( !writable_seen )
            {
                /* Il confine codice/dati: deve essere esprimibile
                 * dalla MPU. */
                if ( vaddr % ELF_PROTECT_STEP != 0 )
                    return ELF_BAD_SEGMENT;
                data_start = vaddr;
                writable_seen = 1;
            }
        }
        else if ( writable_seen )
        {
            return ELF_BAD_SEGMENT;         /* codice dopo i dati */
        }

        /* L'ingresso deve cadere in un segmento eseguibile, in una
         * zona che ha davvero dei byte nel file (un'istruzione Thumb
         * ne occupa almeno 2). */
        if ( ( flags & PF_X ) && vaddr <= entry_addr
             && fits( entry_addr - vaddr, 2, filesz ) )
            entry_ok = 1;

        prev_end = vaddr + memsz;           /* <= 3072: nessun overflow */
        loads++;
    }

    if ( loads == 0 )
        return ELF_BAD_SEGMENT;             /* niente da caricare */
    if ( ( entry & 1u ) == 0 || !entry_ok )
        return ELF_BAD_ENTRY;               /* non Thumb, o fuori dal codice */

    plan->entry = entry;
    plan->mem_end = prev_end;
    /* Senza segmenti scrivibili, la parte scrivibile comincia alla
     * prima tacca da 512 dopo il programma (3072 è multiplo di 512,
     * quindi il risultato non lo supera). */
    plan->data_start = writable_seen
        ? data_start
        : ( prev_end + ELF_PROTECT_STEP - 1 ) / ELF_PROTECT_STEP * ELF_PROTECT_STEP;
    return ELF_OK;
}

/* --- 3. Sezioni e rilocazioni -------------------------------------- */

/* La sezione che occupa [addr, addr + sec_size) in memoria e comincia
 * a sec_offset nel file sta tutta dentro un segmento da caricare, nella
 * parte che ha byte nel file, e con la stessa corrispondenza tra
 * indirizzi e posizioni nel file?
 * Serve perché il loader copia per segmenti, mentre le rilocazioni
 * indicano i punti per sezione: le due viste devono coincidere. */
static int section_in_segment( const struct elf_plan *plan, uint32_t addr,
                               uint32_t sec_size, uint32_t sec_offset )
{
    for ( uint32_t i = 0; i < plan->ph_count; i++ )
    {
        const uint8_t *ph = segment_at( plan, i );
        if ( !is_load_segment( ph ) )
            continue;

        uint32_t vaddr  = read_u32( ph + PH_VADDR );
        uint32_t filesz = read_u32( ph + PH_FILESZ );

        if ( addr >= vaddr && fits( addr - vaddr, sec_size, filesz )
             && sec_offset == read_u32( ph + PH_OFFSET ) + ( addr - vaddr ) )
            return 1;
    }
    return 0;
}

/* Controlla una sezione di rilocazioni (tipo REL) e tutti i suoi
 * record. `sh` è il suo section header. `seen` ha un bit per ogni
 * parola del programma: ricorda quali sono già state indicate da una
 * rilocazione R_ARM_ABS32. */
static int check_rel_section( const uint8_t *file, uint32_t size, uint32_t slot_base,
                              struct elf_plan *plan, const uint8_t *sh,
                              uint32_t *seen )
{
    uint32_t rel_offset = read_u32( sh + SH_OFFSET );
    uint32_t rel_size   = read_u32( sh + SH_SIZEOF );

    /* I record sono da 8 byte e stanno tutti nel file. */
    if ( read_u32( sh + SH_ENTSIZE ) != REL_SIZE || rel_size % REL_SIZE != 0
         || !fits( rel_offset, rel_size, size ) )
        return ELF_BAD_SECTION;

    /* sh_info dice a quale sezione si applicano le rilocazioni. */
    uint32_t info = read_u32( sh + SH_INFO );
    if ( info >= plan->sh_count )
        return ELF_BAD_SECTION;
    const uint8_t *target = section_at( plan, info );

    /* Se quella sezione non viene caricata (per esempio le
     * informazioni di debug), queste rilocazioni non contano. */
    if ( ( read_u32( target + SH_FLAGS ) & SHF_ALLOC ) == 0 )
        return ELF_OK;

    /* La sezione da correggere deve avere byte nel file (non è BSS) e
     * stare dentro un segmento che verrà copiato. */
    uint32_t t_addr   = read_u32( target + SH_ADDR );
    uint32_t t_offset = read_u32( target + SH_OFFSET );
    uint32_t t_size   = read_u32( target + SH_SIZEOF );
    if ( read_u32( target + SH_TYPE ) == SHT_NOBITS
         || !section_in_segment( plan, t_addr, t_size, t_offset ) )
        return ELF_BAD_SECTION;

    /* sh_link dice qual è la tabella dei simboli usata dai record. */
    uint32_t link = read_u32( sh + SH_LINK );
    if ( link >= plan->sh_count )
        return ELF_BAD_SECTION;
    const uint8_t *symtab = section_at( plan, link );
    uint32_t sym_offset = read_u32( symtab + SH_OFFSET );
    uint32_t sym_size   = read_u32( symtab + SH_SIZEOF );
    if ( read_u32( symtab + SH_TYPE ) != SHT_SYMTAB
         || read_u32( symtab + SH_ENTSIZE ) != SYM_SIZE
         || !fits( sym_offset, sym_size, size ) )
        return ELF_BAD_SECTION;
    uint32_t sym_count = sym_size / SYM_SIZE;

    /* Ora i record, uno per uno. */
    for ( uint32_t k = 0; k < rel_size / REL_SIZE; k++ )
    {
        const uint8_t *rel = file + rel_offset + k * REL_SIZE;
        uint32_t r_offset = read_u32( rel + REL_OFFSET );
        uint32_t r_info   = read_u32( rel + REL_INFO );
        uint32_t type     = r_info & 0xffu;     /* gli 8 bit bassi */
        uint32_t sym      = r_info >> 8;        /* il resto */

        /* Solo tre tipi sono ammessi. Il tipo trovato va nel piano:
         * il loader lo stampa nel messaggio di errore. */
        if ( type != R_ARM_ABS32 && type != R_ARM_THM_CALL
             && type != R_ARM_THM_JUMP24 )
        {
            plan->reloc_type = type;
            return ELF_BAD_RELOC_TYPE;
        }

        /* Il punto da correggere (4 byte) deve stare nella sezione. */
        if ( r_offset < t_addr || !fits( r_offset - t_addr, 4, t_size ) )
            return ELF_BAD_RELOC;

        /* Il simbolo deve esistere ed essere definito (sezione != 0). */
        if ( sym >= sym_count
             || read_u16( file + sym_offset + sym * SYM_SIZE + SYM_SHNDX ) == 0 )
            return ELF_BAD_RELOC;

        if ( type == R_ARM_ABS32 )
        {
            /* Una parola: allineata a 4. Il loader vi sommerà la base
             * dello slot, e la somma non deve superare i 32 bit. */
            if ( ( r_offset & 3u ) != 0 )
                return ELF_BAD_RELOC;
            uint32_t word = read_u32( file + t_offset + ( r_offset - t_addr ) );
            if ( word > 0xffffffffu - slot_base )
                return ELF_BAD_RELOC;

            /* Ogni parola va corretta una volta sola: indicata due
             * volte, riceverebbe la base due volte. Il punto sta nei
             * 3072 byte, quindi il numero della parola è sotto 768. */
            uint32_t index = r_offset / 4;
            uint32_t bit = 1u << ( index % 32 );
            if ( seen[index / 32] & bit )
                return ELF_BAD_RELOC;
            seen[index / 32] |= bit;
        }
        else if ( ( r_offset & 1u ) != 0 )
        {
            /* Un salto Thumb: allineato a 2. È relativo, quindi non
             * c'è nulla da correggere. */
            return ELF_BAD_RELOC;
        }
    }
    return ELF_OK;
}

/* Scorre tutte le sezioni: rifiuta i tipi vietati e controlla ogni
 * sezione di rilocazioni. Delle altre sezioni (nomi, debug, stringhe)
 * non guarda nulla, perché il loader non le usa. */
static int check_sections( const uint8_t *file, uint32_t size, uint32_t slot_base,
                           struct elf_plan *plan )
{
    /* Un bit per ogni parola del programma: 3072 / 4 = 768 bit, cioè
     * 24 numeri da 32 bit (96 byte sullo stack). Azzerati con un
     * ciclo: scrivendo `= { 0 }` il compilatore chiamerebbe memset,
     * che nel kernel non esiste. */
    uint32_t seen[ELF_PROGRAM_LIMIT / 4 / 32];
    for ( uint32_t i = 0; i < sizeof( seen ) / sizeof( seen[0] ); i++ )
        seen[i] = 0;

    for ( uint32_t i = 0; i < plan->sh_count; i++ )
    {
        const uint8_t *sh = section_at( plan, i );
        uint32_t type = read_u32( sh + SH_TYPE );

        if ( type == SHT_RELA || type == SHT_DYNAMIC )
            return ELF_BAD_SECTION;
        if ( type == SHT_REL )
        {
            int result = check_rel_section( file, size, slot_base, plan, sh, seen );
            if ( result != ELF_OK )
                return result;
        }
    }
    return ELF_OK;
}

/* --- Funzioni pubbliche -------------------------------------------- */

int elf_check( const uint8_t *file, uint32_t size, uint32_t slot_base,
               struct elf_plan *plan )
{
    plan->file = file;
    plan->reloc_type = 0;

    /* Ogni passo si fida solo di ciò che i precedenti hanno già
     * controllato: prima le tabelle, poi i segmenti (che servono per
     * controllare le sezioni), infine sezioni e rilocazioni. */
    int result = check_header( file, size, plan );
    if ( result == ELF_OK )
        result = check_segments( file, size, plan );
    if ( result == ELF_OK )
        result = check_sections( file, size, slot_base, plan );
    return result;
}

int elf_next_segment( const struct elf_plan *plan, uint32_t *cursor,
                      struct elf_segment *seg )
{
    while ( *cursor < plan->ph_count )
    {
        const uint8_t *ph = segment_at( plan, *cursor );
        ( *cursor )++;

        if ( is_load_segment( ph ) )
        {
            seg->data      = plan->file + read_u32( ph + PH_OFFSET );
            seg->addr      = read_u32( ph + PH_VADDR );
            seg->file_size = read_u32( ph + PH_FILESZ );
            seg->mem_size  = read_u32( ph + PH_MEMSZ );
            return 1;
        }
    }
    return 0;                               /* segmenti finiti */
}

int elf_next_abs32( const struct elf_plan *plan,
                    struct elf_reloc_cursor *cursor, uint32_t *addr )
{
    while ( cursor->section < plan->sh_count )
    {
        const uint8_t *sh = section_at( plan, cursor->section );

        if ( is_loaded_rel_section( plan, sh ) )
        {
            const uint8_t *records = plan->file + read_u32( sh + SH_OFFSET );
            uint32_t count = read_u32( sh + SH_SIZEOF ) / REL_SIZE;

            while ( cursor->record < count )
            {
                const uint8_t *rel = records + cursor->record * REL_SIZE;
                cursor->record++;

                if ( ( read_u32( rel + REL_INFO ) & 0xffu ) == R_ARM_ABS32 )
                {
                    *addr = read_u32( rel + REL_OFFSET );
                    return 1;
                }
            }
        }
        cursor->section++;                  /* sezione finita: la prossima */
        cursor->record = 0;
    }
    return 0;                               /* rilocazioni finite */
}
