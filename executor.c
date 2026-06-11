#include "executor.h"
#include "builtins.h"
#include "jobs.h"     
#include <unistd.h>  
#include <sys/types.h> 
#include <sys/wait.h> 
#include <fcntl.h>    
#include <signal.h>   
#include <stdio.h>     
#include <stdlib.h>   
#include <string.h>  
#include <errno.h>   


//cache de directorios de $PATH, se llena una sola vez en executor_init()
static char *path_dirs[MAX_PATH_DIRS];
static int   path_dirs_count = 0;

//puntero a la tabla de jobs global, se recibe en executor_init()
static JobTable *g_jobs = NULL;

//validar
int validarsyntax(NodeComando *head) {
    NodeComando *cur = head;
    while (cur != NULL) {
        // Si el comando actual tiene background, el siguiente operador NO puede ser un pipe
        if (cur->background && cur->next_op == OP_PIPE) {
            fprintf(stderr, "ucvsh: error sintáctico cerca del elemento inesperado `|'\n");
            return -1;
        }
        cur = cur->next;
    }
    return 0;
}
//rutas estandar de Linux usadas si $PATH no está definida
static const char *FALLBACK_PATHS[] = {
    "/bin", "/usr/bin", "/sbin", "/usr/sbin", "/usr/local/bin", NULL
};

//rutina asincrona del SO que intercepta terminaciones de hijos
static void sigchld_handler(int signo)
{
(void)signo;
pid_t pid;
int   status;

//recolecta hijos terminados dinamicamente y marca la tabla como DONE sin bloquear la shell
while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    if (g_jobs == NULL) continue;

     //buscar el job en la tabla y marcarlo como done
    Job *j = g_jobs->head;
    while (j != NULL) {
        if (j->pgid == pid) {
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                j->state = DONE;
                }
                break;
            }
            j = j->next;
        }
    }
}

//configuracion general previa al primer prompt del bucle REPL
void executor_init(JobTable *table)
{
//guardar referencia a la tabla de jobs para usarla en sigchld_handler
g_jobs = table;

//parsear $PATH y cachear los directorios
const char *path_env = getenv("PATH");

//procesa la variable de entorno PATH trozandola y resguardando directorios seguros
if (path_env == NULL || path_env[0] == '\0') {
    //$PATH no definida: usar rutas estándar de Linux
    for (int i = 0; FALLBACK_PATHS[i] != NULL && path_dirs_count < MAX_PATH_DIRS; i++) {
        path_dirs[path_dirs_count] = strdup(FALLBACK_PATHS[i]);
        if (path_dirs[path_dirs_count]) path_dirs_count++;
        }
    } else {
 
        char *path_copy = strdup(path_env);
        if (!path_copy) { perror("executor_init: strdup"); return; }

        char *saveptr = NULL;
        char *token   = strtok_r(path_copy, ":", &saveptr);
        while (token != NULL && path_dirs_count < MAX_PATH_DIRS) {
            path_dirs[path_dirs_count] = strdup(token);
            if (path_dirs[path_dirs_count]) path_dirs_count++;
            token = strtok_r(NULL, ":", &saveptr);
        }
        free(path_copy);
    }
//instalar manejador de SIGCHLD
struct sigaction sa;
sa.sa_handler = sigchld_handler;
sigemptyset(&sa.sa_mask);

sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("executor_init: sigaction SIGCHLD");
    }
}
void executor_cleanup(void)
{
for (int i = 0; i < path_dirs_count; i++) {
    free(path_dirs[i]);
        path_dirs[i] = NULL;
    }
    path_dirs_count = 0;
}

