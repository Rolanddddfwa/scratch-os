/*
 * Lettura dell'immagine dei programmi (formato IMG1): vedi kernel/image.h
 * e la sezione 6.1 dell'assignment.
 *
 * Com'è fatta l'immagine:
 *
 *   byte 0..19        header (5 numeri da 4 byte)
 *   directory_offset  directory: `count` voci da 24 byte
 *   ...               i file dei programmi, uno dopo l'altro
 *
 * Ogni numero è senza segno, a 32 bit, little-endian. Gli offset si
 * contano dal primo byte dell'immagine.
 */

#include <stdint.h>
#include "kernel/image.h"

/* Indirizzo a cui QEMU carica fs.img: lo definisce il linker script
 * (scripts/mps2_m3.ld). Conta solo l'indirizzo del simbolo. */
extern const uint8_t __fs_image_start[];

/* --- Costanti del formato ----------------------------------------- */

#define IMG_MAGIC        0x31474d49u    /* i byte 'I' 'M' 'G' '1' */
#define IMG_VERSION      1u
#define IMG_HEADER_SIZE  20u            /* byte dell'header */
#define IMG_MAX_SIZE     0x00200000u    /* 2 MiB: lo spazio in FLASH */
#define IMG_MAX_COUNT    32u            /* voci al massimo */
#define IMG_ENTRY_SIZE   24u            /* byte di una voce di directory */
#define IMG_NAME_FIELD   16u            /* byte del campo nome */
#define IMG_MIN_FILE     40u            /* dimensione minima di un file */

/* Posizione dei campi dentro l'header. */
#define HDR_MAGIC        0u
#define HDR_VERSION      4u
#define HDR_IMAGE_SIZE   8u
#define HDR_COUNT        12u
#define HDR_DIR_OFFSET   16u

/* Posizione dei campi dentro una voce di directory. */
#define ENT_NAME         0u
#define ENT_OFFSET       16u
#define ENT_SIZE         20u

/* --- Stato --------------------------------------------------------- */

/* Risultato del controllo, ricordato dopo la prima volta. L'immagine
 * sta in FLASH e non può cambiare mentre il sistema gira, quindi
 * basta controllarla una volta sola. */
enum { IMAGE_UNCHECKED = 0, IMAGE_VALID, IMAGE_INVALID };
static int image_state = IMAGE_UNCHECKED;

/* Copiati dall'header quando l'immagine risulta valida. */
static uint32_t image_entries;          /* numero di voci */
static uint32_t image_dir_offset;       /* dove comincia la directory */

/* --- Funzioni di aiuto --------------------------------------------- */

/* Legge un numero a 32 bit little-endian: il primo byte è il meno
 * significativo. Letto un byte alla volta, funziona a qualunque
 * indirizzo, anche non allineato. */
static uint32_t read_u32( const uint8_t *p )
{
    return ( uint32_t ) p[0]
         | ( uint32_t ) p[1] << 8
         | ( uint32_t ) p[2] << 16
         | ( uint32_t ) p[3] << 24;
}

/* Indirizzo della voce di directory numero `index`. */
static const uint8_t *entry_at( uint32_t dir_offset, uint32_t index )
{
    return __fs_image_start + dir_offset + index * IMG_ENTRY_SIZE;
}

/* Un carattere ammesso in un nome: A-Z, a-z, 0-9, '_' e '-'. */
static int name_char_ok( uint8_t c )
{
    return ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' )
        || ( c >= '0' && c <= '9' ) || c == '_' || c == '-';
}

/* Il campo nome (16 byte) è valido? Deve contenere da 1 a 15
 * caratteri ammessi, seguiti da un NUL. I byte dopo il primo NUL non
 * si guardano. */
static int name_ok( const uint8_t *name )
{
    uint32_t len = 0;

    while ( len < IMG_NAME_FIELD && name[len] != 0 )
    {
        if ( !name_char_ok( name[len] ) )
            return 0;
        len++;
    }
    /* len == 0: nome vuoto. len == 16: manca il NUL. */
    return len >= 1 && len <= IMAGE_NAME_MAX;
}

/* Due stringhe che finiscono con NUL sono uguali? */
static int str_equal( const char *a, const char *b )
{
    while ( *a != 0 && *a == *b )
    {
        a++;
        b++;
    }
    return *a == *b;
}

/* --- Controllo dell'immagine --------------------------------------- */

/* Controlla tutte le regole della sezione 6.1. Ritorna 1 se l'immagine
 * è valida, 0 altrimenti. Non scrive nulla nell'immagine.
 *
 * L'ordine dei controlli conta: ogni numero viene verificato prima di
 * essere usato per calcolare un indirizzo, così il kernel non legge
 * mai fuori dall'immagine anche se questa è corrotta. I confronti
 * sono scritti nella forma `size <= fine - offset`, che non puo'
 * andare in overflow (la somma `offset + size` invece potrebbe). */
