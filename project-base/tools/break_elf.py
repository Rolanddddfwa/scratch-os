#!/usr/bin/env python3
"""Rompe un programma ELF valido in un punto preciso.

Serve per provare che il kernel rifiuta i file sbagliati: ogni guasto
parte da un ELF buono e ne cambia una sola cosa.

Uso, dalla cartella del progetto:

    python3 tools/break_elf.py --list
    python3 tools/break_elf.py build/programs/thumb_ptr.elf bad_magic build/broken/bad_magic.elf
    python3 tools/break_elf.py build/programs/thumb_ptr.elf --all build/broken

Con --all scrive un file GUASTO.elf per ogni guasto nella cartella
indicata. L'ELF di partenza deve avere almeno una rilocazione
R_ARM_ABS32 nel codice (tutti i programmi che usano printf ne hanno).
"""

import argparse
import os
import struct
import sys

PT_LOAD = 1
SHT_RELA, SHT_REL = 4, 9
SHF_ALLOC = 2
R_ARM_ABS32 = 2


def u16(d, p): return struct.unpack_from("<H", d, p)[0]
def u32(d, p): return struct.unpack_from("<I", d, p)[0]
def put16(d, p, v): struct.pack_into("<H", d, p, v)
def put32(d, p, v): struct.pack_into("<I", d, p, v)


