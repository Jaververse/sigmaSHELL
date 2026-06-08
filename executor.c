/*
  executor.c — Implementación del Motor 
  Este archivo implementa toda la lógica de ejecución de comandos del shell.
  Es el módulo que interactúa directamente con el núcleo Linux a través de
  syscalls como fork(), execv(), waitpid(), pipe() y dup2().
 
  Estructura interna del archivo:
    1. Includes y constantes privadas
    2. Variables de módulo (estado interno)
    3. Funciones privadas (helpers, no declaradas en .h)
    4. Implementación de las funciones públicas (declaradas en executor.h)
 */


#include "executor.h"


/* POSIX */
#include <unistd.h>    
#include <sys/types.h>   
#include <sys/wait.h>    
#include <fcntl.h>       
#include <signal.h>     

/*Librería estándar de C*/
#include <stdio.h>  
#include <stdlib.h>     
#include <string.h>     
#include <errno.h>     
#include <stdbool.h>     


/*
  path_dirs[] — Caché de los directorios de búsqueda.
  Se puebla en executor_init() leyendo $PATH una sola vez.
  Cada entrada es un puntero a memoria dinámica (strdup).
  path_dirs_count indica cuántas entradas son válidas.
 */
static char  *path_dirs[MAX_PATH_DIRS];
static int    path_dirs_count = 0;

/*
  Rutas estándar de Linux usadas como fallback cuando $PATH no está definida.
  Cubre la mayoría de distribuciones modernas.
 */
static const char *DEFAULT_PATH_DIRS[] = {
    "/bin",
    "/usr/bin",
    "/sbin",
    "/usr/sbin",
    "/usr/local/bin",
    "/usr/local/sbin",
    NULL 
};


/*
 sigchld_handler()
  Manejador de la señal SIGCHLD.
 
  Cuando un proceso hijo en background termina, el kernel envía SIGCHLD
  al proceso padre (ucvsh), sin manejar esta señal, el hijo quedaría como
  proceso zombie (entrada en la tabla de procesos del kernel sin liberarse)
  hasta que el padre llame a wait().
 
  Este manejador llama a waitpid() con WNOHANG en un bucle para cosechar
  (reap) todos los hijos que ya terminaron sin bloquear al shell.
 
  El flag WNOHANG es crucial, sin él, waitpid() bloquearía al shell mientras
  espera a un hijo, lo que es inaceptable durante la ejecución interactiva.
 
  también actualiza el estado del trabajo en la tabla interna del Integrante 3.
*/
static void sigchld_handler(int signo)
{
    (void)signo; /* suprimir warning de parámetro no usado */

    pid_t pid;
    int   status;

    /*
     * Bucle: waitpid(-1, ...) espera a CUALQUIER hijo.
     * WNOHANG: retorna inmediatamente si no hay hijos listos (pid == 0).
     * WUNTRACED: también notifica si un hijo fue suspendido (Ctrl-Z).
     * Continua hasta que no queden hijos terminados (pid <= 0).
     */
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {

        /* job integrante 3, actualizamos la tabla */
#ifdef HAVE_JOBS_H
        if (WIFEXITED(status)) {
            jobs_update_status(pid, JOB_DONE);
            fprintf(stderr, "\n[ucvsh] Trabajo PID %d finalizado (código %d)\n",
                    pid, WEXITSTATUS(status));
        } else if (WIFSTOPPED(status)) {
            jobs_update_status(pid, JOB_STOPPED);
        }
#else
        /* Sin módulo de jobs: simplemente imprimimos una notificación */
        if (WIFEXITED(status)) {
            fprintf(stderr, "\n[ucvsh] Proceso en background PID %d finalizado\n",
                    pid);
        }
#endif
    }
}

