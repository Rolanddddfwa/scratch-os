/*
 * Lettura dell'immagine dei programmi (formato IMG1, assignment 6.1).
 *
 * QEMU carica build/fs.img in FLASH all'indirizzo 0x00200000. Il kernel
 * la legge lì dove si trova: non la copia in RAM e non la modifica.
 *
 * Tutte le funzioni controllano prima l'intera immagine. Se una sola
 * regola del formato è violata, l'immagine non è valida e ogni
 * funzione risponde con un errore.
 */
#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

/* Lunghezza massima di un nome di programma, senza il NUL finale. */
#define IMAGE_NAME_MAX 15

/* Numero di programmi nell'immagine (1..32), oppure -1 se l'immagine
 * non è valida. */
int image_count( void );

/* Nome del programma numero `index` (0 = primo della directory).
 * Il puntatore indica i byte dentro l'immagine in FLASH: la stringa
 * finisce con NUL e ha da 1 a 15 caratteri.
 * Ritorna 0 se l'immagine o l'indice non sono validi. */
const char *image_name( unsigned index );

/* Cerca il programma chiamato `name` (stringa del kernel, con NUL).
 * Se lo trova ritorna 0 e scrive in *file l'indirizzo del suo primo
 * byte dentro l'immagine e in *size la sua dimensione in byte.
 * Ritorna -1 se l'immagine non è valida o il nome non esiste: per
 * distinguere i due casi, chiamare prima image_count(). */
int image_find( const char *name, const uint8_t **file, uint32_t *size );

#endif /* IMAGE_H */
