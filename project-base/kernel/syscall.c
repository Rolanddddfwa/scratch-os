/*
 * Embedded Operating Systems and Architecture course
 * Licensed under the MIT License. See LICENSE in the repository root.
 *
 * Contact: Stefano Di Carlo <stefano.dicarlo@polito.it>
 *
 * System calls, kernel side: find which service `svc #n` asked for,
 * run it, and hand the result back to the caller.
 *
 * Everything here runs inside the SVCall exception, so it is
 * privileged: it may touch the drivers and the hardware registers.
 *
 * One service does not return to its caller: SYS_START_TASKS (main()
 * starts the first task). It enters a task instead, so svc_dispatch()
 * returns the EXC_RETURN value that ends the exception (see
 * kernel/svc_entry.s).
 *
 * Since week 5, some services give the CPU away: yield, delay,
 * delay_until and exit. They do not switch here. They only change the
 * task's state and make PendSV pending (task_request_switch()). PendSV
 * has a lower priority than SVCall (0xE0 against 0x80), so it waits:
 * it runs right after this exception ends, and switches then. That is
 * why exit can now be an ordinary entry of the table: by the time the
 * task would resume after its `svc`, PendSV has already switched away,
 * and a FINISHED task is never chosen again.
 */

#include <stdint.h>
#include "kernel/syscall.h"
#include "lib/syscalls.h"
#include "drivers/uart.h"
#include "drivers/systick.h"
#include "kernel/task.h"
#include "kernel/trace.h"
#include "kernel/image.h"
#include "kernel/loader.h"

/* Every service has the same shape: up to three arguments (the
 * caller's r0, r1, r2) and one result (which becomes the caller's r0).
 * A service that needs fewer arguments simply ignores the others.
 * syscall_fn is the type "pointer to such a function". */
typedef uint32_t ( *syscall_fn )( uint32_t a0, uint32_t a1, uint32_t a2 );

/* Is [p, p + len) fully inside [start, end)?
 * Written as `len <= end - p` so that p + len can never wrap around. */
static int in_range( uint32_t p, uint32_t len, uint32_t start, uint32_t end )
{
    return p >= start && p <= end && len <= end - p;
}

/* Progetto, punto D: il task t può LEGGERE [p, p + len)? Può leggere
 * la propria partizione (codice compreso) e la FLASH, dove stanno le
 * stringhe costanti dei task statici: la stessa regola di SYS_WRITE. */
static int readable_range( const struct tcb *t, uint32_t p, uint32_t len )
{
    return in_range( p, len, t->part_start, t->part_end )
        || in_range( p, len, 0x00000000, 0x00400000 );
}

/* Progetto, punto E: il task t può SCRIVERE in [p, p + len)?
 * Per un programma caricato con il W^X la parte scrivibile della
 * partizione comincia dopo il codice. Il kernel non deve scrivere nel
 * codice per conto del programma: sarebbe un modo di aggirare il W^X,
 * e la MPU fermerebbe il kernel stesso con un fault non recuperabile.
 * Per i task statici code_size è 0 e vale tutta la partizione. */
static int writable_range( const struct tcb *t, uint32_t p, uint32_t len )
{
    return in_range( p, len, t->part_start + t->code_size, t->part_end );
}

/* SYS_WRITE: a0 = buf, a1 = len. Put as many bytes as fit into the
 * UART send buffer and return how many were taken.
 * It must never wait for room. We are inside the SVCall handler
 * (priority 0x80), and the UART send interrupt that empties the buffer
 * has a less urgent priority (0xC0): it cannot run until we return. If
 * we waited here for room, nothing would ever make room, and the
 * system would freeze. So we return the count, possibly 0, and the
 * caller tries again later. */
static uint32_t sys_write_impl( uint32_t a0, uint32_t a1, uint32_t a2 )
{
    (void) a2;                                  /* not used */
    const char *buf = ( const char * ) a0;      /* the number is really an address */
    unsigned len = a1;

    /* A task must not make the kernel read memory the task itself may
     * not read. Accept the buffer only inside its own partition or
     * inside FLASH (string constants). The kernel itself is trusted. */
    const struct tcb *t = tcb_current;
    if ( t != 0
         && !in_range( a0, len, t->part_start, t->part_end )
         && !in_range( a0, len, 0x00000000, 0x00400000 ) )
        return ( uint32_t ) -1;

    return uart_write( buf, len );              /* bytes accepted, 0..len */
}

