/*
 * Embedded Operating Systems and Architecture course
 * Licensed under the MIT License. See LICENSE in the repository root.
 *
 * Contact: Stefano Di Carlo <stefano.dicarlo@polito.it>
 *
 * MPU set-up: see kernel/mpu.h.
 *
 * Region 0 lets tasks read and run FLASH (code and constants).
 * Region 1 lets a task read and write its own partition, nothing else.
 * Any other access by a task raises a MemManage fault.
 *
 * Progetto, punto E: per un programma caricato le regioni 1 e 2
 * insieme danno il W^X (vedi kernel/mpu.h).
 */

#include <stdint.h>
#include "cmsdk.h"
#include "kernel/mpu.h"

/* Campo SRD del registro RASR (bit 15..8): un bit per sottoregione;
 * il bit n a 1 spegne la sottoregione n, cioè lì la regione non vale. */
#define MPU_RASR_SRD( mask )    ( ( uint32_t ) ( mask ) << 8 )

void mpu_init( void )
{
    /* Region 0 = FLASH, from address 0, 4 MB = 2^22 bytes.
     * Read-only for everyone, and code may run from it (no XN). */
    MPU->RBAR = 0x00000000 | MPU_RBAR_VALID | 0;       /* = 0x00000010 */
    MPU->RASR = MPU_RASR_AP( MPU_AP_RO ) | MPU_RASR_C
              | MPU_RASR_SIZE( 22 ) | MPU_RASR_ENABLE; /* = 0x0602002B */

    /* Switch the MPU on. PRIVDEFENA: the kernel (privileged) keeps the
     * normal memory map, so only tasks are restricted. */
    MPU->CTRL = MPU_CTRL_ENABLE | MPU_CTRL_PRIVDEFENA;  /* = 5 */
    __asm volatile ( "dsb" ::: "memory" );  /* finish the writes ... */
    __asm volatile ( "isb" ::: "memory" );  /* ... before the next instruction */
}

void mpu_set_task_region( uint32_t base, uint32_t size, int executable,
                          uint32_t code_size )
{
    /* The SIZE field holds log2(size) - 1. __builtin_ctz counts the
     * trailing zero bits, which is log2 of a power of two.
     * Example: 4096 = 2^12, so SIZE = 11. */
    uint32_t log2_size = __builtin_ctz( size );

    /* I bit uguali per le regioni 1 e 2: stessi attributi di memoria,
     * stessa dimensione, regione accesa. */
    uint32_t common = MPU_RASR_S | MPU_RASR_C
                    | MPU_RASR_SIZE( log2_size ) | MPU_RASR_ENABLE;

    if ( executable && code_size != 0 )
    {
        /* Programma caricato, protetto W^X.
         *
         * Regione 1 = tutto lo slot, sola lettura, eseguibile (niente
         * XN). Da sola farebbe dello slot una zona tutta "codice". */
        MPU->RBAR = base | MPU_RBAR_VALID | 1;
        MPU->RASR = MPU_RASR_AP( MPU_AP_RO ) | common;

        /* Regione 2 = lo stesso slot, lettura e scrittura, mai
         * eseguibile (XN), spenta sulle sottoregioni del codice.
         * Esempio: slot da 4096 byte e code_size = 1024. Una
         * sottoregione è di 512 byte, quindi il codice ne occupa 2 e
         * la maschera è 0b00000011: la regione 2 vale da 1024 in su
         * (dati, BSS e stack) e vince sulla 1 perché ha il numero più
         * alto; sotto 1024 resta la regione 1. */
        uint32_t code_subregions = code_size / ( size / MPU_SUBREGIONS );
        uint32_t disabled = ( 1u << code_subregions ) - 1u;
        MPU->RBAR = base | MPU_RBAR_VALID | 2;
        MPU->RASR = MPU_RASR_XN | MPU_RASR_AP( MPU_AP_FULL_RW )
                  | MPU_RASR_SRD( disabled ) | common;
    }
    else
    {
        /* Region 1 = the task's partition: read-write for everyone, but
         * never executable (XN) unless the caller asks for the "whole
         * slot" policy. base must be a multiple of size. */
        MPU->RBAR = base | MPU_RBAR_VALID | 1;          /* 4 KB at 0x20000000: 0x20000011 */
        MPU->RASR = ( executable ? 0u : MPU_RASR_XN )
                  | MPU_RASR_AP( MPU_AP_FULL_RW ) | common; /* 4 KB: 0x13060017 */

        /* Regione 2 spenta: potrebbe essere rimasta accesa sullo slot
         * del programma che girava prima. */
        MPU->RBAR = MPU_RBAR_VALID | 2;
        MPU->RASR = 0;
    }
    __asm volatile ( "dsb" ::: "memory" );
    __asm volatile ( "isb" ::: "memory" );
}