class Elf:
    """Le posizioni che servono per rompere il file."""

    def __init__(self, d):
        self.phoff, self.shoff = u32(d, 28), u32(d, 32)
        self.phnum, self.shnum = u16(d, 44), u16(d, 48)
        # Program header dei segmenti PT_LOAD non vuoti.
        self.loads = [self.phoff + 32 * i for i in range(self.phnum)
                      if u32(d, self.phoff + 32 * i) == PT_LOAD
                      and u32(d, self.phoff + 32 * i + 20) != 0]
        # Section header delle sezioni REL che riguardano contenuto caricato.
        self.loaded_rels = []
        self.debug_rels = []
        for i in range(self.shnum):
            sh = self.shoff + 40 * i
            if u32(d, sh + 4) != SHT_REL:
                continue
            target = self.shoff + 40 * u32(d, sh + 28)
            (self.loaded_rels if u32(d, target + 8) & SHF_ALLOC else self.debug_rels).append(sh)
        # Il primo record R_ARM_ABS32 del contenuto caricato.
        self.abs32 = None
        for sh in self.loaded_rels:
            for k in range(u32(d, sh + 20) // 8):
                rec = u32(d, sh + 16) + 8 * k
                if u32(d, rec + 4) & 0xFF == R_ARM_ABS32:
                    self.abs32 = (sh, rec)
                    break
            if self.abs32:
                break
        if self.abs32 is None:
            raise ValueError("l'ELF non ha rilocazioni R_ARM_ABS32 nel contenuto caricato")

    def site_offset(self, d):
        """Posizione nel file della parola indicata dal primo ABS32."""
        sh, rec = self.abs32
        target = self.shoff + 40 * u32(d, sh + 28)
        return u32(d, target + 16) + (u32(d, rec) - u32(d, target + 12))

    def symbol_offset(self, d):
        """Posizione nel file del simbolo usato dal primo ABS32."""
        sh, rec = self.abs32
        symtab = self.shoff + 40 * u32(d, sh + 24)
        return u32(d, symtab + 16) + 16 * (u32(d, rec + 4) >> 8)


# Ogni funzione riceve i byte (modificabili) e le posizioni, e ritorna
# i byte del file rotto. Il nome dopo "break_" è il nome del guasto.

def break_bad_magic(d, e):
    """il primo byte non è 0x7f"""
    d[0] = 0x7E
    return d

def break_bad_header(d, e):
    """dimensione dei program header dichiarata 31 invece di 32"""
    put16(d, 42, 31)
    return d

def break_not_arm(d, e):
    """macchina diversa da ARM"""
    put16(d, 18, 3)
    return d

def break_tables_outside(d, e):
    """la tabella delle sezioni comincia oltre la fine del file"""
    put32(d, 32, len(d))
    return d

def break_truncated_elf(d, e):
    """file tagliato a metà"""
    return d[:len(d) // 2]

def break_truncated_header(d, e):
    """file tagliato a 40 byte: nemmeno l'header è intero"""
    return d[:40]

def break_too_large(d, e):
    """l'ultimo segmento finisce a 3076 byte"""
    ph = e.loads[-1]
    put32(d, ph + 20, 3076 - u32(d, ph + 8))
    return d

def break_segment_outside_file(d, e):
    """un segmento dichiara più byte di quanti ne ha il file"""
    put32(d, e.loads[0] + 4, len(d) - 4)
    return d

def break_segment_rwx(d, e):
    """il segmento del codice è anche scrivibile"""
    put32(d, e.loads[0] + 24, 7)
    return d

def break_entry_not_thumb(d, e):
    """ingresso con il bit 0 a zero"""
    put32(d, 24, u32(d, 24) & ~1)
    return d

def break_entry_outside(d, e):
    """ingresso fuori dal codice"""
    put32(d, 24, 0x801)
    return d

def break_bad_reloc(d, e):
    """una rilocazione punta fuori dal programma"""
    put32(d, e.abs32[1], 0x00100000)
    return d

def break_reloc_unaligned(d, e):
    """una rilocazione ABS32 punta a un indirizzo non multiplo di 4"""
    put32(d, e.abs32[1], u32(d, e.abs32[1]) + 2)
    return d

def break_reloc_overflow(d, e):
    """la parola da rilocare, sommata alla base, supera i 32 bit"""
    put32(d, e.site_offset(d), 0xFFFFFFF0)
    return d

def break_reloc_duplicate(d, e):
    """due rilocazioni ABS32 indicano la stessa parola"""
    sh, first = e.abs32
    records = [u32(d, sh + 16) + 8 * k for k in range(u32(d, sh + 20) // 8)]
    second = next(r for r in records if r != first and u32(d, r + 4) & 0xFF == R_ARM_ABS32)
    put32(d, second, u32(d, first))
    return d

def break_unsupported_reloc(d, e):
    """una rilocazione di tipo 3 (R_ARM_REL32)"""
    rec = e.abs32[1]
    put32(d, rec + 4, (u32(d, rec + 4) & ~0xFF) | 3)
    return d

def break_undefined_symbol(d, e):
    """una rilocazione usa un simbolo non definito"""
    put16(d, e.symbol_offset(d) + 14, 0)
    return d

def break_rela_section(d, e):
    """una sezione di rilocazioni dichiarata RELA"""
    put32(d, (e.debug_rels or e.loaded_rels)[0] + 4, SHT_RELA)
    return d

def break_reloc_table_outside(d, e):
    """un elenco di rilocazioni esce dal file"""
    put32(d, e.loaded_rels[0] + 20, len(d))
    return d


BREAKS = {name[len("break_"):]: func for name, func in globals().items()
          if name.startswith("break_")}


def make(source, kind):
    data = bytearray(source)
    return bytes(BREAKS[kind](data, Elf(data)))


def main():
    parser = argparse.ArgumentParser(description="Rompe un ELF valido in un punto preciso.")
    parser.add_argument("elf", nargs="?", help="ELF valido di partenza")
    parser.add_argument("kind", nargs="?", help="guasto da applicare (vedi --list)")
    parser.add_argument("output", nargs="?", help="file da scrivere")
    parser.add_argument("--all", metavar="CARTELLA", help="scrive tutti i guasti in CARTELLA")
    parser.add_argument("--list", action="store_true", help="elenca i guasti ed esce")
    args = parser.parse_args()

    if args.list:
        for kind, func in BREAKS.items():
            print(f"{kind:22s} {func.__doc__}")
        return 0
    if args.elf is None or (args.all is None and (args.kind is None or args.output is None)):
        parser.error("servono ELF GUASTO USCITA, oppure ELF --all CARTELLA")
    if args.all is None and args.kind not in BREAKS:
        parser.error(f"guasto sconosciuto: {args.kind} (vedi --list)")

    try:
        with open(args.elf, "rb") as f:
            source = f.read()
        jobs = ([(k, os.path.join(args.all, k + ".elf")) for k in BREAKS]
                if args.all else [(args.kind, args.output)])
        outputs = [(path, make(source, kind)) for kind, path in jobs]
    except (OSError, ValueError, struct.error, IndexError, StopIteration) as e:
        parser.error(f"non riesco a usare '{args.elf}': {e}")

    for path, data in outputs:
        if os.path.dirname(path):
            os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
    print(f"break_elf: scritti {len(outputs)} file")
    return 0


if __name__ == "__main__":
    sys.exit(main())