int executor_find_binary(const char *name, char *out_path)
{
if (!name || !out_path) return -1;

//caso especial: ruta explicita (contiene '/')
if (strchr(name, '/') != NULL) {
    if (access(name, X_OK) == 0) {
        strncpy(out_path, name, MAX_BINARY_PATH - 1);
        out_path[MAX_BINARY_PATH - 1] = '\0';
        return 0;
        }
        return -1;
    }

//caso normal: buscar en cada directorio de $PATH
    for (int i = 0; i < path_dirs_count; i++) {
        int n = snprintf(out_path, MAX_BINARY_PATH, "%s/%s", path_dirs[i], name);
        if (n <= 0 || n >= MAX_BINARY_PATH) continue; //ruta larga
        if (access(out_path, X_OK) == 0) return 0; //encontrado
    }
    return -1;
}
static int apply_redirections(const NodeComando *cmd)
{
    /* Redirección de entrada: < archivo */
    if (cmd->input_file != NULL) {
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd == -1) { perror(cmd->input_file); return -1; }
        if (dup2(fd, STDIN_FILENO) == -1) {
            perror("dup2 stdin"); close(fd); return -1;
        }
        close(fd);
    }

    /* Redirección de salida: > archivo (siempre trunca, no hay >> en el parser) */
    if (cmd->output_file != NULL) {
        int banderas;
        
        // Verificamos una bandera.
        if (cmd->output_append) {
            banderas = O_WRONLY | O_CREAT | O_APPEND; // APPEND
        } else {
            banderas = O_WRONLY | O_CREAT | O_TRUNC; // O_TRUNC
        }
        int fd = open(cmd->output_file, banderas, 0644);
        if (fd == -1) { perror(cmd->output_file); return -1; }
        if (dup2(fd, STDOUT_FILENO) == -1) {
            perror("dup2 stdout"); close(fd); return -1;
        }
        close(fd);
    }
    return 0;
}


static int run_single_command(const NodeComando *cmd)
{
    if (!cmd || cmd->argc == 0 || !cmd->args[0]) return -1;

    //exec bloque
    if (strcmp(cmd->args[0], "exec") == 0) {

        // Si solo se escribió "exec" sin argumentos, no hace nada
        if (cmd->argc < 2) return 0; 

        // Buscar el binario del argumento que está DESPUÉS de "exec"
        char binary_path[MAX_BINARY_PATH];
        if (executor_find_binary(cmd->args[1], binary_path) != 0) {
            fprintf(stderr, "ucvsh: %s: comando no encontrado\n", cmd->args[1]);
            return 127;
        }

        // Aplicar las redirecciones directamente al proceso padre (tu shell)
        if (apply_redirections(cmd) != 0) return 1;

        // Ejecutar el nuevo programa pasándole los argumentos desde el índice 1
        // ¡Aquí la shell desaparece y es reemplazada!
        execv(binary_path, &cmd->args[1]);

        // Si execv() retorna, es porque hubo un error fatal
        perror("ucvsh: exec");
        return 127; 
    }
    
    //jobsp bloque
    if(strcmp(cmd->args[0],"jobsp")== 0) {
     fprintf(stderr, "ucvsh: %s: comando no encontrado\n", cmd->args[0]);
     return 127;
    }
    if (strcmp(cmd->args[0], "jobs") == 0 && cmd->argc > 1 && cmd->args[1] != NULL && strcmp(cmd->args[1], "-p") == 0) {
        // Cambiamos temporalmente el nombre del argumento 0 para que run_builtin sepa qué hacer
        free(cmd->args[0]);
        cmd->args[0] = strdup("jobsp");
    }

    //1. verificar si es built-in
    if (is_builtin(cmd->args[0])) {
        return run_builtin((NodeComando *)cmd);
    }
    //2. buscar el binario
    char binary_path[MAX_BINARY_PATH];
    if (executor_find_binary(cmd->args[0], binary_path) != 0) {
        fprintf(stderr, "ucvsh: %s: comando no encontrado\n", cmd->args[0]);
        return 127;  //convención estándar de shell: command not found
    }

//3. fork()
pid_t pid = fork();
    if (pid == -1) { perror("fork"); return -1; }

    if (pid == 0) {
     
    signal(SIGCHLD, SIG_DFL);

    //aplicar redirecciones de archivo antes de execv
    if (apply_redirections(cmd) != 0) exit(1);

    execv(binary_path, cmd->args);
    perror(binary_path);   //execv solo retorna en error
    exit(127);
}

    if (cmd->background) {
        //ELIMINO Y MUESTRO
        delete_DONE_and_Print(g_jobs);
    //background: registrar en jobs y devolver control
        char *cmd_str = CMD_string((NodeComando *)cmd);
        int job_id = add_job(g_jobs, pid, cmd_str ? cmd_str : cmd->args[0], RUNNING);
        free(cmd_str);
        fprintf(stderr, "[%d] %d\n", job_id, pid);
        return 0;
    } else {
    //foreground: esperar al hijo
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid"); return -1;
        }
        if (WIFEXITED(status))   return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
        return -1;
    }
}


