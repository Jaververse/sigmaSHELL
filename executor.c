#include "executor.h"   //declara la interfaz publica del modulo de ejecucion
#include "builtins.h"   //permite reconocer y ejecutar comandos internos antes de buscar binarios externos
#include "jobs.h"       //permite registrar, actualizar e imprimir trabajos en segundo plano

#include <unistd.h>      //provee fork(), execv(), dup2(), close(), access() y constantes STDIN/STDOUT
#include <sys/types.h>   //define pid_t y tipos POSIX usados por fork/wait
#include <sys/wait.h>    //provee waitpid() y macros WIFEXITED/WEXITSTATUS/WIFSIGNALED
#include <fcntl.h>       //provee open() y banderas O_RDONLY, O_CREAT, O_TRUNC y O_APPEND
#include <signal.h>      //provee sigaction(), sigprocmask() y constantes de senales
#include <stdio.h>       //provee perror(), fprintf(), snprintf() y stderr
#include <stdlib.h>      //provee malloc(), calloc(), free(), exit() y getenv()
#include <string.h>      //provee strdup(), strchr(), strncpy() y strtok_r()
#include <errno.h>       //se mantiene disponible para diagnosticos POSIX si se amplian errores del executor

static char *path_dirs[MAX_PATH_DIRS];    //cache de directorios tomados de PATH para no parsear PATH en cada comando
static int path_dirs_count = 0;           //cantidad real de entradas validas guardadas en path_dirs
static JobTable *g_jobs = NULL;           //referencia a la tabla global de jobs entregada desde main.c

static const char *FALLBACK_PATHS[] = {   //rutas usadas si la variable PATH no existe o esta vacia
    "/bin", "/usr/bin", "/sbin", "/usr/sbin", "/usr/local/bin", NULL
};

static void sigchld_handler(int signo)
{
    (void)signo;                          //evita advertencias porque el manejador no necesita leer el numero de senal

    pid_t pid;                            //PID del hijo que waitpid() logra recolectar sin bloquear la shell
    int status;                           //estado de salida recibido desde el kernel para cada hijo terminado

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) { //recolecta hijos terminados sin detener el prompt
        if (g_jobs == NULL) {             //si no existe tabla de jobs, no hay estructura donde marcar el proceso
            continue;
        }

        Job *job = g_jobs->head;          //comienza la busqueda desde el primer job registrado
        while (job != NULL) {             //recorre la lista enlazada hasta encontrar el PID asociado al job
            if (job->pgid == pid) {       //en este proyecto pgid se usa como identificador del proceso lider
                if (WIFEXITED(status) || WIFSIGNALED(status)) { //solo se marca terminado si realmente finalizo
                    job->state = DONE;    //la limpieza definitiva se hace luego desde jobs/builtins, no dentro del handler
                }
                break;                    //el PID ya fue encontrado, por lo que no se sigue recorriendo la lista
            }
            job = job->next;              //avanza al siguiente trabajo registrado
        }
    }
}

