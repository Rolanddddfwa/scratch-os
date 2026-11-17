/*
 * Linker script di un programma caricabile.
 *
 * Il programma è collegato come se partisse dall'indirizzo 0. Il
 * kernel lo copia poi in uno slot di RAM (per esempio 0x20002000) e
 * corregge gli indirizzi sommando la base dello slot: per farlo usa
 * le rilocazioni che l'opzione --emit-relocs lascia nel file ELF.
 *
 * Com'è fatto uno slot da 4096 byte:
 *
 *   0 ............ codice e costanti     si legge e si esegue
 *   DATA_START ... dati iniziali, BSS    si legge e si scrive
 *   3072 ......... limite del programma
 *   3072..4095 ... stack (cresce verso il basso da 4096)
 */

ENTRY(_start)                   /* il programma parte da _start (rt/crt0.s) */

/* La MPU del Cortex-M3 divide una regione da 4 KiB in 8 sottoregioni
 * da 512 byte, e può dare permessi diversi solo a sottoregioni intere.
 * Per proteggere il codice dalla scrittura e i dati dall'esecuzione
 * (W^X), il confine tra i due deve quindi cadere su un multiplo di 512. */
MPU_STEP = 512;

/* Due segmenti dichiarati a mano: il codice (lettura ed esecuzione,
 * FLAGS 5) e i dati (lettura e scrittura, FLAGS 6). Lasciando decidere
 * al linker, quando codice e dati distano meno di una pagina li unisce
 * in un solo segmento con tutti i permessi, e il W^X non è più
 * possibile. */
PHDRS
{
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0;                      /* collegato all'indirizzo 0 */

    /* Codice. .text.start (cioè _start) per primo, all'indirizzo 0. */
    .text :
    {
        KEEP(*(.text.start))    /* KEEP: non scartarlo, anche se nessuno lo "chiama" */
        *(.text .text.*)        /* tutto il resto del codice */
        . = ALIGN(4);
    } :code

    /* Costanti e stringhe: stanno con il codice, nella parte che non
     * si può scrivere. */
    .rodata :
    {
        *(.rodata .rodata.*)
        . = ALIGN(4);
    }
    __code_end = .;             /* fine di codice e costanti */

    /* Qui comincia la parte scrivibile. */
    . = ALIGN(MPU_STEP);
    __data_start = .;

    /* Variabili con un valore iniziale: i loro byte sono nel file.
     * LONG(0) riserva sempre una parola: così ogni programma ha un
     * segmento scrivibile, all'indirizzo giusto, anche senza variabili.
     * Lo richiede il generatore del test kit (big/generate_fixtures.py),
     * che costruisce il file too_large allungando quel segmento. */
    .data :
    {
        LONG(0)
        *(.data .data.*)
        . = ALIGN(4);
    } :data

    /* Variabili che partono da zero: nel file non occupano spazio,
     * il loader azzera la zona. Stanno nello stesso segmento dei dati:
     * è la parte del segmento oltre i byte presenti nel file. */
    .bss :
    {
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
    } :data
    __program_end = .;          /* fine del programma: deve essere <= 3072 */

    /* Tabelle per le eccezioni C++ e note del compilatore: un programma
     * C senza libreria non le usa. Se restassero nel file sarebbero
     * contenuto "caricato" con rilocazioni che il kernel rifiuta. */
    /DISCARD/ :
    {
        *(.ARM.exidx* .ARM.extab*)
        *(.comment .note*)
    }
}