/* 
 * parse_path_env()
  Lee la variable de entorno $PATH, la divide por ':' y almacena cada
  directorio en el arreglo path_dirs[] del módulo.
 
  Utiliza strtok_r (versión reentrante y segura de strtok) para no modificar
  la cadena original del entorno.
 
 * Si $PATH no existe o está vacía, carga las rutas de DEFAULT_PATH_DIRS.
*/
static void parse_path_env(void)
{
    /* Limpiar entradas anteriores si las hubiera*/
    for (int i = 0; i < path_dirs_count; i++) {
        free(path_dirs[i]);
        path_dirs[i] = NULL;
    }
    path_dirs_count = 0;

    const char *path_env = getenv("PATH");

    if (path_env == NULL || path_env[0] == '\0') {
        /* $PATH no definida: usar rutas por defecto */
        fprintf(stderr, "[executor] AVISO: $PATH no definida, usando rutas por defecto\n");

        for (int i = 0; DEFAULT_PATH_DIRS[i] != NULL && path_dirs_count < MAX_PATH_DIRS; i++) {
            path_dirs[path_dirs_count] = strdup(DEFAULT_PATH_DIRS[i]);
            if (path_dirs[path_dirs_count] == NULL) {
                perror("[executor] strdup en parse_path_env (defecto)");
                continue;
            }
            path_dirs_count++;
        }
        return;
    }

    /* hace una copia de $PATH porque strtok_r la modifica */
    char *path_copy = strdup(path_env);
    if (path_copy == NULL) {
        perror("[executor] strdup en parse_path_env");
        return;
    }

    char *saveptr = NULL;        /* puntero de estado para strtok_r */
    char *token   = strtok_r(path_copy, ":", &saveptr);

    while (token != NULL && path_dirs_count < MAX_PATH_DIRS) {
        path_dirs[path_dirs_count] = strdup(token);
        if (path_dirs[path_dirs_count] == NULL) {
            perror("[executor] strdup en parse_path_env (token)");
        } else {
            path_dirs_count++;
        }
        token = strtok_r(NULL, ":", &saveptr);
    }

    free(path_copy);
}

/* 
 * redirect_io()
  Aplica las redirecciones de entrada/salida declaradas en un Command.
  Debe llamarse DENTRO del proceso hijo, justo antes de execv().
 
    - Si cmd->input_file  != NULL: abre el archivo en modo lectura y lo
      conecta a STDIN_FILENO usando dup2().
    - Si cmd->output_file != NULL: abre (o crea/trunca) el archivo en modo
      escritura y lo conecta a STDOUT_FILENO usando dup2().
    - Si cmd->output_append == true: usa O_APPEND en lugar de O_TRUNC.
 
  dup2(oldfd, newfd): hace que newfd sea una copia de oldfd.
  Después del dup2, el fd original se cierra (ya no se necesita).
 
  Retorna 0 en éxito, -1 en error.
 */
static int redirect_io(const Command *cmd)
{
    /* Redirección de entrada: < archivo */
    if (cmd->input_file != NULL) {
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd == -1) {
            perror(cmd->input_file);
            return -1;
        }
        /* dup2: STDIN_FILENO (fd 0) ahora apunta al archivo */
        if (dup2(fd, STDIN_FILENO) == -1) {
            perror("[executor] dup2 input");
            close(fd);
            return -1;
        }
        close(fd); /* el fd original ya no se necesita después del dup2 */
    }

    /* Redirección de salida: > archivo  o  >> archivo */
    if (cmd->output_file != NULL) {
        int flags = O_WRONLY | O_CREAT;
        flags |= (cmd->output_append) ? O_APPEND : O_TRUNC;

        /* Permisos del archivo nuevo: rw-r--r-- (0644) */
        int fd = open(cmd->output_file, flags, 0644);
        if (fd == -1) {
            perror(cmd->output_file);
            return -1;
        }
        /* dup2: STDOUT_FILENO (fd 1) ahora apunta al archivo */
        if (dup2(fd, STDOUT_FILENO) == -1) {
            perror("[executor] dup2 output");
            close(fd);
            return -1;
        }
        close(fd);
    }

    return 0;
}