void executor_init(JobTable *table)
{
    executor_cleanup();                   //limpia un cache anterior si la funcion se invoca mas de una vez accidentalmente
    g_jobs = table;                       //guarda la tabla de jobs para que SIGCHLD pueda marcar procesos background

    const char *path_env = getenv("PATH"); //lee PATH del ambiente para localizar comandos externos

    if (path_env == NULL || path_env[0] == '\0') { //si PATH no existe, se usan rutas comunes del sistema
        for (int i = 0; FALLBACK_PATHS[i] != NULL && path_dirs_count < MAX_PATH_DIRS; i++) { //copia rutas fallback
            path_dirs[path_dirs_count] = strdup(FALLBACK_PATHS[i]); //duplica la cadena porque el cache se libera luego
            if (path_dirs[path_dirs_count] != NULL) { //solo cuenta entradas copiadas correctamente
                path_dirs_count++;
            }
        }
    } else {                              //si PATH existe, se tokeniza por ':' para obtener sus directorios
        char *path_copy = strdup(path_env); //strtok_r modifica la cadena, por eso se trabaja con una copia
        if (path_copy == NULL) {          //si no hay memoria, el executor queda sin cache pero la shell no se cae
            perror("executor_init: strdup");
            return;
        }

        char *saveptr = NULL;             //estado interno requerido por strtok_r para tokenizar de forma segura
        char *token = strtok_r(path_copy, ":", &saveptr); //obtiene el primer directorio de PATH
        while (token != NULL && path_dirs_count < MAX_PATH_DIRS) { //recorre PATH hasta agotar tokens o espacio
            const char *dir = (token[0] == '\0') ? "." : token; //entrada vacia equivale al directorio actual
            path_dirs[path_dirs_count] = strdup(dir); //duplica cada directorio para mantenerlo en el cache privado
            if (path_dirs[path_dirs_count] != NULL) { //solo incrementa si la copia fue exitosa
                path_dirs_count++;
            }
            token = strtok_r(NULL, ":", &saveptr); //obtiene el siguiente directorio
        }
        free(path_copy);                  //libera la copia temporal porque ya se guardaron las entradas necesarias
    }

    struct sigaction sa;                  //estructura POSIX para instalar el manejador de SIGCHLD
    sa.sa_handler = sigchld_handler;      //asocia SIGCHLD con el manejador que recolecta hijos background
    sigemptyset(&sa.sa_mask);             //no se bloquean senales adicionales durante el handler
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; //reinicia syscalls interrumpidas e ignora hijos solo detenidos

    if (sigaction(SIGCHLD, &sa, NULL) == -1) { //registra el manejador y reporta error si el sistema lo rechaza
        perror("executor_init: sigaction SIGCHLD");
    }
}

void executor_cleanup(void)
{
    for (int i = 0; i < path_dirs_count; i++) { //recorre cada ruta duplicada durante executor_init()
        free(path_dirs[i]);                  //libera la memoria reservada por strdup()
        path_dirs[i] = NULL;                 //evita punteros colgantes si luego se vuelve a inicializar
    }
    path_dirs_count = 0;                     //reinicia el contador para dejar el modulo en estado limpio
}

int executor_find_binary(const char *name, char *out_path)
{
    if (name == NULL || out_path == NULL) {  //valida argumentos obligatorios antes de usar punteros
        return -1;
    }

    if (strchr(name, '/') != NULL) {         //si contiene '/', se interpreta como ruta explicita o relativa
        if (access(name, X_OK) == 0) {       //comprueba que exista y tenga permiso de ejecucion
            strncpy(out_path, name, MAX_BINARY_PATH - 1); //copia la ruta al buffer del llamador
            out_path[MAX_BINARY_PATH - 1] = '\0'; //garantiza terminacion nula aunque la ruta sea larga
            return 0;                        //Exito: el binario se puede ejecutar con execv()
        }
        return -1;                           //Fallo: la ruta explicita no existe o no es ejecutable
    }

    for (int i = 0; i < path_dirs_count; i++) { //busca el nombre del comando en cada directorio cacheado de PATH
        int n = snprintf(out_path, MAX_BINARY_PATH, "%s/%s", path_dirs[i], name); //construye ruta candidata
        if (n <= 0 || n >= MAX_BINARY_PATH) { //descarta rutas truncadas o errores de formato
            continue;
        }
        if (access(out_path, X_OK) == 0) {   //si la ruta candidata es ejecutable, termina la busqueda
            return 0;
        }
    }

    return -1;                               //no se encontro ningun ejecutable compatible en PATH
}

static int apply_redirections(const NodeComando *cmd)
{
    if (cmd->input_file != NULL) {           //redireccion de entrada: comando < archivo
        int fd = open(cmd->input_file, O_RDONLY); //abre el archivo solo para lectura
        if (fd == -1) {                      //si open falla, el comando no debe ejecutarse
            perror(cmd->input_file);
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) == -1) {  //reemplaza stdin por el archivo abierto
            perror("dup2 stdin");
            close(fd);
            return -1;
        }
        close(fd);                           //cierra el descriptor original porque stdin ya apunta al archivo
    }

    if (cmd->output_file != NULL) {          //redireccion de salida: comando > archivo o comando >> archivo
        int flags = O_WRONLY | O_CREAT;      //siempre se escribe y se crea el archivo si no existe
        flags |= cmd->output_append ? O_APPEND : O_TRUNC; //decide entre anexar o truncar segun el parser

        int fd = open(cmd->output_file, flags, 0644); //abre el destino con permisos rw-r--r-- si se crea
        if (fd == -1) {                      //si no se puede abrir el archivo, no se ejecuta el comando
            perror(cmd->output_file);
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) == -1) { //reemplaza stdout por el archivo de salida
            perror("dup2 stdout");
            close(fd);
            return -1;
        }
        close(fd);                           //cierra el descriptor original porque stdout ya apunta al archivo
    }

    return 0;                                //Exito: las redirecciones quedaron aplicadas en el proceso actual
}

