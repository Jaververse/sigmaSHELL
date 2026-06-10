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

//rutas estandar de Linux usadas si $PATH no está definida
static const char *FALLBACK_PATHS[] = {
    "/bin", "/usr/bin", "/sbin", "/usr/sbin", "/usr/local/bin", NULL
};


static void sigchld_handler(int signo)
{
(void)signo;
pid_t pid;
int   status;

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

void executor_init(JobTable *table)
{
//guardar referencia a la tabla de jobs para usarla en sigchld_handler
g_jobs = table;

//parsear $PATH y cachear los directorios
const char *path_env = getenv("PATH");

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
//run_pipeline() Pipeline dinámica
static int run_pipeline(NodeComando *first)
{
//contar nodos
int n = 0;
NodeComando *cur = first;
while (cur != NULL) {
    n++;
    if (cur->next_op == OP_PIPE) {
        cur = cur->next;
    } else {
        break;
        }
    }

//si solo hay un nodo, no hay pipes: ejecutar como comando simple
    if (n == 1) return run_single_command(first);

    int num_pipes = n - 1;

//alojar los tres arreglos dinámicamente con malloc.
    NodeComando **nodes = malloc(n * sizeof(NodeComando *));
    if (!nodes) {
        perror("run_pipeline: malloc nodes");
        return -1;
    }
int *pipe_fds = malloc(num_pipes * 2 * sizeof(int));
    if (!pipe_fds) {
        perror("run_pipeline: malloc pipe_fds");
        free(nodes);
        return -1;
    }
    pid_t *child_pids = malloc(n * sizeof(pid_t));
    if (!child_pids) {
        perror("run_pipeline: malloc child_pids");
        free(pipe_fds);
        free(nodes);
        return -1;
    }
//rellenar nodes[] recorriendo la lista de nuevo
    cur = first;
    for (int i = 0; i < n; i++) {
        nodes[i] = cur;
        if (cur->next_op == OP_PIPE) cur = cur->next;
    }

//inicializar e crear todos los pipes ANTES del primer fork
for (int i = 0; i < num_pipes; i++) {
    pipe_fds[i*2+0] = -1;
    pipe_fds[i*2+1] = -1;
    }
for (int i = 0; i < num_pipes; i++) {
    int fds[2];
    if (pipe(fds) == -1) {
        perror("pipe");
        //cerrar los pipes ya creados antes de abortar
        for (int j = 0; j < i; j++) {
            close(pipe_fds[j*2+0]);
            close(pipe_fds[j*2+1]);
        }
        free(child_pids);
        free(pipe_fds);
        free(nodes);
        return -1;
        }
        pipe_fds[i*2+0] = fds[0];
        pipe_fds[i*2+1] = fds[1];
    }
    //crear un proceso hijo por cada comando de la pipeline.
    for (int i = 0; i < n; i++) {
        NodeComando *cmd = nodes[i];

    //Buscar el binario de este segmento en $PATH
    char bin[MAX_BINARY_PATH];
    if (executor_find_binary(cmd->args[0], bin) != 0) {
        fprintf(stderr, "ucvsh: %s: comando no encontrado\n", cmd->args[0]);
        for (int k = 0; k < num_pipes; k++) {
            close(pipe_fds[k*2+0]);
            close(pipe_fds[k*2+1]);
        }
        free(child_pids);
        free(pipe_fds);
        free(nodes);
        return 127;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork pipeline");
        for (int k = 0; k < num_pipes; k++) {
            close(pipe_fds[k*2+0]);
            close(pipe_fds[k*2+1]);
        }
        free(child_pids);
        free(pipe_fds);
        free(nodes);
        return -1;
    }
    if (pid == 0) {
        //proceso hijo i
        signal(SIGCHLD, SIG_DFL);

    //conectar stdin al extremo de lectura del pipe anterior
    //Solo para los comandos que no son el primero (i > 0)
    if (i > 0) {
        if (dup2(pipe_fds[(i-1)*2+0], STDIN_FILENO) == -1) {
            perror("dup2 stdin pipe"); exit(1);
            }
        }

        //conectar stdout al extremo de escritura del pipe siguiente
        //solo para los comandos que no son el último (i < n-1)
        if (i < n - 1) {
            if (dup2(pipe_fds[i*2+1], STDOUT_FILENO) == -1) {
                perror("dup2 stdout pipe"); exit(1);
            }
        }
        //CRÍTICO: cerrar TODOS los extremos de TODOS los pipes
        for (int k = 0; k < num_pipes; k++) {
            close(pipe_fds[k*2+0]);
            close(pipe_fds[k*2+1]);
        }
        //liberar memoria dinámica antes de execv
        free(child_pids);
        free(pipe_fds);
        free(nodes);

        //aplicar redirecciones de archivo si las hay
            if (apply_redirections(cmd) != 0) exit(1);

            execv(bin, cmd->args);
            perror(bin);
            exit(127);
    }
    //PADRE: registrar PID del hijo y cerrar los extremos del pipe anterior que ya no necesita
        child_pids[i] = pid;
        if (i > 0) {
            close(pipe_fds[(i-1)*2+0]);
            close(pipe_fds[(i-1)*2+1]);
        }
    }
    //cerrar el último pipe que quedó pendiente tras el bucle
    if (num_pipes > 0) {
        close(pipe_fds[(num_pipes-1)*2+0]);
        close(pipe_fds[(num_pipes-1)*2+1]);
    }
    //esperar a TODOS los hijos de la pipeline.
    int last_exit = 0;
    for (int i = 0; i < n; i++) {
        int status;
        waitpid(child_pids[i], &status, 0);
        if (i == n - 1) {
            if (WIFEXITED(status))        last_exit = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) last_exit = 128 + WTERMSIG(status);
        }
    }

    //liberar memoria dinámica antes de retornar
    free(child_pids);
    free(pipe_fds);
    free(nodes);

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