/* SYS_READ: no arguments. Return one received byte, or -1 if none. */
static uint32_t sys_read_impl( uint32_t a0, uint32_t a1, uint32_t a2 )
{
    (void) a0;                                  /* not used */
    (void) a1;
    (void) a2;
    /* -1 becomes 0xFFFFFFFF here; the caller's `int` turns it back into -1. */
    return ( uint32_t ) uart_getc_polled();
}

/* SYS_UPTIME: no arguments. Return the milliseconds since start. */
static uint32_t sys_uptime_impl( uint32_t a0, uint32_t a1, uint32_t a2 )
{
    (void) a0;                                  /* not used */
    (void) a1;
    (void) a2;
    return systick_uptime_ms();
}

/* The scheduler services below only make sense for a task. Before the
 * first task starts (tcb_current == 0), they answer -1. */

/* SYS_YIELD: no arguments. The calling task gives up the rest of its
 * time slice: it stays READY, and the next READY task runs. If no other
 * task is READY, the scheduler picks the caller again. */
static uint32_t sys_yield_impl( uint32_t a0, uint32_t a1, uint32_t a2 )
{
    (void) a0;                                  /* not used */
    (void) a1;
    (void) a2;
    if ( tcb_current == 0 )
        return ( uint32_t ) -1;
    task_request_switch();                      /* PendSV runs after this exception */
    return 0;
}

/* SYS_DELAY: a0 = ticks. Block the caller for that many ticks, counted
 * from now. A delay of 0 is just a yield. */
static uint32_t sys_delay_impl( uint32_t a0, uint32_t a1, uint32_t a2 )
{
    (void) a1;                                  /* not used */
    (void) a2;
    if ( tcb_current == 0 )
        return ( uint32_t ) -1;
    if ( a0 == 0 )
        task_request_switch();                  /* delay(0) = yield */
    else
        task_block_until( tick_count + a0 );    /* READY again at tick now + a0 */
    return 0;
}

/* SYS_DELAY_UNTIL: a0 = last_wake (an address in the task's
 * partition), a1 = period. Block the caller until tick *last_wake +
 * period, and store that tick in *last_wake for the next call. Unlike
 * SYS_DELAY, the time the task spent working does not push the next
 * wake-up later: the task wakes at last_wake + period, + 2 * period,
 * ... exactly, which is what a periodic task needs. */
static uint32_t sys_delay_until_impl( uint32_t a0, uint32_t a1, uint32_t a2 )
{
    (void) a2;                                  /* not used */
    const struct tcb *t = tcb_current;
    if ( t == 0 )
        return ( uint32_t ) -1;

    /* The kernel is going to read and WRITE *last_wake. As for
     * SYS_WRITE, accept the address only inside the caller's own
     * partition, or a task could make the kernel write anywhere. It
     * must also be a multiple of 4, as every uint32_t is.
     * Progetto, punto E: per un programma caricato vale solo la parte
     * scrivibile dello slot, non il codice (vedi writable_range). */
    if ( !writable_range( t, a0, sizeof( uint32_t ) )
         || ( a0 & 3 ) != 0 )
        return ( uint32_t ) -1;

    uint32_t *last_wake = ( uint32_t * ) a0;    /* the number is really an address */
    uint32_t wake = *last_wake + a1;            /* the next wake-up tick */
    *last_wake = wake;                          /* remembered for the next period */

    /* Is wake still in the future? Same wrap-safe test as in
     * task_increment_tick(). If the task is late (wake already passed),
     * it does not block at all: it goes on at once. */
    if ( ( int32_t ) ( wake - tick_count ) > 0 )
        task_block_until( wake );
    return 0;
}

/* SYS_EXIT: no arguments. The calling task is finished: it is marked
 * FINISHED and never chosen again. The result is never seen. */
static uint32_t sys_exit_impl( uint32_t a0, uint32_t a1, uint32_t a2 )
{
    (void) a0;                                  /* not used */
    (void) a1;
    (void) a2;
    if ( tcb_current == 0 )
        return ( uint32_t ) -1;
    task_finish_current();                      /* PendSV switches away after this exception */
    return 0;
}

/* SYS_TICK_COUNT: no arguments. Return the ticks since the scheduler
 * started (one tick = 10 ms). */
static uint32_t sys_tick_count_impl( uint32_t a0, uint32_t a1, uint32_t a2 )
{
    (void) a0;                                  /* not used */
    (void) a1;
    (void) a2;
    return tick_count;
}

