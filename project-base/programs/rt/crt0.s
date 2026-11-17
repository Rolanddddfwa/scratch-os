/*
 * Codice di avvio di un programma caricabile.
 *
 * Il kernel non chiama main(): fa partire il task dall'indirizzo di
 * ingresso scritto nel file ELF, cioè da _start (vedi ENTRY nel linker
 * script program.ld). _start chiama main() e, quando main() ritorna,
 * chiede al kernel di chiudere il programma con la SVC 4 (sys_exit).
 * Così "ritornare da main" e "chiamare sys_exit()" fanno la stessa cosa.
 *
 * Qui non c'è nulla da inizializzare:
 *   - lo stack pointer lo ha già impostato il kernel (cima dello slot);
 *   - i dati iniziali li ha copiati il loader, che ha anche azzerato
 *     il BSS.
 *
 * Equivalente in C:
 *     void _start(void) {
 *         main();
 *         sys_exit();        // non ritorna
 *     }
 */
    .syntax unified             /* sintassi ARM moderna */
    .cpu cortex-m3              /* solo istruzioni che questa CPU possiede */
    .thumb                      /* codice Thumb, l'unico che il Cortex-M esegue */

    /* Sezione a parte: il linker script la mette per prima, così
     * _start sta all'indirizzo 0 del programma. */
    .section .text.start,"ax",%progbits

    .global _start              /* visibile al linker: è il punto di ingresso */
    .type _start, %function     /* è codice */
    .thumb_func                 /* è codice Thumb: il suo indirizzo ha il bit 0 a 1 */
_start:
    bl main                     /* main(); il valore di ritorno si ignora */
    svc #4                      /* svc(SYS_EXIT): il programma è finito */
    b .                         /* mai raggiunto: il kernel non torna qui */
    .size _start, . - _start
