/*
  main.c punto de entrada y bucle REPL
 implementa el núcleo del shell: el ciclo REPL
    1. Inicializar todos los módulos del shell.
    2. Ejecutar el bucle principal: leer línea, parsear, ejecutar.
    3. Gestionar el teardown limpio al salir.
 */

#include "main.h"  
#include "executor.h"  


#ifdef HAVE_PARSER_H
#  include "parser.h"    /* parser_parse_line(), parser_free_pipeline()       */
#endif

#ifdef HAVE_BUILTINS_H
#  include "builtins.h"  /* builtins_init()                                   */
#endif

#ifdef HAVE_JOBS_H
#  include "jobs.h"      /* jobs_init(), jobs_cleanup()                       */
#endif

#ifdef HAVE_HISTORY_H
#  include "history.h"   /* history_init(), history_add(), history_cleanup()  */
#endif

#include <stdio.h> 
#include <stdlib.h>   
#include <string.h>    
#include <stdbool.h>    
#include <unistd.h>      

/*
 shell_last_exit_code:
 codigo de salida del último comando. Inicia en 0 (éxito).
 el integrante 2 lo leerá para implementar && y ||.
 */
int  shell_last_exit_code = 0;

/*
 shell_running:
  el built-in "exit" lo pondrá en false para salir del bucle principal.
 */
bool shell_running = true;


/*
 shell_print_prompt()
  Imprime el prompt en stdout y fuerza el vaciado del buffer.
  fflush(stdout) es necesario porque stdout es line-buffered por defecto
  cuando está conectado a una terminal. Sin fflush, el prompt podría no
  aparecer antes de que el programa quede bloqueado esperando input.
 */
void shell_print_prompt(void)
{
    printf("%s", SHELL_PROMPT);
    fflush(stdout);
}

/* 
 read_line()
  Lee una línea de stdin y la almacena en el buffer proporcionado.
    usa fgets() que es seguro (respeta el tamaño del buffer).
    elimina el '\n' final que fgets incluye.
    si el usuario presiona Ctrl-D (EOF), retorna false para indicar
    que el shell debe terminar limpiamente.
 
  Parámetros:
    buffer: Buffer donde se almacena la línea leída.
    buf_size: Tamaño del buffer en bytes.
 
  Retorna:
    true  si se leyó una línea correctamente.
    false si se alcanzó EOF (Ctrl-D) o hubo un error de lectura.
 */
static bool read_line(char *buffer, size_t buf_size)
{
    if (fgets(buffer, (int)buf_size, stdin) == NULL) {
        /* EOF o error de lectura */
        if (feof(stdin)) {
            /* Ctrl-D: salida limpia */
            printf("\n"); /* Salto de línea estético antes de salir */
        } else {
            perror("[main] fgets");
        }
        return false;
    }

    /* Eliminar el '\n' final que fgets deja en el buffer.
      strcspn() retorna la posición del primer '\n' o '\r'.
      Si no hay '\n' (línea muy larga truncada), no hace nada. */
    buffer[strcspn(buffer, "\n\r")] = '\0';

    return true;
}

/* 
  execute_line()
  Procesa y ejecuta una línea de entrada del usuario.
    La línea puede contener múltiples comandos separados por ';', '&&', '||'.
    El parser devuelve una lista de Pipelines con sus operadores de control.
    Este función itera sobre ellas, ejecutando cada una según el operador
    y el código de salida del comando anterior.
 
  Parámetros:
    line — Línea de entrada (ya sin '\n', no vacía).
 */
static void execute_line(const char *line)
{

}
int main(void)
{
    /*
    inicializar los módulos en el orden correcto.
    history debe cargarse antes del bucle REPL para
    que las flechas funcionen desde el primer comando.
     */

    /* inicializar el motor de ejecución (cachea $PATH, instala SIGCHLD) */
    executor_init();

    /* inicializar la tabla de jobs del integrante 3 */
#ifdef HAVE_JOBS_H
    jobs_init();
#endif

    /* inicializar el historial del Integrante 3 (carga ~/.ucvsh_history) */
#ifdef HAVE_HISTORY_H
    history_init();
#endif

    /* inicializar los built-ins del Integrante 2 (si requieren init) */
#ifdef HAVE_BUILTINS_H
    builtins_init();
#endif

    /* BUCLE REPL
      Se ejecuta indefinidamente hasta que:
        a) El built-in "exit" pone shell_running = false
        b) El usuario presiona Ctrl-D (EOF en stdin)
        c) Un error irrecuperable de lectura
     */

    char input_buffer[MAX_INPUT_LEN];

    while (shell_running) {

        /*mostrar prompt y leer entrada del usuario

        /*
         *mostrar el prompt si stdin es una terminal interactiva.
         * Si ucvsh recibe su input de un pipe o archivo (modo script),

         */

        /* Registrar en el historial  */

        history_add(input_buffer);

    executor_cleanup();  /* liberar arreglo de directorios de $PATH */

    return shell_last_exit_code; /* el shell retorna el último exit code */
}
