#!/usr/bin/env python3
"""Costruisce un'immagine IMG1 finta per provare la shell e il kernel.

Serve finché non c'è il vero tools/mkimage.py: i "programmi" dentro
sono solo byte di riempimento, non ELF. Bastano per provare `ls` e i
controlli del kernel sull'immagine (assignment, sezione 6.1).

Uso, dalla cartella del progetto:

    python3 tools/fake_image.py                  immagine valida
    python3 tools/fake_image.py alfa beta        valida, con questi nomi
    python3 tools/fake_image.py --break magic    immagine rotta in un punto
    python3 tools/fake_image.py --list           elenco dei guasti possibili

Poi `make run`. Con `make clean` l'immagine sparisce e torna quella
segnaposto della base.
"""

import argparse
import struct
import sys

HEADER_SIZE = 20        # byte dell'header
ENTRY_SIZE = 24         # byte di una voce di directory
MAGIC = 0x31474D49      # i byte 'I' 'M' 'G' '1'


def build_image(names, file_size=48):
    """Ritorna i byte di un'immagine valida, un file finto per nome."""
    count = len(names)
    dir_offset = HEADER_SIZE
    offset = dir_offset + count * ENTRY_SIZE    # i file partono dopo la directory

    directory = b""
    contents = b""
    for i, name in enumerate(names):
        # voce: nome (16 byte, riempito di zeri), offset, dimensione
        directory += struct.pack("<16sII", name.encode("ascii"), offset, file_size)
        contents += bytes([0xA0 + i]) * file_size
        offset += file_size                     # file_size è multiplo di 4

    image_size = offset
    header = struct.pack("<IIIII", MAGIC, 1, image_size, count, dir_offset)
    return header + directory + contents


def put32(img, pos, value):
    """Scrive un numero a 32 bit little-endian alla posizione pos."""
    img[pos:pos + 4] = struct.pack("<I", value)


def entry(i):
    """Posizione della voce di directory numero i (directory al byte 20)."""
    return HEADER_SIZE + i * ENTRY_SIZE


# Ogni guasto parte dall'immagine valida (almeno 2 programmi da 48 byte)
# e ne cambia un solo punto. Il kernel deve rifiutarli tutti.
def break_magic(img):         put32(img, 0, 0x32474D49)          # "IMG2"
def break_version(img):       put32(img, 4, 2)
def break_size_small(img):    put32(img, 8, 19)
def break_size_big(img):      put32(img, 8, 0x00200001)
def break_count_zero(img):    put32(img, 12, 0)
def break_count_33(img):      put32(img, 12, 33)
def break_dir_low(img):       put32(img, 16, 16)                 # dentro l'header
def break_dir_odd(img):       put32(img, 16, 22)                 # non multiplo di 4
def break_dir_outside(img):   put32(img, 16, len(img))           # oltre la fine
def break_name_empty(img):    img[entry(0)] = 0
def break_name_char(img):     img[entry(0)] = ord(".")           # carattere vietato
def break_name_no_nul(img):   img[entry(0):entry(0) + 16] = b"a" * 16
def break_name_dup(img):      img[entry(1):entry(1) + 16] = img[entry(0):entry(0) + 16]
def break_offset_odd(img):    put32(img, entry(1) + 16, len(img) - 46)
def break_offset_in_dir(img): put32(img, entry(0) + 16, HEADER_SIZE)
def break_file_small(img):    put32(img, entry(0) + 20, 39)
def break_file_outside(img):  put32(img, entry(1) + 20, 0xFFFFFFF0)
def break_overlap(img):       put32(img, entry(1) + 16, struct.unpack_from("<I", img, entry(0) + 16)[0])


BREAKS = {name[len("break_"):]: func
          for name, func in sorted(globals().items())
          if name.startswith("break_")}


def main():
    parser = argparse.ArgumentParser(description="Costruisce un'immagine IMG1 finta.")
    parser.add_argument("names", nargs="*", default=["alfa", "beta", "gamma"],
                        help="nomi dei programmi finti (predefiniti: alfa beta gamma)")
    parser.add_argument("--output", default="build/fs.img", help="file da scrivere")
    parser.add_argument("--break", dest="broken", metavar="GUASTO",
                        help="rompe l'immagine in un punto (vedi --list)")
    parser.add_argument("--list", action="store_true", help="elenca i guasti ed esce")
    args = parser.parse_args()

    if args.list:
        print("\n".join(BREAKS))
        return 0
    if args.broken is not None and args.broken not in BREAKS:
        print(f"fake_image: guasto sconosciuto: {args.broken} (vedi --list)", file=sys.stderr)
        return 2
    if args.broken is not None and len(args.names) < 2:
        print("fake_image: per --break servono almeno 2 nomi", file=sys.stderr)
        return 2

    img = bytearray(build_image(args.names))
    if args.broken is not None:
        BREAKS[args.broken](img)

    with open(args.output, "wb") as f:
        f.write(img)
    stato = f"rotta ({args.broken})" if args.broken else "valida"
    print(f"{args.output}: {len(img)} byte, {len(args.names)} programmi, {stato}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