/* 
  close_all_pipe_ends()
  Cierra todos los extremos de TODOS los pipes pasados en el arreglo.
 
  Se usa dentro de cada proceso hijo para asegurarse de que no quede ningún
  extremo de pipe abierto que no le pertenezca. Sin esta limpieza, los
  procesos de lectura nunca recibirían EOF y quedarían bloqueados para
  siempre (deadlock).
 
  Parámetros:
    pipe_fds  — Matriz de pares de file descriptors: pipe_fds[i][0] = lectura,
       pipe_fds[i][1] = escritura.
    count     — Número de pipes en el arreglo.
 */
static void close_all_pipe_ends(int pipe_fds[][2], int count)
{
    for (int i = 0; i < count; i++) {
        if (pipe_fds[i][0] != -1) close(pipe_fds[i][0]);
        if (pipe_fds[i][1] != -1) close(pipe_fds[i][1]);
    }
}

/*
 * executor_init()
*/
void executor_init(void)
{
    /* 1. Cachear los directorios de $PATH */
    parse_path_env();

    /*
      2. Instalar el manejador de SIGCHLD.
     
      SA_RESTART: si SIGCHLD interrumpe una syscall bloqueante (como read()),
      el kernel la reiniciará automáticamente en lugar de fallar con EINTR.
      Esto es importante para que el historial (Integrante 3) no pierda
      la entrada del usuario por una señal.
     */
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    /* SA_NOCLDSTOP: no notificar cuando el hijo se detenga (solo al terminar) */

    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("[executor] sigaction SIGCHLD");
        /* No fatal: el shell puede seguir funcionando, solo habrá zombies */
    }

    fprintf(stderr, "[executor] Inicializado. Directorios en PATH: %d\n",
            path_dirs_count);
}

/* 
 * executor_cleanup()
*/
void executor_cleanup(void)
{
    for (int i = 0; i < path_dirs_count; i++) {
        free(path_dirs[i]);
        path_dirs[i] = NULL;
    }
    path_dirs_count = 0;
}

/*
 * executor_find_binary()

  Algoritmo de búsqueda:
    Caso especial 1: Si `name` ya contiene una '/' (ruta absoluta o relativa
    como "./mi_programa" o "/usr/bin/gcc"), se verifica directamente con
    access() sin buscar en PATH.
 
    Caso normal: Para cada directorio D en path_dirs[]:
     - Construye la ruta: D + "/" + name
      - Verifica con access(ruta, X_OK) si el archivo existe y es ejecutable
     - Si sí: copia la ruta a out_path y retorna 0
    Si no se encontró en ningún directorio: retorna -1.
*/
int executor_find_binary(const char *name, char *out_path)
{
    if (name == NULL || out_path == NULL) return -1;

    /*
      Caso especial: ruta absoluta o relativa (contiene '/').
      El usuario escribió algo como "/bin/ls" o "./mi_script.sh".
      Verificamos directamente si es ejecutable.
     */
    if (strchr(name, '/') != NULL) {
        if (access(name, X_OK) == 0) {
            strncpy(out_path, name, MAX_BINARY_PATH - 1);
            out_path[MAX_BINARY_PATH - 1] = '\0';
            return 0;
        }
        /* La ruta explícita no existe o no es ejecutable */
        return -1;
    }

    /*
      Caso normal: buscar en los directorios cacheados de $PATH.
      Para cada directorio: concatenamos dir + "/" + name y verificamos.
     */
    for (int i = 0; i < path_dirs_count; i++) {
        /* Construir la ruta completa en out_path */
        int written = snprintf(out_path, MAX_BINARY_PATH, "%s/%s",
                               path_dirs[i], name);

        if (written < 0 || written >= MAX_BINARY_PATH) {
            /* La ruta resultante sería demasiado larga, ignorar este directorio */
            continue;
        }

        /* access(X_OK): verifica permiso de ejecución */
        if (access(out_path, X_OK) == 0) {
            return 0; /* Encontrado, out_path ya contiene la ruta completa */
        }
    }

    /* No encontrado en ningún directorio de $PATH */
    return -1;
}

