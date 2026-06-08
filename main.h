/*
exposicion de las constantes globales del shell 
 */

#ifndef MAIN_H
#define MAIN_H

#include <stdbool.h>

/*prompt que se muestra al usuario */
#define SHELL_PROMPT    "ucvsh> "

/*nombre del binario*/
#define SHELL_NAME      "ucvsh"

/* longitud máxima de una línea de entrada del usuario */
#define MAX_INPUT_LEN   4096


/* shell_last_exit_code
 * coigo de salida del último comando ejecutado en foreground.
 * se actualiza en main.c después de cada llamada a executor_run_pipeline().
 * necesario para que el integrante 2 implemente los operadores && y ||:
 *   - && ejecuta el comando derecho si shell_last_exit_code == 0
 *   - || ejecuta el comando derecho si shell_last_exit_code != 0
 */
extern int shell_last_exit_code;

/*
 * shell_running
 * Bandera de control del bucle REPL principal.
 * Cuando el built-in "exit" se ejecuta integrante 2, pone esta variable
 * en false para que el bucle en main.c termine limpiamente.
 */
extern bool shell_running;


/*
 * shell_print_prompt()
 * Imprime el prompt "ucvsh> " en stdout.
 * (mostrar el directorio actual, usuario, rama de git)
 */
void shell_print_prompt(void);

#endif /* MAIN_H */
