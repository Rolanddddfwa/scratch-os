#!/usr/bin/env python3
"""Impacchetta i programmi in un'immagine IMG1 (assignment, 6.1 e 6.3).

Uso:

    python3 tools/mkimage.py image build/fs.img NOME=FILE.elf [NOME=FILE.elf ...]

Ogni file viene copiato byte per byte, senza modifiche: nella versione
Big è il kernel a controllare il contenuto degli ELF. Lo strumento
controlla solo ciò che serve perché l'immagine sia una IMG1 valida.

Com'è fatta l'immagine (numeri a 32 bit, little-endian):

    byte 0    header: magic "IMG1", versione, dimensione totale,
              numero di voci, offset della directory
    byte 20   directory: una voce da 24 byte per programma
              (nome in 16 byte, offset del file, dimensione del file)
    poi       i file, nell'ordine della riga di comando, ognuno
              allineato a un multiplo di 4 con byte a zero

In caso di errore non scrive nulla, stampa "mkimage.py: error: ..."
sullo standard error ed esce con stato 2.
"""

import argparse
import os
import re
import struct
import sys

MAGIC = 0x31474D49              # i byte 'I' 'M' 'G' '1'
VERSION = 1
HEADER_SIZE = 20                # byte dell'header
ENTRY_SIZE = 24                 # byte di una voce di directory
MAX_PROGRAMS = 32
MAX_NAME = 15                   # caratteri di un nome, senza il NUL
MIN_FILE_SIZE = 40              # dimensione minima di un file
MAX_IMAGE_SIZE = 0x00200000     # 2 MiB: lo spazio riservato in FLASH

NAME_RE = re.compile(r"[A-Za-z0-9_-]{1,%d}" % MAX_NAME)


def align4(n):
    """Arrotonda n al multiplo di 4 successivo."""
    return (n + 3) & ~3


def read_programs(specs, fail):
    """Controlla gli argomenti NOME=FILE e legge i file.

    Ritorna una lista di coppie (nome, contenuto), nell'ordine dato.
    `fail(messaggio)` stampa l'errore ed esce: non ritorna.
    """
    if len(specs) > MAX_PROGRAMS:
        fail(f"too many programs: {len(specs)} (at most {MAX_PROGRAMS})")

    programs = []
    seen = set()
    for spec in specs:
        name, sep, path = spec.partition("=")
        if sep == "" or path == "":
            fail(f"expected NAME=PROGRAM.elf, got '{spec}'")
        if NAME_RE.fullmatch(name) is None:
            fail(f"invalid program name '{name}': use 1 to {MAX_NAME} "
                 "characters from A-Z a-z 0-9 _ -")
        if name in seen:
            fail(f"duplicate program name '{name}'")
        seen.add(name)

        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError as e:
            fail(f"cannot read '{path}': {e.strerror}")
        if len(data) < MIN_FILE_SIZE:
            fail(f"'{path}' is too small: {len(data)} bytes "
                 f"(at least {MIN_FILE_SIZE})")
        programs.append((name, data))
    return programs


def build_image(programs, fail):
    """Ritorna i byte dell'immagine IMG1 per la lista (nome, contenuto)."""
    count = len(programs)
    directory_offset = HEADER_SIZE
    offset = align4(directory_offset + count * ENTRY_SIZE)  # contents_start

    directory = bytearray()
    contents = bytearray()
    start = offset                          # dove comincia `contents`
    for name, data in programs:
        # Zeri di riempimento fino al multiplo di 4, poi il file intatto.
        contents += bytes(offset - start - len(contents))
        # "16s" riempie con byte a zero il resto del campo nome.
        directory += struct.pack("<16sII", name.encode("ascii"), offset, len(data))
        contents += data
        offset = align4(offset + len(data))

    # La dimensione è la fine dell'ultimo file, senza riempimento finale.
    image_size = start + len(contents)
    if image_size > MAX_IMAGE_SIZE:
        fail(f"image too large: {image_size} bytes (at most {MAX_IMAGE_SIZE})")

    header = struct.pack("<IIIII", MAGIC, VERSION, image_size, count, directory_offset)
    return bytes(header + directory + contents)


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="mkimage.py",
        usage="mkimage.py image OUTPUT NAME=PROGRAM.elf [NAME=PROGRAM.elf ...]",
        description="Pack program files into an IMG1 image.")
    commands = parser.add_subparsers(dest="command", required=True, metavar="image")
    image = commands.add_parser("image", prog=parser.prog, usage=parser.usage,
                                help="build an IMG1 image")
    image.add_argument("output", metavar="OUTPUT", help="image file to write")
    # REMAINDER: tutto ciò che segue OUTPUT è un programma, anche se
    # comincia con '-' (un nome come "-a" è ammesso dal formato, e
    # argparse lo scambierebbe per un'opzione).
    image.add_argument("programs", metavar="NAME=PROGRAM.elf",
                       nargs=argparse.REMAINDER,
                       help="program name and the file to store under it")
    args = parser.parse_args(argv)
    if not args.programs:
        parser.error("the following arguments are required: NAME=PROGRAM.elf")

    # Prima tutti i controlli, poi una sola scrittura: se qualcosa non
    # va, il file di uscita non viene toccato.
    programs = read_programs(args.programs, parser.error)
    data = build_image(programs, parser.error)

    directory = os.path.dirname(args.output)
    try:
        if directory:
            os.makedirs(directory, exist_ok=True)
        with open(args.output, "wb") as f:
            f.write(data)
    except OSError as e:
        parser.error(f"cannot write '{args.output}': {e.strerror}")

    print(f"IMG1: {args.output} ({len(data)} bytes, {len(programs)} programs)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