static int run_pipeline(NodeComando *first)
{
    if (!first) return -1;

    // 1. Contar cuántos comandos componen el pipeline
    int n = 0;
    NodeComando *cur = first;
    while (cur != NULL) {
        n++;
        if (cur->next_op == OP_PIPE) cur = cur->next;
        else break;
    }

    // Si solo hay un nodo, delegar inmediatamente a comando simple
    if (n == 1) return run_single_command(first);

    int num_pipes = n - 1;

    // Alojamos solo los arreglos estrictamente necesarios
    int *pipe_fds = malloc(num_pipes * 2 * sizeof(int));
    pid_t *child_pids = malloc(n * sizeof(pid_t));
    if (!pipe_fds || !child_pids) {
        perror("run_pipeline: malloc");
        free(pipe_fds); free(child_pids);
        return -1;
    }

    // 2. Inicializar y crear todos los pipes
    for (int i = 0; i < num_pipes; i++) {
        if (pipe(pipe_fds + i * 2) == -1) {
            perror("ucvsh: pipe");
            for (int j = 0; j < i * 2; j++) close(pipe_fds[j]);
            free(pipe_fds); free(child_pids);
            return -1;
        }
    }

    // 3. Crear los procesos hijos recorriendo la lista enlazada
    cur = first;
    int is_background = 0;

    for (int i = 0; i < n; i++) {
        // El estado de background lo determina el último comando de la tubería
        if (i == n - 1) {
            is_background = cur->background;
        }

        // Mapeo preventivo de jobs -p tal como lo hace tu run_single_command
        if (strcmp(cur->args[0], "jobs") == 0 && cur->argc > 1 && cur->args[1] != NULL && strcmp(cur->args[1], "-p") == 0) {
            free(cur->args[0]);
            cur->args[0] = strdup("jobsp");
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("ucvsh: fork pipeline");
            for (int k = 0; k < num_pipes * 2; k++) close(pipe_fds[k]);
            free(pipe_fds); free(child_pids);
            return -1;
        }

        if (pid == 0) {
            // =========================================================
            //                     CÓDIGO DEL HIJO
            // =========================================================
            signal(SIGCHLD, SIG_DFL);

            // Redirección interna de los Pipes
            if (i > 0) {
                dup2(pipe_fds[(i - 1) * 2 + 0], STDIN_FILENO);
            }
            if (i < n - 1) {
                dup2(pipe_fds[i * 2 + 1], STDOUT_FILENO);
            }

            // Cerrar TODOS los extremos de copia en el hijo
            for (int k = 0; k < num_pipes * 2; k++) {
                close(pipe_fds[k]);
            }

            // Liberar memoria dinámica local en el hijo antes del exec
            free(pipe_fds);
            free(child_pids);

            // Aplicar redirecciones de archivos avanzadas (<, >, >>)
            if (apply_redirections(cur) != 0) exit(1);

            // Control de Built-ins en subshell
            if (is_builtin(cur->args[0])) {
                int ret = run_builtin(cur);
                exit(ret);
            }

            // Control del bloque EXEC
            if (strcmp(cur->args[0], "exec") == 0) {
                if (cur->argc < 2) exit(0);
                char binary_path[MAX_BINARY_PATH];
                if (executor_find_binary(cur->args[1], binary_path) != 0) {
                    fprintf(stderr, "ucvsh: %s: comando no encontrado\n", cur->args[1]);
                    exit(127);
                }
                execv(binary_path, &cur->args[1]);
                perror("ucvsh: exec");
                exit(127);
            }

            if (strcmp(cur->args[0], "jobsp") == 0) {
                fprintf(stderr, "ucvsh: %s: comando no encontrado\n", cur->args[0]);
                exit(127);
            }

            // Ejecución de comandos externos estándar (Búsqueda segura en el hijo)
            char binary_path[MAX_BINARY_PATH];
            if (executor_find_binary(cur->args[0], binary_path) != 0) {
                fprintf(stderr, "ucvsh: %s: comando no encontrado\n", cur->args[0]);
                exit(127);
            }

            execv(binary_path, cur->args);
            perror(binary_path);
            exit(127);
        }

        // PADRE: Registra el PID y avanza en la lista enlazada
        child_pids[i] = pid;
        cur = cur->next;
    }

    // 4. El padre cierra todos sus descriptores de pipes abiertos de una sola vez
    for (int k = 0; k < num_pipes * 2; k++) {
        close(pipe_fds[k]);
    }

    int last_exit = 0;

    // 5. MANEJO DE ESPERA (BACKGROUND VS FOREGROUND)
    if (is_background) {
        // Lógica idéntica a tu run_single_command para registrar el Job de fondo
        delete_DONE_and_Print(g_jobs);

        char full_cmd[1024] = {0}; 
        NodeComando *tmp = first;
       while (tmp != NULL) {
        // Concatenar todos los argumentos del comando actual
        for (int i = 0; i < tmp->argc; i++) {
            strcat(full_cmd, tmp->args[i]);
            
            // Agregar espacio entre argumentos, pero no al final del comando
            if (i < tmp->argc - 1) {
                strcat(full_cmd, " ");
            }
        }

        // Si hay un pipe, agregar el separador y continuar al siguiente nodo
        if (tmp->next_op == OP_PIPE) {
            strcat(full_cmd, " | ");
            tmp = tmp->next;
        } else {
            break; // Salir si no hay más pipes
        }
    }
 // agregar
        int job_id = add_job(g_jobs, child_pids[0], full_cmd, RUNNING);


        fprintf(stderr, "[%d] %d\n", job_id, child_pids[0]);
        last_exit = 0;
    } else {
        // Foreground: Esperar de forma ordenada a todos los hijos secuencialmente
      for (int i = 0; i < n; i++) {
    int status;
    if (waitpid(child_pids[i], &status, 0) == -1) {
        // Si el error es ECHILD, significa que el proceso ya fue recolectado por el handler
        if (errno == ECHILD) {
            // No hacemos nada, solo continuamos al siguiente hijo
        } else {
            // Si es otro error (ej. EINTR), reportamos
            perror("waitpid");
        }
        continue; // SALTAMOS al siguiente hijo porque este no se pudo esperar
    }
    
    // Capturar la salida únicamente del último eslabón
    if (i == n - 1) {
        if (WIFEXITED(status))        last_exit = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) last_exit = 128 + WTERMSIG(status);
    }
}
    }

    // Liberación de memoria local en el proceso padre
    free(pipe_fds);
    free(child_pids);

    return last_exit;
}