/* SYS_TRACE_DUMP: no arguments. Print the scheduler trace and its
 * Gantt chart on the UART (kernel/trace.c). */
static uint32_t sys_trace_dump_impl( uint32_t a0, uint32_t a1, uint32_t a2 )
{
    (void) a0;                                  /* not used */
    (void) a1;
    (void) a2;
    trace_print();
    return 0;
}

/* Wait bookkeeping is supplied even though the untouched base has no loader.
 * Student loader code supplies SYS_PROGRAM_RUN and creates program tasks. */
static uint32_t sys_program_wait_impl( uint32_t a0, uint32_t a1, uint32_t a2 )
{
    (void) a1;
    (void) a2;
    uint32_t *frame;
    __asm volatile ( "mrs %0, psp" : "=r" ( frame ) );
    return (uint32_t) task_wait_program( a0, &frame[0] );
}

/* SYS_IMAGE_COUNT: nessun argomento. Ritorna il numero di programmi
 * nell'immagine (1..32), oppure -1 se l'immagine non è valida. */
static uint32_t sys_image_count_impl( uint32_t a0, uint32_t a1, uint32_t a2 )
{
    (void) a0;                                  /* non usati */
    (void) a1;
    (void) a2;
    return ( uint32_t ) image_count();
}

/* SYS_IMAGE_NAME: a0 = indice, a1 = destinazione, a2 = capacità in
 * byte della destinazione. Copia il nome del programma numero a0, con
 * il NUL finale, e ritorna 0.
 * Ritorna -1 se la capacità non è tra 1 e 16, se la destinazione non
 * sta tutta nella parte scrivibile della partizione del chiamante (non
 * vale la FLASH, e nemmeno il codice di un programma caricato), se
 * l'immagine o l'indice non sono validi, oppure se
 * il nome non entra. In quest'ultimo caso copia comunque l'inizio del
 * nome, chiuso da un NUL. */
static uint32_t sys_image_name_impl( uint32_t a0, uint32_t a1, uint32_t a2 )
{
    uint32_t index = a0;
    char *dst = ( char * ) a1;                  /* il numero è un indirizzo */
    uint32_t capacity = a2;

    const struct tcb *t = tcb_current;
    if ( t == 0 )                               /* nessun task: nessuna partizione */
        return ( uint32_t ) -1;

    /* Prima tutti i controlli, poi la scrittura: il kernel sta per
     * scrivere `capacity` byte a un indirizzo scelto dal task, e non
     * deve farsi usare per scrivere fuori dalla sua partizione. */
    if ( capacity < 1 || capacity > IMAGE_NAME_MAX + 1 )
        return ( uint32_t ) -1;
    if ( !writable_range( t, a1, capacity ) )
        return ( uint32_t ) -1;

    const char *name = image_name( index );     /* 0: immagine o indice non validi */
    if ( name == 0 )
        return ( uint32_t ) -1;

    /* Copia al massimo capacity - 1 caratteri: l'ultimo posto è
     * riservato al NUL, così la destinazione è sempre una stringa. */
    uint32_t i = 0;
    while ( i < capacity - 1 && name[i] != 0 )
    {
        dst[i] = name[i];
        i++;
    }
    dst[i] = 0;

    /* Se mi sono fermato sul NUL del nome, è entrato tutto. */
    return name[i] == 0 ? 0 : ( uint32_t ) -1;
}

/* SYS_PROGRAM_RUN: a0 = nome del programma (stringa del chiamante).
 * Carica il programma in uno slot libero e lo avvia. Ritorna l'id del
 * nuovo task, oppure -1 (nome o immagine non validi), -2 (programma non
 * trovato), -3 (nessuno slot libero), -4 (file non valido).
 *
 * Qui si controlla solo il nome, perché arriva da un task: deve essere
 * non vuoto e finire con un NUL entro 16 byte, e ogni byte letto deve
 * stare in memoria che il chiamante può leggere. Il nome viene copiato
 * in un array del kernel; tutto il resto lo fa loader_run(). */