static int run_builtin_with_redirections(const NodeComando *cmd)
{
    int saved_stdin = -1;                    //copia de seguridad de stdin cuando hay redireccion de entrada
    int saved_stdout = -1;                   //copia de seguridad de stdout cuando hay redireccion de salida
    int result = 1;                          //valor por defecto si ocurre un error antes de ejecutar el builtin

    if (cmd->input_file != NULL) {           //solo se duplica stdin si el comando realmente redirige entrada
        saved_stdin = dup(STDIN_FILENO);     //guarda stdin para poder restaurarlo despues del builtin
        if (saved_stdin == -1) {             //si no se puede duplicar, no es seguro modificar stdin
            perror("dup stdin");
            return 1;
        }
    }

    if (cmd->output_file != NULL) {          //solo se duplica stdout si el comando realmente redirige salida
        saved_stdout = dup(STDOUT_FILENO);   //guarda stdout para que el prompt vuelva a la terminal despues
        if (saved_stdout == -1) {            //si falla, se restaura lo que ya se haya duplicado
            perror("dup stdout");
            if (saved_stdin != -1) close(saved_stdin);
            return 1;
        }
    }

    if (apply_redirections(cmd) == 0) {      //aplica redirecciones en el proceso padre solo durante el builtin
        result = run_builtin((NodeComando *)cmd); //ejecuta el builtin en el padre para que cd/exit/jobs afecten la shell
        fflush(stdout);                      //fuerza escritura pendiente antes de restaurar stdout
        fflush(stderr);                      //fuerza diagnosticos pendientes antes de restaurar descriptores
    }

    if (saved_stdin != -1) {                 //si stdin fue guardado, se devuelve a su descriptor original
        if (dup2(saved_stdin, STDIN_FILENO) == -1) perror("restore stdin");
        close(saved_stdin);
    }

    if (saved_stdout != -1) {                //si stdout fue guardado, se devuelve a la terminal original
        if (dup2(saved_stdout, STDOUT_FILENO) == -1) perror("restore stdout");
        close(saved_stdout);
    }

    return result;                           //retorna el codigo real del builtin o 1 si fallo la preparacion
}