/* 
  executor_run_simple()

  Ejecuta un comando simple, sin pipes

    1. VERIFICAR BUILT-IN: Si builtins.h está disponible, delegamos al
       módulo del Integrante 2. Los built-ins (cd, exit, jobs) no hacen
       fork(); se ejecutan en el proceso del propio shell.
 
   2. BUSCAR BINARIO: Llamamos a executor_find_binary(). Si no se encuentra,
       imprimimos error y retornamos -1.
 
    3. fork(): Duplicamos el proceso del shell.
       - En el HIJO: aplicamos redirecciones de I/O y llamamos a execv().
         execv() reemplaza la imagen del proceso hijo por el binario encontrado.
         Si execv() retorna, fue un error.
       - En el PADRE:
           * Foreground: waitpid() bloquea hasta que el hijo termina.
             Extraemos el código de salida con WEXITSTATUS.
           * Background: registramos el PID en la tabla de jobs y retornamos
             inmediatamente con código 0.
 */
int executor_run_simple(const Command *cmd)
{
    if (cmd == NULL || cmd->argc == 0 || cmd->argv[0] == NULL) return -1;

    /* 1. Verificar si es un built-in
      Los built-ins se ejecutan en el proceso del shell sin fork().
      Ejemplos: exit, cd, jobs, history.
      La implementación real viene del módulo del integrante 2.
     */
#ifdef HAVE_BUILTINS_H
    if (builtins_is_builtin(cmd->argv[0])) {
        return builtins_execute(cmd);
    }
#else
    /*
      implementación provisional de exit y jobs mientras builtins.h
      no está disponible. Permite probar el executor de forma aislada.
     */
    if (strcmp(cmd->argv[0], "exit") == 0) {
        fprintf(stderr, "[executor] built-in exit (provisional)\n");
        executor_cleanup();
        exit(0);
    }
#endif

    /*2. Buscar el binario en $PATH */
    char binary_path[MAX_BINARY_PATH];

    if (executor_find_binary(cmd->argv[0], binary_path) != 0) {
        fprintf(stderr, "ucvsh: comando no encontrado: %s\n", cmd->argv[0]);
        return 127; /* 127 = convención estándar de shell: command not found */
    }

    /* 3. fork()*/
    pid_t pid = fork();

    if (pid == -1) {
        /* fork() fallido: error del kernel (falta de recursos, etc.) */
        perror("[executor] fork");
        return -1;
    }

    if (pid == 0) {
        /*
          PROCESO HIJO
          En este punto somos una copia exacta del padre (ucvsh).
          Nuestro objetivo: transformarnos en el programa solicitado.
         */

        /* Restaurar SIGCHLD al comportamiento por defecto en el hijo.
          El hijo no debe heredar el manejador del padre. */
        signal(SIGCHLD, SIG_DFL);

        /* Aplicar redirecciones de I/O (< archivo, > archivo, >> archivo) */
        if (redirect_io(cmd) != 0) {
            /* Error en la redirección: terminar el hijo */
            exit(1);
        }

        /*
          execv(path, argv):
            - path:  ruta completa al binario (ej: "/bin/ls")
            - argv:  arreglo de strings con los argumentos.
                     argv[0] = nombre del programa (convención UNIX)
                     argv[argc] = NULL (centinela obligatorio para execv)
         
          execv() NO retorna si tiene éxito: reemplaza completamente
          la imagen del proceso con el nuevo programa.
          Si retorna, ocurrió un error.
         */
        execv(binary_path, cmd->argv);

        /* Si llegamos aquí, execv() falló */
        perror(binary_path);
        exit(127);
    }

    /* 
      PROCESO PADRE
      pid > 0: somos el shell padre. El hijo ya está corriendo.
     */

    if (cmd->background) {
        /*Modo background (asíncrono): no esperamos al hijo
          Registramos el PID en la tabla de jobs del Integrante 3
          y devolvemos inmediatamente el control al usuario.
         */
        fprintf(stderr, "[%d] %d\n", 1, pid); /* Formato tradicional de bash */

#ifdef HAVE_JOBS_H
        jobs_add(pid, cmd->argv[0]);
#endif
        return 0;

    } else {
        /*Modo foreground (síncrono): esperamos al hijo
        
         waitpid(pid, &status, 0):
            - pid:    esperamos específicamente a ESTE hijo
            - status: donde el kernel escribe el estado de terminación
            - 0:      bloqueante (sin WNOHANG)
         
          Usamos WIFEXITED y WEXITSTATUS para extraer el código de salida.
          El código de salida es importante para los operadores && y ||
          que evaluará el Integrante 2 en parser.c.
         */
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            perror("[executor] waitpid");
            return -1;
        }

        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);   /* Código de salida normal (0-255) */
        } else if (WIFSIGNALED(status)) {
            /* El proceso fue terminado por una señal (Ctrl-C = SIGINT, etc.) */
            return 128 + WTERMSIG(status); /* Convención bash */
        }

        return -1;
    }
}