static uint32_t sys_program_run_impl( uint32_t a0, uint32_t a1, uint32_t a2 )
{
    (void) a1;                                  /* non usati */
    (void) a2;
    const struct tcb *t = tcb_current;
    if ( t == 0 )                               /* nessun task: nessuna partizione */
        return ( uint32_t ) -1;

    char name[IMAGE_NAME_MAX + 1];              /* 15 caratteri + NUL */
    uint32_t len;
    for ( len = 0; len <= IMAGE_NAME_MAX; len++ )
    {
        /* Un byte alla volta: prima si controlla l'indirizzo, poi si
         * legge. Così non si legge mai oltre il NUL, né fuori dalla
         * memoria del chiamante. */
        if ( !readable_range( t, a0 + len, 1 ) )
            return ( uint32_t ) -1;
        name[len] = *( const char * ) ( a0 + len );
        if ( name[len] == '\0' )
            break;
    }
    /* len == 0: nome vuoto. len == 16: nessun NUL nei primi 16 byte. */
    if ( len == 0 || len > IMAGE_NAME_MAX )
        return ( uint32_t ) -1;

    return ( uint32_t ) loader_run( name );
}

/* The dispatch table: table[n] is the function for service n.
 * The [SYS_WRITE] = ... form puts each entry at its number, whatever
 * the order of the lines. Entry 0 is left empty (0): it is not a
 * service. Entry 5 (SYS_START_TASKS) is empty too: svc_dispatch
 * handles it itself, because it does not return to its caller. */
static const syscall_fn table[] = {
    [SYS_WRITE]       = sys_write_impl,
    [SYS_READ]        = sys_read_impl,
    [SYS_UPTIME]      = sys_uptime_impl,
    [SYS_EXIT]        = sys_exit_impl,
    [SYS_YIELD]       = sys_yield_impl,
    [SYS_DELAY]       = sys_delay_impl,
    [SYS_DELAY_UNTIL] = sys_delay_until_impl,
    [SYS_TRACE_DUMP]  = sys_trace_dump_impl,
    [SYS_TICK_COUNT]  = sys_tick_count_impl,
    [SYS_IMAGE_COUNT] = sys_image_count_impl,
    [SYS_IMAGE_NAME]  = sys_image_name_impl,
    [SYS_PROGRAM_RUN]  = sys_program_run_impl,
    [SYS_PROGRAM_WAIT] = sys_program_wait_impl,
};

/* How many entries the table has (here 11: numbers 0..10). */
#define TABLE_SIZE ( sizeof( table ) / sizeof( table[0] ) )

/* EXC_RETURN bit 2: 1 if the caller was on the process stack (a task),
 * 0 if it was on the main stack (main(), the kernel). */
#define EXC_RETURN_PSP_BIT 0x4u

/* frame points at the 8 registers the hardware pushed when `svc` ran:
 *     frame[0] = r0     frame[4] = r12
 *     frame[1] = r1     frame[5] = lr
 *     frame[2] = r2     frame[6] = pc   (the instruction after `svc`)
 *     frame[3] = r3     frame[7] = xPSR
 * exc_return is the EXC_RETURN value of this exception. The function
 * returns the EXC_RETURN value SVC_Handler must use to end it. */
uint32_t svc_dispatch( uint32_t *frame, uint32_t exc_return )
{
    /* Which service? The number is not in a register: it is written
     * inside the `svc #n` instruction itself. That instruction is 2
     * bytes long and sits just before the saved pc (frame[6]), and its
     * first byte in memory holds n. So we look at the byte 2 bytes
     * before the saved pc:
     *     pc - 2:  n       (low byte of `svc #n`)
     *     pc - 1:  0xDF    (high byte: "this is an svc")
     *     pc:      the next instruction  */
    uint8_t n = ( ( uint8_t * ) frame[6] )[-2];

    /* Did a task ask (process stack), or main() (main stack)? */
    int from_task = ( exc_return & EXC_RETURN_PSP_BIT ) != 0;

    /* SYS_START_TASKS: kernel only. A task (on the process stack) that
     * tries it falls through to the table below and gets -1. */
    if ( n == SYS_START_TASKS && !from_task && tcb_current == 0 )
    {
        return task_start_first();      /* enter the first task */
    }

    /* An unknown number, or 0, or an empty table entry: answer -1. */
    if ( n >= TABLE_SIZE || table[n] == 0 )
    {
        frame[0] = ( uint32_t ) -1;
        return exc_return;              /* back to the caller */
    }

    /* Run the service with the caller's r0, r1, r2 as arguments, and
     * store its result in the saved r0. When the exception ends, the
     * hardware pops the frame back into the registers, and the caller
     * finds the result in r0. */
    frame[0] = table[n]( frame[0], frame[1], frame[2] );
    return exc_return;                  /* back to the caller, as it came */
}
