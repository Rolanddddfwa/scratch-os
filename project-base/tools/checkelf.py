#!/usr/bin/env python3
"""Controlla che un programma ELF rispetti le regole dell'assignment (6.3).

Uso:

    python3 tools/checkelf.py build/programs/NOME.elf [ALTRO.elf ...]

Per ogni file stampa una riga di riepilogo e, se trova qualcosa che il
kernel rifiuterebbe, una riga "ATTENZIONE" per ogni problema. È solo un
aiuto durante la build: non modifica il file e non ferma `make`, perché
decidere se un programma si può caricare spetta al kernel.

Legge l'ELF con il solo modulo struct. Le strutture sono quelle
standard a 32 bit: header da 52 byte, program header da 32, section
header da 40, simboli da 16, rilocazioni REL da 8.
"""

import struct
import sys

PROGRAM_LIMIT = 3072            # byte di slot per codice + dati + BSS

PT_LOAD, PT_DYNAMIC = 1, 2
SHT_SYMTAB, SHT_RELA, SHT_DYNAMIC, SHT_NOBITS, SHT_REL = 2, 4, 6, 8, 9
SHF_ALLOC = 0x2
PF_X = 0x1

R_ARM_ABS32, R_ARM_THM_CALL, R_ARM_THM_JUMP24 = 2, 10, 30
BRANCHES = (R_ARM_THM_CALL, R_ARM_THM_JUMP24)


def check(path):
    """Ritorna (riepilogo, lista dei problemi) per il file `path`."""
    data = open(path, "rb").read()
    problems = []

    # --- Header del file (52 byte) ---------------------------------
    if len(data) < 52 or data[:4] != b"\x7fELF":
        return "non è un file ELF", ["manca il magic ELF"]
    ei_class, ei_data, ei_version = data[4], data[5], data[6]
    (e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, _flags, e_ehsize,
     e_phentsize, e_phnum, e_shentsize, e_shnum, _shstrndx) = struct.unpack_from(
        "<HHIIIIIHHHHHH", data, 16)
    if (ei_class, ei_data, ei_version) != (1, 1, 1):
        problems.append("non è un ELF a 32 bit, little-endian, versione 1")
    if e_type != 2 or e_machine != 40 or e_version != 1:
        problems.append("non è un eseguibile (ET_EXEC) per ARM")
    if (e_ehsize, e_phentsize, e_shentsize) != (52, 32, 40):
        problems.append("dimensioni dei record non standard")

    # --- Segmenti: che cosa va caricato in memoria ------------------
    loads = []
    for i in range(e_phnum):
        p_type, _off, p_vaddr, _pa, p_filesz, p_memsz, p_flags, _al = struct.unpack_from(
            "<IIIIIIII", data, e_phoff + i * 32)
        if p_type == PT_LOAD:
            loads.append((p_vaddr, p_filesz, p_memsz, p_flags))
        if p_type == PT_DYNAMIC:
            problems.append("contiene collegamento dinamico (PT_DYNAMIC)")
    if not loads:
        return "nessun segmento PT_LOAD", problems + ["manca un segmento PT_LOAD"]
    if min(v for v, _, _, _ in loads) != 0:
        problems.append("il programma non è collegato all'indirizzo 0")
    end = max(v + m for v, _, m, _ in loads)
    if end > PROGRAM_LIMIT:
        problems.append(f"occupa {end} byte: il limite è {PROGRAM_LIMIT}")
    if e_entry & 1 == 0:
        problems.append("l'ingresso non è un indirizzo Thumb (bit 0 a zero)")
    if not any(f & PF_X and v <= (e_entry & ~1) < v + s for v, s, _, f in loads):
        problems.append("l'ingresso non cade dentro il codice")

    # --- Sezioni -----------------------------------------------------
    sections = [struct.unpack_from("<IIIIIIIIII", data, e_shoff + i * 40)
                for i in range(e_shnum)]
    # Campi usati: [1] tipo, [2] flag, [3] indirizzo, [4] offset nel
    # file, [5] dimensione, [6] link, [7] info.
    symbols = []
    for s in sections:
        if s[1] == SHT_SYMTAB:
            symbols = [struct.unpack_from("<IIIBBH", data, s[4] + k * 16)
                       for k in range(s[5] // 16)]
        if s[1] == SHT_RELA:
            problems.append("contiene rilocazioni RELA")
        if s[1] == SHT_DYNAMIC:
            problems.append("contiene collegamento dinamico (.dynamic)")
    # Ogni sezione caricata deve stare in un segmento: altrimenti il
    # loader non la copia e non la azzera (succede al BSS se il linker
    # script lo lascia fuori dai segmenti).
    for s in sections:
        if s[2] & SHF_ALLOC and s[5] != 0 and not any(
                v <= s[3] and s[3] + s[5] <= v + m for v, _, m, _ in loads):
            problems.append(f"una sezione caricata (indirizzo 0x{s[3]:x}, {s[5]} byte) "
                            "è fuori da tutti i segmenti")
    # Il simbolo 0 è sempre vuoto; negli altri, sezione 0 = non definito.
    if any(sym[5] == 0 for sym in symbols[1:]):
        problems.append("contiene simboli non definiti")

    # --- Rilocazioni delle sezioni caricate -------------------------
    abs32 = 0
    for s in sections:
        if s[1] != SHT_REL or s[7] >= len(sections):
            continue
        target = sections[s[7]]             # sh_info: la sezione da correggere
        if not target[2] & SHF_ALLOC:
            continue                        # debug e simili: il kernel le ignora
        for k in range(s[5] // 8):
            r_offset, r_info = struct.unpack_from("<II", data, s[4] + k * 8)
            r_type, r_sym = r_info & 0xFF, r_info >> 8
            inside = target[3] <= r_offset and r_offset + 4 <= target[3] + target[5]
            if r_type == R_ARM_ABS32:
                abs32 += 1
                if r_offset % 4 or not inside or target[1] == SHT_NOBITS:
                    problems.append(f"rilocazione ABS32 in un punto non valido: 0x{r_offset:x}")
            elif r_type in BRANCHES:
                if r_offset % 2 or not inside:
                    problems.append(f"salto Thumb in un punto non valido: 0x{r_offset:x}")
            else:
                problems.append(f"tipo di rilocazione non ammesso: {r_type} a 0x{r_offset:x}")
            if r_sym >= len(symbols) or (r_sym != 0 and symbols[r_sym][5] == 0):
                problems.append(f"rilocazione verso un simbolo non definito a 0x{r_offset:x}")

    code = sum(m for _, _, m, f in loads if f & PF_X)
    data_size = sum(s for _, s, _, f in loads if not f & PF_X)
    bss = sum(m - s for _, s, m, _ in loads)
    summary = (f"codice {code}, dati {data_size}, bss {bss}, "
               f"fine {end}/{PROGRAM_LIMIT}, {abs32} parole da rilocare")
    return summary, problems


def main():
    if len(sys.argv) < 2:
        print("uso: checkelf.py FILE.elf [FILE.elf ...]", file=sys.stderr)
        return 2
    for path in sys.argv[1:]:
        try:
            summary, problems = check(path)
        except (OSError, struct.error):
            summary, problems = "non leggibile", ["file troncato o illeggibile"]
        print(f"checkelf: {path}: {summary}")
        for p in problems:
            print(f"checkelf: {path}: ATTENZIONE: {p}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