static int image_check( void )
{
    const uint8_t *hdr = __fs_image_start;

    /* 1. L'header. */
    if ( read_u32( hdr + HDR_MAGIC ) != IMG_MAGIC )
        return 0;
    if ( read_u32( hdr + HDR_VERSION ) != IMG_VERSION )
        return 0;

    uint32_t image_size = read_u32( hdr + HDR_IMAGE_SIZE );
    uint32_t count      = read_u32( hdr + HDR_COUNT );
    uint32_t dir_offset = read_u32( hdr + HDR_DIR_OFFSET );

    if ( image_size < IMG_HEADER_SIZE || image_size > IMG_MAX_SIZE )
        return 0;
    if ( count < 1 || count > IMG_MAX_COUNT )
        return 0;
    if ( dir_offset < IMG_HEADER_SIZE || ( dir_offset & 3u ) != 0 )
        return 0;

    /* 2. La directory deve stare tutta dentro l'immagine.
     * count <= 32, quindi dir_size <= 768 e non va in overflow. */
    uint32_t dir_size = count * IMG_ENTRY_SIZE;
    if ( dir_offset > image_size || dir_size > image_size - dir_offset )
        return 0;

    /* I file cominciano dopo la directory, a un multiplo di 4.
     * dir_offset e dir_size sono già multipli di 4, quindi anche la
     * somma lo è; l'arrotondamento resta scritto perché è la regola
     * del formato. */
    uint32_t contents_start = ( dir_offset + dir_size + 3u ) & ~3u;

    /* 3. Ogni voce, da sola. */
    for ( uint32_t i = 0; i < count; i++ )
    {
        const uint8_t *e = entry_at( dir_offset, i );
        uint32_t offset = read_u32( e + ENT_OFFSET );
        uint32_t size   = read_u32( e + ENT_SIZE );

        if ( !name_ok( e + ENT_NAME ) )
            return 0;
        if ( ( offset & 3u ) != 0 || offset < contents_start )
            return 0;
        if ( size < IMG_MIN_FILE )
            return 0;
        /* Il file [offset, offset + size) deve stare nell'immagine. */
        if ( offset > image_size || size > image_size - offset )
            return 0;
    }

    /* 4. Ogni coppia di voci: nomi diversi e file non sovrapposti.
     * Qui offset + size non va in overflow: il passo 3 ha già detto
     * che è al massimo image_size (2 MiB). */
    for ( uint32_t i = 0; i < count; i++ )
    {
        const uint8_t *a = entry_at( dir_offset, i );
        uint32_t a_start = read_u32( a + ENT_OFFSET );
        uint32_t a_end   = a_start + read_u32( a + ENT_SIZE );

        for ( uint32_t j = i + 1; j < count; j++ )
        {
            const uint8_t *b = entry_at( dir_offset, j );
            uint32_t b_start = read_u32( b + ENT_OFFSET );
            uint32_t b_end   = b_start + read_u32( b + ENT_SIZE );

            if ( str_equal( ( const char * ) ( a + ENT_NAME ),
                            ( const char * ) ( b + ENT_NAME ) ) )
                return 0;
            /* Due intervalli si toccano se ognuno comincia prima
             * della fine dell'altro. */
            if ( a_start < b_end && b_start < a_end )
                return 0;
        }
    }

    /* Tutto in regola: ricordo i due numeri che servono dopo. */
    image_entries = count;
    image_dir_offset = dir_offset;
    return 1;
}

/* L'immagine è valida? La prima chiamata la controlla, le successive
 * riusano il risultato. */
static int image_valid( void )
{
    if ( image_state == IMAGE_UNCHECKED )
        image_state = image_check() ? IMAGE_VALID : IMAGE_INVALID;
    return image_state == IMAGE_VALID;
}

/* --- Funzioni pubbliche -------------------------------------------- */

int image_count( void )
{
    if ( !image_valid() )
        return -1;
    return ( int ) image_entries;
}

const char *image_name( unsigned index )
{
    if ( !image_valid() || index >= image_entries )
        return 0;
    return ( const char * ) ( entry_at( image_dir_offset, index ) + ENT_NAME );
}

int image_find( const char *name, const uint8_t **file, uint32_t *size )
{
    if ( !image_valid() )
        return -1;

    for ( uint32_t i = 0; i < image_entries; i++ )
    {
        const uint8_t *e = entry_at( image_dir_offset, i );

        if ( str_equal( name, ( const char * ) ( e + ENT_NAME ) ) )
        {
            *file = __fs_image_start + read_u32( e + ENT_OFFSET );
            *size = read_u32( e + ENT_SIZE );
            return 0;
        }
    }
    return -1;                          /* nessun programma con quel nome */
}