/*
  executor_run_piped()
  Implementa la ejecución de una cadena de N comandos con N-1 pipes.
 */
int executor_run_piped(const Pipeline *pipeline)
{
    int   n          = pipeline->count;  /* Número de comandos en la cadena */
    int   num_pipes  = n - 1;            /* Número de pipes necesarios       */

    /*
      Creamos todos los pipes antes de hacer ningún fork().
      pipe_fds[i][0] = extremo de lectura  del pipe i
      pipe_fds[i][1] = extremo de escritura del pipe i
      Inicializamos a -1 para saber qué fds no se crearon en caso de error.
     */
    int pipe_fds[MAX_PIPE_SEGMENTS - 1][2];
    for (int i = 0; i < num_pipes; i++) {
        pipe_fds[i][0] = -1;
        pipe_fds[i][1] = -1;
    }

    for (int i = 0; i < num_pipes; i++) {
        if (pipe(pipe_fds[i]) == -1) {
            perror("[executor] pipe");
            /* Cerrar los pipes ya creados antes de abortar */
            for (int j = 0; j < i; j++) {
                close(pipe_fds[j][0]);
                close(pipe_fds[j][1]);
            }
            return -1;
        }
    }

    /* Arreglo de PIDs de los hijos para waitpid posterior */
    pid_t child_pids[MAX_PIPE_SEGMENTS];

    /*Crear un proceso hijo para cada comando de la pipeline*/
    for (int i = 0; i < n; i++) {
        const Command *cmd = &pipeline->commands[i];

        /* Buscar el binario de este segmento */
        char binary_path[MAX_BINARY_PATH];
        if (executor_find_binary(cmd->argv[0], binary_path) != 0) {
            fprintf(stderr, "ucvsh: comando no encontrado: %s\n", cmd->argv[0]);
            /* Limpiar todos los pipes y abortar */
            close_all_pipe_ends(pipe_fds, num_pipes);
            return 127;
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("[executor] fork en pipeline");
            close_all_pipe_ends(pipe_fds, num_pipes);
            return -1;
        }

        if (pid == 0) {
            /* 
              PROCESO HIJO i
             */

            signal(SIGCHLD, SIG_DFL);

            /*  Conectar stdin al pipe del segmento anterior
              Todos los comandos excepto el primero (i > 0) leen del pipe
              del segmento anterior: pipe_fds[i-1][0]
             */
            if (i > 0) {
                if (dup2(pipe_fds[i-1][0], STDIN_FILENO) == -1) {
                    perror("[executor] dup2 stdin pipe");
                    exit(1);
                }
            }

            /*  Conectar stdout al pipe del segmento siguiente 
              Todos los comandos excepto el último (i < n-1) escriben al
              pipe del segmento siguiente: pipe_fds[i][1]
             */
            if (i < n - 1) {
                if (dup2(pipe_fds[i][1], STDOUT_FILENO) == -1) {
                    perror("[executor] dup2 stdout pipe");
                    exit(1);
                }
            }

            /*
              Cerrar todos los extremos de TODOS los pipes.
              El hijo ya los copió a STDIN/STDOUT con dup2(). Los fds
              originales ya no son necesarios y DEBEN cerrarse.
             
              Si un hijo deja abierto el extremo de escritura de un pipe
              del que no es el escritor, el lector de ese pipe nunca
              recibirá EOF (el kernel solo envía EOF cuando TODOS los
              escritores cierran su extremo). Resultado: DEADLOCK.
             */
            close_all_pipe_ends(pipe_fds, num_pipes);

            /* Aplicar redirecciones de archivo (< o >) si las hay */
            if (redirect_io(cmd) != 0) {
                exit(1);
            }

            /* Reemplazar la imagen del proceso con el binario */
            execv(binary_path, cmd->argv);
            perror(binary_path);
            exit(127);
        }

        /* 
          PADRE: registrar PID del hijo creado
         */
        child_pids[i] = pid;

        /*
          El padre cierra los extremos del pipe anterior una vez que el
          hijo que los necesitaba ya fue creado.
         */
        if (i > 0) {
            /* Cerrar el pipe del que el hijo i ya leyó */
            close(pipe_fds[i-1][0]);
            close(pipe_fds[i-1][1]);
        }
    }

    /*
      El padre cierra el último pipe (el del último escritor) que quedó
      pendiente después del bucle.
     */
    if (num_pipes > 0) {
        close(pipe_fds[num_pipes - 1][0]);
        close(pipe_fds[num_pipes - 1][1]);
    }

    /*Esperar a los hijos o registrarlos en background */
    int last_exit_code = 0;

    if (pipeline->background) {
        /* Background: registrar todos los PIDs y retornar */
        for (int i = 0; i < n; i++) {
            fprintf(stderr, "[%d] %d\n", i + 1, child_pids[i]);
#ifdef HAVE_JOBS_H
            jobs_add(child_pids[i], pipeline->commands[i].argv[0]);
#endif
        }
        return 0;

    } else {
        /* Foreground: esperar a TODOS los hijos.
         * Importante: esperamos en orden para obtener el código de salida
         * del ÚLTIMO comando de la pipeline (comportamiento de bash). */
        for (int i = 0; i < n; i++) {
            int status;
            if (waitpid(child_pids[i], &status, 0) == -1) {
                perror("[executor] waitpid en pipeline");
                continue;
            }
            if (i == n - 1) {
                /* Nos interesa el código de salida del último proceso */
                if (WIFEXITED(status)) {
                    last_exit_code = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    last_exit_code = 128 + WTERMSIG(status);
                }
            }
        }
        return last_exit_code;
    }
}

/* 
 executor_run_pipeline()=
 Punto de entrada público. Decide si delegar a run_simple o run_piped.
*/
int executor_run_pipeline(const Pipeline *pipeline)
{
    if (pipeline == NULL || pipeline->count == 0) return -1;

    if (pipeline->count == 1) {
        /*
          Un solo comando: puede tener redirecciones pero no pipes.
          Propagamos el flag background del pipeline al comando.
         */
        Command single = pipeline->commands[0];
        single.background = pipeline->background;
        return executor_run_simple(&single);
    }

    /* Dos o más comandos: pipeline con pipes */
    return executor_run_piped(pipeline);
}
