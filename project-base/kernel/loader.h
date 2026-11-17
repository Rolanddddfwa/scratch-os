/*
 * Il loader: carica un programma dall'immagine in uno slot di RAM e lo
 * fa partire come task (assignment R6, sezioni 6.3 e 6.4).
 */
#ifndef LOADER_H
#define LOADER_H

/* Carica e avvia il programma chiamato `name` (stringa del kernel,
 * chiusa da NUL: l'ha già controllata e copiata la SVC 13).
 * Ritorna l'id del nuovo task (un numero >= 0), oppure:
 *   -1  l'immagine non è valida
 *   -2  nessun programma con quel nome
 *   -3  nessuno slot libero
 *   -4  il file del programma non è valido
 * In caso di errore nessuno slot viene modificato. */
int loader_run( const char *name );

#endif /* LOADER_H */
