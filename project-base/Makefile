# Embedded Operating Systems and Architecture course
# Licensed under the MIT License. See LICENSE in the repository root.
#
# Contact: Stefano Di Carlo <stefano.dicarlo@polito.it>

# ARM bare-metal cross-compiler: runs on the host, emits Cortex-M code.
CC := arm-none-eabi-gcc

# The time slice of the round-robin scheduler, in ticks (one tick =
# 10 ms). Change it from the command line: `make QUANTUM=2 run`.
QUANTUM ?= 5

# All generated files go under build/; `make clean` just deletes it.
BUILD_DIR := build
# Final linked image. .axf is ARM's name for an ELF carrying debug info.
BIN := $(BUILD_DIR)/scratch-os.axf
IMAGE := $(BUILD_DIR)/fs.img
IMAGE_ADDR := 0x00200000
IMAGE_TOOL ?= tools/mkimage.py
PROGRAM_DIR ?= programs

# --- Programmi caricabili (progetto, punto A) ------------------------
# Ogni file programs/NOME.c diventa un programma a sé,
# build/programs/NOME.elf, collegato all'indirizzo 0 e senza nulla in
# comune con il kernel. I nomi sono ordinati, così l'immagine esce
# sempre uguale.
PROGRAM_BUILD := $(BUILD_DIR)/programs
PROGRAM_RT    := $(PROGRAM_DIR)/rt
PROGRAM_LD    := $(PROGRAM_RT)/program.ld
PROGRAM_CHECK := tools/checkelf.py
PROGRAM_NAMES := $(sort $(basename $(notdir $(wildcard $(PROGRAM_DIR)/*.c))))
PROGRAM_ELFS  := $(PROGRAM_NAMES:%=$(PROGRAM_BUILD)/%.elf)
# Gli argomenti di mkimage.py: NOME=FILE.elf per ogni programma.
IMAGE_ARGS    := $(foreach n,$(PROGRAM_NAMES),$(n)=$(PROGRAM_BUILD)/$(n).elf)

# Collegati dentro ogni programma: avvio (_start), funzioni di aiuto
# del compilatore, stub delle system call e printf. Sono gli stessi
# sorgenti di lib/ usati dal kernel, ma compilati a parte.
PROGRAM_RT_OBJECTS := $(PROGRAM_BUILD)/obj/rt/crt0.o $(PROGRAM_BUILD)/obj/rt/mem.o \
                      $(PROGRAM_BUILD)/obj/lib/syscalls.o \
                      $(PROGRAM_BUILD)/obj/lib/printf.o $(PROGRAM_BUILD)/obj/lib/format.o
PROGRAM_OBJECTS    := $(PROGRAM_NAMES:%=$(PROGRAM_BUILD)/obj/%.o)

# The folders that hold our source files, one folder per OS layer.
SRC_DIRS := boot kernel drivers lib src
# Find every .c file (C code) and every .s file (assembly) in those
# folders. `wildcard` is make's way of listing files that match a pattern,
# so a new file is built automatically, with no change to this Makefile.
SOURCES := $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.c) $(wildcard $(d)/*.s))
# Map each foo.c or foo.s to its build/foo.o object file. The object
# keeps the folder name, e.g. kernel/irq.s becomes build/kernel/irq.o.
# Two nested patsubst calls: the inner one rewrites ".s" sources, the
# outer one rewrites ".c" sources (a word that does not match is left
# alone, so each source is changed by the one that applies to it).
OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(patsubst %.s,$(BUILD_DIR)/%.o,$(SOURCES)))

# -mthumb / -mcpu: target the Cortex-M3's Thumb instruction set.
# -nostartfiles: no default C startup code - boot/startup.s is ours.
# -ffreestanding: no hosted environment - no full standard library, no
#  special meaning for main. Together with -nostdlib below, this makes
#  "no C library" true of the image.
# -std=gnu17: C17 plus GNU extensions (used by the inline-asm blocks).
CFLAGS := -mthumb -mcpu=cortex-m3 -nostartfiles -ffreestanding -std=gnu17
# -Iinclude: look for #include "..." headers in the include/ folder,
# so every file can write #include "cmsdk.h" whatever its own folder.
CFLAGS += -Iinclude
# -I.: also look in the project folder itself (the one holding this
# Makefile). Every other header is then written with its folder, e.g.
# #include "drivers/uart.h", the same from whatever file includes it.
CFLAGS += -I.
# -fno-builtin: GCC normally treats names like printf as the C library
# function it knows. There is no C library here (-nostdlib): our own
# printf in lib/printf.c is a plain function like any other.
CFLAGS += -fno-builtin
# Enable the standard and extra warning sets.
CFLAGS += -Wall -Wextra
# -O0 + full debug info: keep the generated code close to the source so
# it can be stepped through instruction by instruction in class.
CFLAGS += -O0 -g3
# Not -Werror: this code is kept across course years, and a new compiler
# warning should not stop the build.

# Pass the quantum to the C code: -DQUANTUM=5 defines QUANTUM as 5,
# as if kernel/task.h had `#define QUANTUM 5`.
CFLAGS += -DQUANTUM=$(QUANTUM)

# Flag per i programmi caricabili. Come per il kernel: Thumb, Cortex-M3,
# nessuna libreria C. In più:
# -Os: uno slot offre solo 3072 byte a codice e dati, quindi si
#  ottimizza per la dimensione.
# -g: informazioni di debug, per usare gdb con add-symbol-file.
# -ffunction-sections -fdata-sections: ogni funzione e ogni variabile
#  in una sezione sua, così il linker scarta quelle non usate.
# -mword-relocations: gli indirizzi stanno sempre in parole intere da
#  32 bit (R_ARM_ABS32), mai spezzati dentro due istruzioni. È l'unico
#  tipo di indirizzo assoluto che il loader sa correggere.
# -fno-unwind-tables -fno-asynchronous-unwind-tables: niente tabelle
#  per le eccezioni, che porterebbero rilocazioni non ammesse.
# -fno-tree-loop-distribute-patterns: non trasformare i cicli in
#  chiamate a memset/memcpy (programs/rt/mem.c le definisce con un ciclo).
PROGRAM_CFLAGS := -mthumb -mcpu=cortex-m3 -ffreestanding -fno-builtin -std=gnu17
PROGRAM_CFLAGS += -I. -Wall -Wextra -Os -g
PROGRAM_CFLAGS += -ffunction-sections -fdata-sections -mword-relocations
PROGRAM_CFLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables
PROGRAM_CFLAGS += -fno-tree-loop-distribute-patterns

# Collegamento di un programma.
# -T: il linker script dei programmi (indirizzo 0, codice e poi dati).
# --emit-relocs: lascia nel file l'elenco delle rilocazioni, che serve
#  al loader per correggere gli indirizzi nello slot.
# --gc-sections: scarta le sezioni che nessuno usa.
# --build-id=none: nessuna nota con un identificativo della build.
# -z max-page-size=512: pagine da 512 byte per il linker. Con quelle
#  normali da 4 KiB codice e dati, che distano meno di una pagina,
#  finirebbero in un solo segmento con tutti i permessi; così escono
#  due segmenti PT_LOAD, uno eseguibile e uno scrivibile.
# -lgcc: le funzioni di aiuto del compilatore (per esempio la divisione
#  a 64 bit), se il programma ne usa.
PROGRAM_LDFLAGS := -nostdlib -nostartfiles -T $(PROGRAM_LD)
PROGRAM_LDFLAGS += -Wl,--emit-relocs -Wl,--gc-sections -Wl,--build-id=none
PROGRAM_LDFLAGS += -Wl,-z,max-page-size=512

# Use our linker script instead of the toolchain's default.
LDFLAGS := -T scripts/mps2_m3.ld
# Also write a map file: it shows which symbol landed at which address.
LDFLAGS += -Xlinker -Map=$(BUILD_DIR)/output.map
# -nostdlib: do not link a C library or its startup code.
# -lgcc: still link libgcc, the small compiler helper library (for
# example software integer division, which the compiler may use alone).
LDFLAGS += -nostdlib -lgcc

# The command line that starts QEMU: the MPS2-AN385 board, no monitor
# console, no window, UART0 connected to this terminal, our image as
# the program. There is no semihosting: all output goes through the UART.
QEMU := qemu-system-arm -machine mps2-an385 -monitor null -nographic -serial stdio -kernel $(BIN)
QEMU += -device loader,file=$(IMAGE),addr=$(IMAGE_ADDR),force-raw=on

# These targets are command names, not files on disk.
.PHONY: all clean image programs elf-test test-kit-image test-kit run debug run-trace test

# Default goal: build the kernel and the program image.
all: $(BIN) $(IMAGE)

# Compile one .c file to a .o file, creating its build folder first.
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble one .s file (hand-written assembly) to a .o file. We still
# call $(CC): GCC hands a ".s" file straight to the assembler, and the
# same CPU flags make the assembly match what the C files use.
$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Link every object file into the final image. The linker script is a
# prerequisite too, so editing it makes make link again.
$(BIN): $(OBJECTS) scripts/mps2_m3.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJECTS) -o $(BIN)

# --- Programmi caricabili e immagine ---------------------------------

# Compila un sorgente di programs/ (programmi e programs/rt/mem.c).
$(PROGRAM_BUILD)/obj/%.o: $(PROGRAM_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PROGRAM_CFLAGS) -c $< -o $@

# Assembla il codice di avvio (programs/rt/crt0.s).
$(PROGRAM_BUILD)/obj/%.o: $(PROGRAM_DIR)/%.s
	@mkdir -p $(dir $@)
	$(CC) $(PROGRAM_CFLAGS) -c $< -o $@

# Ricompila per i programmi i sorgenti di lib/ (printf e stub).
$(PROGRAM_BUILD)/obj/lib/%.o: lib/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PROGRAM_CFLAGS) -c $< -o $@

$(PROGRAM_BUILD)/obj/lib/%.o: lib/%.s
	@mkdir -p $(dir $@)
	$(CC) $(PROGRAM_CFLAGS) -c $< -o $@

# Collega un programma: il suo oggetto più il codice comune. Poi
# checkelf.py stampa un riepilogo e segnala ciò che il kernel
# rifiuterebbe (senza fermare la build).
$(PROGRAM_BUILD)/%.elf: $(PROGRAM_BUILD)/obj/%.o $(PROGRAM_RT_OBJECTS) $(PROGRAM_LD)
	$(CC) $(PROGRAM_CFLAGS) $(PROGRAM_LDFLAGS) -Wl,-Map=$(@:.elf=.map) $< $(PROGRAM_RT_OBJECTS) -lgcc -o $@
	@python3 $(PROGRAM_CHECK) $@

# Gli oggetti nascono da regole a schema: senza questa riga make li
# cancellerebbe a fine build e li ricompilerebbe ogni volta.
.SECONDARY: $(PROGRAM_OBJECTS) $(PROGRAM_RT_OBJECTS)

# Solo i programmi, senza immagine.
programs: $(PROGRAM_ELFS)

# L'immagine IMG1 con tutti i programmi, costruita dai sorgenti. Si
# rifà quando cambia un programma o lo strumento.
$(IMAGE): $(PROGRAM_ELFS) $(IMAGE_TOOL)
	@mkdir -p $(dir $@)
	python3 $(IMAGE_TOOL) image $@ $(IMAGE_ARGS)

# Come sopra, ma la rifà sempre: utile dopo aver provato un'immagine
# scritta a mano in build/fs.img.
image: $(PROGRAM_ELFS)
	@mkdir -p $(dir $(IMAGE))
	python3 $(IMAGE_TOOL) image $(IMAGE) $(IMAGE_ARGS)

# --- Prova sul PC del controllo degli ELF (progetto, punto C) ---------
# kernel/elf.c non usa nulla del kernel, quindi si compila anche con il
# gcc del PC. `make elf-test`:
#   1. controlla che i programmi veri siano tutti accettati;
#   2. rompe un programma in tutti i modi previsti da tools/break_elf.py;
#   3. controlla che ogni file rotto sia rifiutato.
HOSTCC        ?= gcc
ELF_TEST      := $(BUILD_DIR)/elf_test
BROKEN_DIR    := $(BUILD_DIR)/broken
# L'ELF buono da cui partire per costruire quelli rotti.
BROKEN_SOURCE ?= $(firstword $(PROGRAM_ELFS))

$(ELF_TEST): tools/elf_test.c kernel/elf.c kernel/elf.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -std=gnu17 -Wall -Wextra -I. -o $@ tools/elf_test.c kernel/elf.c

elf-test: $(ELF_TEST) $(PROGRAM_ELFS)
	$(ELF_TEST) $(PROGRAM_ELFS)
	python3 tools/break_elf.py $(BROKEN_SOURCE) --all $(BROKEN_DIR)
	$(ELF_TEST) --bad $(BROKEN_DIR)/*.elf

# --- Test kit ufficiale, suite Big --------------------------------------
# Compila i sorgenti del kit con le stesse regole dei programmi
# (programs/rt e lib/), dà a ognuno il nome che la suite si aspetta,
# genera i file rotti e l'immagine con il generatore del kit, poi lancia
# i 14 test. Ogni test avvia QEMU da capo.
#   make test-kit                   tutta la suite
#   make test-kit TESTS=thumb_ptr   un solo test (TESTS="a b" per più test)
KIT_DIR      := test-kit
KIT_BUILD    := $(BUILD_DIR)/test-kit
KIT_IMAGE    := $(KIT_BUILD)/big/fs.img
# NOME:sorgente. print_plain deve essere il primo: il generatore lo usa
# come base per costruire i file rotti.
KIT_PROGRAMS := print_plain:small/programs/print_plain.c \
                global_state:small/programs/globals_bss_const.c \
                thumb_ptr:small/programs/thumb_function_pointer.c \
                nonzero_slot:small/programs/nonzero_slot_relocation.c \
                normal_return:small/programs/normal_return.c \
                explicit_exit:small/programs/explicit_exit.c \
                kernel_store:small/programs/kernel_store.c \
                wx_write_code:big/programs/wx_write_code.c \
                wx_exec_data:big/programs/wx_exec_data.c
KIT_NAMES    := $(foreach p,$(KIT_PROGRAMS),$(firstword $(subst :, ,$(p))))
KIT_ELFS     := $(KIT_NAMES:%=$(KIT_BUILD)/programs/%.elf)

# Per ogni NOME:sorgente, una regola che compila quel sorgente in
# build/test-kit/obj/NOME.o.
define KIT_COMPILE
$(KIT_BUILD)/obj/$(1).o: $(KIT_DIR)/$(2)
	@mkdir -p $$(dir $$@)
	$$(CC) $$(PROGRAM_CFLAGS) -c $$< -o $$@
endef
$(foreach p,$(KIT_PROGRAMS),$(eval $(call KIT_COMPILE,$(firstword $(subst :, ,$(p))),$(lastword $(subst :, ,$(p))))))

$(KIT_BUILD)/programs/%.elf: $(KIT_BUILD)/obj/%.o $(PROGRAM_RT_OBJECTS) $(PROGRAM_LD)
	@mkdir -p $(dir $@)
	$(CC) $(PROGRAM_CFLAGS) $(PROGRAM_LDFLAGS) $< $(PROGRAM_RT_OBJECTS) -lgcc -o $@
	@python3 $(PROGRAM_CHECK) $@

.SECONDARY: $(KIT_NAMES:%=$(KIT_BUILD)/obj/%.o)

# L'immagine della suite Big: i 9 programmi più i 6 file rotti.
$(KIT_IMAGE): $(KIT_ELFS) $(KIT_DIR)/big/generate_fixtures.py
	python3 $(KIT_DIR)/big/generate_fixtures.py --output-dir $(KIT_BUILD)/big/files \
		--image $@ $(foreach n,$(KIT_NAMES),$(n)=$(KIT_BUILD)/programs/$(n).elf)

test-kit-image: $(KIT_IMAGE)

test-kit: $(BIN) $(KIT_IMAGE)
	cd $(KIT_DIR) && python3 run_tests.py --suite big \
		--kernel $(abspath $(BIN)) --image $(abspath $(KIT_IMAGE)) \
		$(foreach t,$(TESTS),--test $(t))

# Build, load the image at its fixed FLASH address, then run in QEMU.
run: $(BIN) $(IMAGE)
	$(QEMU)

# Same as `run`, but paused for a debugger:
#   -S             - freeze the emulated CPU at reset
#   -gdb tcp::1234 - open a GDB server on TCP port 1234 and wait there
# From a second terminal, connect with:
#   arm-none-eabi-gdb build/scratch-os.axf   (on Linux: gdb-multiarch)
# (for a demo build, e.g. DEMO=rogue: build/demo-rogue/scratch-os.axf;
#  for QUANTUM=2: build/quantum-2/scratch-os.axf)
# and inside gdb:
#   target remote :1234
debug: $(BIN) $(IMAGE)
	$(QEMU) -S -gdb tcp::1234

# Same as `run`, plus `-d unimp`: QEMU prints a message whenever the
# program touches a register or device that QEMU does not implement.
run-trace: $(BIN) $(IMAGE)
	$(QEMU) -d unimp

# Task 5.2 will publish the common test kit. Do not report a false pass
# while that suite is intentionally absent.
test:
	@printf '%s\n' 'make test: unavailable until the common project test kit is implemented (OpenSpec task 5.2).'
	@exit 2

# Remove every generated file, demo and quantum builds included.
clean:
	rm -rf build