int executor_run(NodeComando *head)
{
    if (!head) return -1;

    int exit_code  = 0;
    OpType prev_op = OP_NONE;  //operador con el que se llega al nodo actual
    NodeComando *cur = head;

    while (cur != NULL) {

    //decidir si ejecutar según el operador del nodo anterior
    int should_run = 1;
    if (prev_op == OP_AND && exit_code != 0) should_run = 0;
    if (prev_op == OP_OR  && exit_code == 0) should_run = 0;

    if (!should_run) {
        //saltar este nodo. Si hay cadena pipe adjunta, saltarla toda
        OpType op = cur->next_op;
        cur = cur->next;
        while (op == OP_PIPE && cur != NULL) {
            op  = cur->next_op;
            cur = cur->next;
        }
        prev_op = op;
        continue;
        }
    if (cur->next_op == OP_PIPE) {
        exit_code = run_pipeline(cur);
        //avanzar hasta el nodo posterior a la cadena pipe
        while (cur->next_op == OP_PIPE && cur->next != NULL) {
            cur = cur->next;
        }
        prev_op = cur->next_op;
        cur     = cur->next;
    } else {
        exit_code = run_single_command(cur);
        prev_op   = cur->next_op;
        cur       = cur->next;
    }
}
    return exit_code;
}