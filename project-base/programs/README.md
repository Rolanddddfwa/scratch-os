# Programmi caricabili

Ogni file `NOME.c` di questa cartella diventa un programma a sé,
`build/programs/NOME.elf`, che la shell avvia con `run NOME`. I programmi
non sono collegati al kernel: ognuno porta con sé il proprio codice di
avvio, gli stub delle system call e `printf`.

## Comandi

Dalla cartella del progetto:

| Comando | Cosa fa |
|---|---|
| `make` | Compila il kernel, tutti i programmi e `build/fs.img` |
| `make programs` | Compila solo i programmi |
| `make image` | Rifà sempre `build/fs.img` dai programmi |
| `make run` | Compila ciò che manca e avvia QEMU |

L'immagine si costruisce con il comando fissato dall'assignment:

```
python3 tools/mkimage.py image build/fs.img NOME=FILE.elf [NOME=FILE.elf ...]
```

Il `Makefile` passa tutti i programmi di questa cartella in ordine
alfabetico. Per un elenco diverso (per esempio con file già pronti):

```
make image IMAGE_ARGS="uno=build/programs/print_plain.elf due=altro.elf"
```

## Aggiungere un programma

Crea `programs/NOME.c` con una funzione `main`:

```c
#include "lib/printf.h"

int main( void )
{
    printf( "ciao\n" );
    return 0;
}
```

Regole:

- Il nome del file è il nome del programma: da 1 a 15 caratteri tra
  `A-Z a-z 0-9 _ -`.
- Un programma è un solo file `.c`.
- Sono disponibili `lib/printf.h` e `lib/syscalls.h`. Non c'è la
  libreria C e non c'è `malloc`.
- Codice, dati e BSS devono stare in 3072 byte. Lo stack è di 1024 byte.
- Ritornare da `main` equivale a chiamare `sys_exit()`.

A ogni build `tools/checkelf.py` stampa una riga per programma:

```
checkelf: build/programs/thumb_ptr.elf: codice 580, dati 4, bss 0, fine 1028/3072, 5 parole da rilocare
```

Se compare una riga `ATTENZIONE`, il kernel rifiuterà quel programma.
La build non si ferma: decidere se un programma si può caricare spetta
al kernel.

## Cosa c'è in `rt/`

| File | Contenuto |
|---|---|
| `rt/crt0.s` | `_start`: chiama `main`, poi esegue la SVC 4 |
| `rt/program.ld` | Linker script: indirizzo 0, codice e costanti, poi dati e BSS |
| `rt/mem.c` | `memcpy` e `memset`, che il compilatore può chiamare da solo |

## Com'è fatto un ELF di programma

È un eseguibile ARM a 32 bit (`ET_EXEC`), collegato all'indirizzo 0 con
`--emit-relocs`, con il punto di ingresso Thumb (`_start`, indirizzo 0,
bit 0 a 1).

| Segmento | Indirizzo | Contenuto | Permessi |
|---|---|---|---|
| primo `PT_LOAD` | 0 | `.text`, `.rodata` | lettura, esecuzione |
| secondo `PT_LOAD` | multiplo di 512, almeno 1024 | `.data`, `.bss` | lettura, scrittura |

Il secondo segmento manca se il programma non ha variabili globali. Il
BSS è la parte del secondo segmento oltre `p_filesz`: non è nel file e
il loader la azzera.

I dati cominciano a un multiplo di 512 perché la MPU divide lo slot da
4 KiB in 8 sottoregioni da 512 byte: così il loader può dare al codice
"esegui ma non scrivere" e ai dati "scrivi ma non eseguire".

Le rilocazioni delle sezioni caricate sono solo di tre tipi:

| Tipo | Numero | Cosa fa il loader |
|---|---|---|
| `R_ARM_ABS32` | 2 | Somma la base dello slot alla parola |
| `R_ARM_THM_CALL` | 10 | Nulla: il salto è relativo |
| `R_ARM_THM_JUMP24` | 30 | Nulla: il salto è relativo |

Le sezioni di debug hanno altre rilocazioni, che il kernel ignora.

## Guardare dentro un programma

```
arm-none-eabi-readelf -h -l build/programs/thumb_ptr.elf    # header e segmenti
arm-none-eabi-readelf -S -r build/programs/thumb_ptr.elf    # sezioni e rilocazioni
arm-none-eabi-objdump -d build/programs/thumb_ptr.elf       # codice
arm-none-eabi-size build/programs/*.elf                     # dimensioni
```

Per il debugger, dopo che il programma è stato caricato nello slot 0:

```
add-symbol-file build/programs/thumb_ptr.elf 0x20002000
```