static int run_single_command(const NodeComando *cmd)
{
    if (cmd == NULL || cmd->argc == 0 || cmd->args[0] == NULL) { //evita ejecutar nodos vacios generados por entradas incompletas
        return -1;
    }

    if (is_builtin(cmd->args[0])) {          //los built-ins se ejecutan en el padre para modificar estado interno
        return run_builtin_with_redirections(cmd); //ademas permite redireccionar built-ins como pwd > archivo
    }

    char binary_path[MAX_BINARY_PATH];       //uffer local para guardar la ruta absoluta o relativa ejecutable
    if (executor_find_binary(cmd->args[0], binary_path) != 0) { //busca el comando externo antes de crear el hijo
        fprintf(stderr, "ucvsh: %s: comando no encontrado\n", cmd->args[0]);
        return 127;                         //codigo convencional de shell para command not found
    }

    sigset_t old_mask;                       //mascara original de senales antes de bloquear SIGCHLD
    if (block_sigchld(&old_mask) == -1) {    //evita que el handler recolecte el hijo antes del waitpid foreground
        perror("sigprocmask SIG_BLOCK");
        return -1;
    }

    pid_t pid = fork();                      //crea un hijo para ejecutar el binario externo
    if (pid == -1) {                         //si fork falla, se restaura la mascara y se reporta el error
        perror("fork");
        restore_signal_mask(&old_mask);
        return -1;
    }

    if (pid == 0) {                          //rama del proceso hijo
        reset_child_signals(&old_mask);      //restaura senales para que el programa externo se comporte normal
        if (apply_redirections(cmd) != 0) {  //aplica <, > o >> antes de reemplazar el proceso con execv()
            exit(1);
        }
        execv(binary_path, cmd->args);       //reemplaza el hijo por el programa externo encontrado
        perror(binary_path);                 //execv solo retorna si ocurrio error
        exit(127);                           //mantiene la convencion de fallo de ejecucion/comando
    }

    if (cmd->background) {                   //si el parser marco &, el padre no espera al hijo
        delete_DONE_and_Print(g_jobs);       //antes de mostrar el nuevo job, limpia y reporta trabajos finalizados
        char *cmd_str = CMD_string((NodeComando *)cmd); //reconstruye el comando para imprimirlo desde jobs
        int job_id = add_job(g_jobs, pid, cmd_str ? cmd_str : cmd->args[0], RUNNING); //registra PID y texto del job
        free(cmd_str);                       //libera la cadena reconstruida por CMD_string()
        fprintf(stderr, "[%d] %d\n", job_id, pid); //informa al usuario el id de job y PID del proceso
        restore_signal_mask(&old_mask);      //desbloquea SIGCHLD despues de registrar el job para evitar carreras
        return 0;                            //el comando background se considera lanzado correctamente
    }

    int status;                              //estado del hijo foreground al finalizar
    if (waitpid(pid, &status, 0) == -1) {    //espera exactamente al hijo creado por este comando
        perror("waitpid");
        restore_signal_mask(&old_mask);
        return -1;
    }

    restore_signal_mask(&old_mask);          //reactiva el manejo normal de SIGCHLD despues del wait foreground

    if (WIFEXITED(status)) {                 //si el hijo termino con exit(), se devuelve su codigo real
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {               //si el hijo murio por senal, se usa la convencion 128 + senal
        return 128 + WTERMSIG(status);
    }

    return -1;                               //Caso no esperado: el proceso no salio de forma interpretable
}

static int run_pipeline(NodeComando *first)
{
    int n = 0;                               //cantidad de comandos conectados por pipes
    NodeComando *cur = first;                //cursor temporal para contar la cadena de pipeline

    while (cur != NULL) {                    //recorre nodos hasta que el siguiente operador ya no sea pipe
        n++;
        if (cur->next_op == OP_PIPE) {       //si hay pipe, el siguiente nodo pertenece a la misma pipeline
            cur = cur->next;
        } else {                             //si no hay pipe, la pipeline termina en el nodo actual
            break;
        }
    }

    if (n == 1) {                            //con un solo comando no se necesita infraestructura de pipes
        return run_single_command(first);
    }

    int num_pipes = n - 1;                   //entre n comandos siempre hay n-1 pipes

    NodeComando **nodes = malloc((size_t)n * sizeof(NodeComando *)); //arreglo para acceder por indice a cada comando
    if (nodes == NULL) {                     //sin este arreglo no se puede configurar la pipeline ordenadamente
        perror("run_pipeline: malloc nodes");
        return -1;
    }

    int *pipe_fds = malloc((size_t)num_pipes * 2 * sizeof(int)); //guarda lectura/escritura de cada pipe
    if (pipe_fds == NULL) {                  //si falla, se libera lo ya reservado
        perror("run_pipeline: malloc pipe_fds");
        free(nodes);
        return -1;
    }

    pid_t *child_pids = malloc((size_t)n * sizeof(pid_t)); //guarda los PID para esperar a todos los hijos
    if (child_pids == NULL) {                //si falla, se liberan arreglos anteriores
        perror("run_pipeline: malloc child_pids");
        free(pipe_fds);
        free(nodes);
        return -1;
    }

    char (*binary_paths)[MAX_BINARY_PATH] = calloc((size_t)n, sizeof(*binary_paths)); //rutas de externos por comando
    if (binary_paths == NULL) {              //si falla, no se puede preparar execv() de forma segura
        perror("run_pipeline: calloc binary_paths");
        free(child_pids);
        free(pipe_fds);
        free(nodes);
        return -1;
    }

    cur = first;                             //reinicia el cursor para llenar nodes[]
    for (int i = 0; i < n; i++) {            //copia cada nodo de la pipeline en el arreglo indexado
        nodes[i] = cur;
        if (cur->next_op == OP_PIPE) {       //avanza solo mientras el operador conecta con otro comando
            cur = cur->next;
        }
    }

    for (int i = 0; i < n; i++) {            //valida todos los comandos antes de crear cualquier proceso
        if (nodes[i] == NULL || nodes[i]->argc == 0 || nodes[i]->args[0] == NULL) {
            fprintf(stderr, "ucvsh: pipeline incompleta\n");
            free(binary_paths);
            free(child_pids);
            free(pipe_fds);
            free(nodes);
            return -1;
        }
        if (!is_builtin(nodes[i]->args[0]) && executor_find_binary(nodes[i]->args[0], binary_paths[i]) != 0) {
            fprintf(stderr, "ucvsh: %s: comando no encontrado\n", nodes[i]->args[0]);
            free(binary_paths);
            free(child_pids);
            free(pipe_fds);
            free(nodes);
            return 127;
        }
    }

    for (int i = 0; i < num_pipes; i++) {    //inicializa descriptores en -1 para facilitar cierres seguros
        pipe_fds[i * 2 + 0] = -1;
        pipe_fds[i * 2 + 1] = -1;
    }

    for (int i = 0; i < num_pipes; i++) {    //crea todos los pipes antes de crear hijos
        int fds[2];                          //fds[0] es lectura y fds[1] es escritura
        if (pipe(fds) == -1) {               //si un pipe falla, se cierran los ya creados y se aborta
            perror("pipe");
            for (int j = 0; j < i; j++) {    //recorre solo los pipes que si fueron creados
                close(pipe_fds[j * 2 + 0]);
                close(pipe_fds[j * 2 + 1]);
            }
            free(binary_paths);
            free(child_pids);
            free(pipe_fds);
            free(nodes);
            return -1;
        }
        pipe_fds[i * 2 + 0] = fds[0];        //guarda extremo de lectura del pipe i
        pipe_fds[i * 2 + 1] = fds[1];        //guarda extremo de escritura del pipe i
    }

    sigset_t old_mask;                       //mascara previa a bloquear SIGCHLD durante forks y waits de pipeline
    if (block_sigchld(&old_mask) == -1) {    //evita carreras entre el handler y los waitpid de la pipeline foreground
        perror("sigprocmask SIG_BLOCK");
        for (int k = 0; k < num_pipes; k++) {
            close(pipe_fds[k * 2 + 0]);
            close(pipe_fds[k * 2 + 1]);
        }
        free(binary_paths);
        free(child_pids);
        free(pipe_fds);
        free(nodes);
        return -1;
    }

    for (int i = 0; i < n; i++) {            //crea un proceso hijo por cada segmento de la pipeline
        NodeComando *cmd = nodes[i];         //comando que ejecutara el hijo i
        pid_t pid = fork();                  //crea el hijo para este segmento

        if (pid == -1) {                     //si fork falla, se cierran recursos y se aborta la pipeline
            perror("fork pipeline");
            for (int k = 0; k < num_pipes; k++) {
                close(pipe_fds[k * 2 + 0]);
                close(pipe_fds[k * 2 + 1]);
            }
            restore_signal_mask(&old_mask);
            free(binary_paths);
            free(child_pids);
            free(pipe_fds);
            free(nodes);
            return -1;
        }

        if (pid == 0) {                      //rama del hijo encargado de un comando de la pipeline
            reset_child_signals(&old_mask);  //restaura senales para que el comando no use handlers de la shell

            if (i > 0) {                     //todo comando excepto el primero lee del pipe anterior
                if (dup2(pipe_fds[(i - 1) * 2 + 0], STDIN_FILENO) == -1) {
                    perror("dup2 stdin pipe");
                    exit(1);
                }
            }

            if (i < n - 1) {                 //todo comando excepto el ultimo escribe al pipe siguiente
                if (dup2(pipe_fds[i * 2 + 1], STDOUT_FILENO) == -1) {
                    perror("dup2 stdout pipe");
                    exit(1);
                }
            }

            for (int k = 0; k < num_pipes; k++) { //tras dup2, el hijo cierra todos los extremos originales
                close(pipe_fds[k * 2 + 0]);
                close(pipe_fds[k * 2 + 1]);
            }

            if (apply_redirections(cmd) != 0) { //aplica redirecciones propias del segmento despues de conectar pipes
                exit(1);
            }

            if (is_builtin(cmd->args[0])) {  //built-ins dentro de pipeline se ejecutan en el hijo como en una subshell
                int code = run_builtin(cmd); //esto permite casos como history | grep texto sin modificar el padre
                exit(code);
            }

            execv(binary_paths[i], cmd->args); //ejecuta el binario externo correspondiente al segmento
            perror(binary_paths[i]);        //si execv retorna, hubo error
            exit(127);
        }

        child_pids[i] = pid;                 //el padre registra el PID para esperarlo al final
    }

    for (int k = 0; k < num_pipes; k++) {    //el padre cierra todos los pipes porque solo los hijos los usan
        close(pipe_fds[k * 2 + 0]);
        close(pipe_fds[k * 2 + 1]);
    }

    int last_exit = 0;                       //codigo de salida que devuelve la pipeline: el del ultimo comando
    for (int i = 0; i < n; i++) {            //espera todos los hijos para evitar procesos zombies
        int status;                          //estado individual del hijo i
        if (waitpid(child_pids[i], &status, 0) == -1) {
            perror("waitpid pipeline");
            continue;
        }
        if (i == n - 1) {                    //solo el ultimo comando determina el resultado logico de la pipeline
            if (WIFEXITED(status)) {
                last_exit = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                last_exit = 128 + WTERMSIG(status);
            }
        }
    }

    restore_signal_mask(&old_mask);          //reactiva SIGCHLD cuando ya se recolectaron los hijos foreground
    free(binary_paths);                      //libera rutas preparadas para comandos externos
    free(child_pids);                        //libera arreglo de PIDs
    free(pipe_fds);                          //libera arreglo de descriptores
    free(nodes);                             //libera arreglo de nodos indexados

    return last_exit;                        //devuelve el estado del ultimo comando para && y || posteriores
}

int executor_run(NodeComando *head)
{
    if (head == NULL) {                      //sin lista parseada no hay nada que ejecutar
        return -1;
    }

    int exit_code = 0;                       //estado del comando o pipeline ejecutado mas recientemente
    OpType prev_op = OP_NONE;                //operador que conecta el resultado anterior con el nodo actual
    NodeComando *cur = head;                 //cursor sobre la lista enlazada de comandos

    while (cur != NULL) {                    //recorre toda la lista respetando operadores de control
        int should_run = 1;                  //por defecto cada nodo se ejecuta salvo que && u || indiquen saltarlo

        if (prev_op == OP_AND && exit_code != 0) { //en A && B, B solo corre si A fue exitoso
            should_run = 0;
        }
        if (prev_op == OP_OR && exit_code == 0) {  //en A || B, B solo corre si A fallo
            should_run = 0;
        }

        if (!should_run) {                   //si el operador logico decide saltar, se avanza sin ejecutar
            OpType op = cur->next_op;        //guarda el operador de salida del nodo que se va a saltar
            cur = cur->next;                 //pasa al siguiente nodo
            while (op == OP_PIPE && cur != NULL) { //si se salta una pipeline, se salta completa
                op = cur->next_op;
                cur = cur->next;
            }
            prev_op = op;                    //conserva el operador que conectara con el proximo nodo ejecutable
            continue;
        }

        if (cur->next_op == OP_PIPE) {       //si el nodo inicia una pipeline, se ejecuta como unidad
            exit_code = run_pipeline(cur);   //ejecuta todos los segmentos conectados por |
            while (cur->next_op == OP_PIPE && cur->next != NULL) { //avanza hasta el ultimo nodo de la pipeline
                cur = cur->next;
            }
            prev_op = cur->next_op;          //el operador posterior a la pipeline controla el siguiente paso
            cur = cur->next;                 //continua despues de la pipeline completa
        } else {                             //si no hay pipe, se ejecuta como comando individual
            exit_code = run_single_command(cur); //ejecuta builtin o comando externo con redirecciones/background
            prev_op = cur->next_op;          //guarda el operador que conectara con el siguiente comando
            cur = cur->next;                 //avanza al siguiente nodo de la lista
        }
    }

    return exit_code;                        //devuelve el estado final de la ultima ejecucion realizada
}
