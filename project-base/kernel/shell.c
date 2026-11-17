#include <stdint.h>
#include "kernel/task.h"
#include "lib/printf.h"
#include "lib/syscalls.h"

#define LINE_MAX 31

/* simboli definiti dal linker script: contano solo i loro indirizzi */
extern uint32_t _shell_stack_top[];
extern uint32_t _shell_data_start[], _shell_data_end[], _shell_data_load[];
extern char     _shell_part_start[], _shell_part_end[];

static void read_line(char *line)
{
    unsigned len = 0;

    for (;;) {
        int c = sys_read();

        if (c < 0) {                 /* nessun byte in attesa */
            task_yield();            /* cedo la CPU e riprovo dopo */
            continue;
        }
        if (c == '\r' || c == '\n') {
            printf("\n");
            line[len] = '\0';
            return;
        }
        if (len < LINE_MAX) {
            line[len++] = (char)c;
            printf("%c", c);         /* eco solo dei caratteri salvati */
        }
        /* oltre 31: carattere scartato, niente eco */
    }
}


//vede se le stringe sono identiche
static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
//confornta ogni volta la prima lettera
static int starts_with(const char *s, const char *prefix)
{
    while (*prefix)
        if (*s++ != *prefix++)
            return 0;
    return 1;
}

/* cmd_ls: chiede al kernel quanti programmi ci sono e ne stampa
 * i nomi, uno per riga, nell'ordine della directory. */
static void cmd_ls(void)
{
    char name[16];                       /* 15 caratteri + '\0' */
    int count = sys_image_count();

    if (count < 0) {
        printf("error: no valid image\n");
        return;
    }
    for (int i = 0; i < count; i++) {
        if (sys_image_name((unsigned)i, name, sizeof name) == 0)
            printf("%s\n", name);
    }
}

/* cmd_run: chiede al kernel di avviare il programma. Se fallisce
 * stampa l'errore giusto; se riesce aspetta che il programma finisca. */
static void cmd_run(const char *name)
{
    int id = sys_program_run(name);

    if (id == -2)
        printf("error: program not found\n");
    else if (id == -3)
        printf("error: no free slot\n");
    else if (id < 0)
        printf("error: program cannot be loaded\n");
    else
        sys_program_wait(id);            /* il risultato si ignora */
}

static void run_command(const char *line)
{
    if (str_eq(line, "help")) {
        printf("help - show commands\n"
               "ls - list programs\n"
               "run <name> - run a program\n");
    } else if (str_eq(line, "ls")) {
        cmd_ls();
    } else if (starts_with(line, "run ") && line[4] != '\0') {
        cmd_run(line + 4);
    } else {
        printf("error: unknown command\n");
    }
}


static void shell_main(void)
{
    char line[LINE_MAX + 1];         /* 31 caratteri + '\0' */

    for (;;) {
        printf("scratch> ");
        read_line(line);
        run_command(line); 
    }
}

void task_project_configure(void)
{
    task_configure_shell("shell", shell_main,
                         _shell_stack_top,
                         (uint32_t)_shell_part_start,
                         (uint32_t)_shell_part_end,
                         _shell_data_start, _shell_data_end,
                         _shell_data_load);
}
