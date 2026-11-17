/*
 * Embedded Operating Systems and Architecture course
 * Licensed under the MIT License. See LICENSE in the repository root.
 *
 * Contact: Stefano Di Carlo <stefano.dicarlo@polito.it>
 *
 * The MPU (Memory Protection Unit): the hardware that decides which
 * addresses unprivileged code may touch. The kernel uses two regions:
 *   region 0: all of FLASH, read-only for tasks (code and constants);
 *   region 1: the partition of the task that runs now, read-write.
 * Everything else is off limits to tasks. The kernel (privileged) keeps
 * the normal full memory map.
 *
 * Progetto, punto E (W^X). Per un programma caricato si usa anche la
 * regione 2, sulla stessa partizione:
 *   regione 1: tutto lo slot, sola lettura ed esecuzione (il codice);
 *   regione 2: tutto lo slot, lettura e scrittura senza esecuzione, ma
 *              spenta sulle sottoregioni del codice (dati e stack).
 * Dove due regioni si sovrappongono vince quella con il numero più
 * alto: su dati e stack vale la 2, sul codice resta la 1.
 */
#ifndef MPU_H
#define MPU_H

#include <stdint.h>

/* Set up region 0 (FLASH) and switch the MPU on. Called once by main(). */
void mpu_init( void );

/* La MPU divide ogni regione (di almeno 256 byte) in 8 sottoregioni
 * uguali, che si possono spegnere una per una. Per uno slot da 4096
 * byte una sottoregione è di 512 byte. */
#define MPU_SUBREGIONS 8u

/* Point region 1 at the partition [base, base + size).
 * size must be a power of two (at least 32), and base a multiple of
 * size. Called by the kernel just before it starts a task.
 *
 * code_size (progetto, punto E): quanti byte, dall'inizio della
 * partizione, sono codice. Con executable diverso da 0 e code_size
 * diverso da 0 la partizione è protetta W^X: i primi code_size byte si
 * leggono e si eseguono ma non si scrivono; il resto si legge e si
 * scrive ma non si esegue. code_size deve essere un multiplo di
 * size / MPU_SUBREGIONS. Con code_size = 0 tutto resta come nella
 * base. */
void mpu_set_task_region( uint32_t base, uint32_t size, int executable,
                          uint32_t code_size );

#endif /* MPU_H */
