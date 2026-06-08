/*
 executor.h — Motor de Ejecución
 
declara todas las estructuras de datos, constantes y prototipos de función que conforman el motor de ejecución del shell.
    - Búsqueda dinámica de binarios en $PATH
    - Ejecución síncrona (foreground) y asíncrona (background)
    - Construcción y gestión de pipelines
    - Orquestación de fork() + execv() + waitpid()
 
  Convención de errores: todas las funciones retornan -1 en caso de fallo
  y escriben el motivo en stderr mediante perror() o fprintf(stderr, ...).
 */

#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <sys/types.h>
#include <stdbool.h>     


/* numero maximo de segmentos que puede tener una pipeline.
   ejm: cmd1 | cmd2 | cmd3 | ... | cmd_MAX_PIPE_SEGMENTS
   si el usuario introduce más segmentos, el executor emite error y no ejecuta.
 */
#define MAX_PIPE_SEGMENTS   16

/* longitud máxima de una ruta de directorio leída de $PATH. */
#define MAX_PATH_LEN       512

/*longitud máxima del path completo a un binario (dir + "/" + nombre) */
#define MAX_BINARY_PATH    (MAX_PATH_LEN + 256)

/* numero maximo de directorios en $PATH que el executor recorrera */
#define MAX_PATH_DIRS       64



int executor_run_pipeline(const Pipeline *pipeline);

/*
  executor_find_binary()
  Busca el binario cuyo nombre se pasa en `name` recorriendo cada directorio
  listado en la variable de entorno $PATH. La búsqueda es iterativa:
  para cada directorio D en PATH verifica si D/name existe y es ejecutable
  (usando access(path, X_OK)).
 
  Si $PATH no está definida en el entorno, usa como fallback la lista de
  rutas estándar de Linux: /bin, /usr/bin, /sbin, /usr/sbin, /usr/local/bin.
 
  Parámetros:
    name: Nombre del binario a buscar (ej: "ls", "gcc", "vim").
    out_path: Buffer de salida donde se escribe la ruta completa encontrada.
    Debe tener al menos MAX_BINARY_PATH bytes.
 
  Retorna:
    0  si el binario fue encontrado (out_path contiene la ruta completa).
    -1  si no se encontró el binario en ningún directorio de $PATH.
 */
int executor_find_binary(const char *name, char *out_path);

/*
  executor_run_simple()

  Ejecuta un único Command, sin pipes. internamente:
    1. Si el comando es un built-in, delega a builtins_execute() y retorna.
    2. Llama a executor_find_binary() para localizar el binario.
    3. Realiza fork(): el hijo redirige stdin/stdout si hay archivos de
       redirección y llama a execv(). El padre espera (foreground) o no
       (background) según cmd a background.
 
  Parámetros:
    cmd: Puntero al Command a ejecutar.
 
  Retorna:
    codigo de salida del proceso hijo, o -1 en caso de error.
 */
int executor_run_simple(const Command *cmd);

/*
  executor_run_piped()
 
  Ejecuta una cadena de N comandos conectados por N-1 pipes en memoria.
  El algoritmo es el siguiente:
 
    Para i en [0, count-1]:
      - Si no es el último: crea un pipe (pipe_fds[i]) con pipe()
      - fork() → hijo i:
          * Si no es el primero: dup2(pipe_fds[i-1][0], STDIN_FILENO)
          * Si no es el último:  dup2(pipe_fds[i][1],   STDOUT_FILENO)
          * Cierra TODOS los extremos de pipes del hijo
          * execv(binario, argv)
      - El padre cierra los extremos que ya no necesita de pipe_fds[i-1]
 
    Al final, el padre espera a todos los hijos con waitpid() (foreground)
    o sólo registra sus PIDs en la tabla de jobs (background).
 
  Garantías de corrección:
    - Cierra todos los extremos no usados para evitar deadlocks (lectores
      que esperan EOF que nunca llega porque alguien tiene el extremo
      de escritura abierto).
 
  Parámetros:
    pipeline  — Puntero a la Pipeline con count >= 2.
 
  Retorna:
    Código de salida del último proceso de la cadena, o -1 en error.
 */
int executor_run_piped(const Pipeline *pipeline);

/**
 * executor_init()
 * Inicializa el módulo del executor. Actualmente:
 *   - Parsea y cachea los directorios de $PATH en un arreglo interno para
 *     no tener que llamar a getenv("PATH") en cada búsqueda de binario.
 *   - Configura el manejador de SIGCHLD para limpiar procesos zombie de
 *     trabajos en background.
 *
 * Debe llamarse UNA SOLA VEZ desde main() antes del bucle REPL.
 */
void executor_init(void);

/**
 * executor_cleanup()
 * Libera los recursos internos del executor (arreglo de rutas cacheadas,
 * etc.). Debe llamarse al salir del shell (antes o durante el built-in exit).
 */
void executor_cleanup(void);

#endif /* EXECUTOR_H */
