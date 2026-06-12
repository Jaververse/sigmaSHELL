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


//arreglo estatico que actua como cache para guardar los directorios extraidos de la variable $PATH
static char *path_dirs[MAX_PATH_DIRS];
//contador global que lleva la cantidad de directorios cacheados validos
static int   path_dirs_count = 0;

//puntero estatico a la tabla de jobs global que permite a executor.c gestionar los procesos en background
static JobTable *g_jobs = NULL;

//funcion que valida que la sintaxis de la lista de comandos generada por el parser sea correcta antes de ejecutarla
int validarsyntax(NodeComando *head) {
//puntero auxiliar para recorrer la lista enlazada de comandos sin perder la referencia al inicio   
    NodeComando *cur = head;
//bucle para iterar sobre todos los nodos de comando hasta llegar al final de la lista
while (cur != NULL) {
//verifica si un comando se envia a background ('&') pero el siguiente operador intenta hacer un pipe ('|'), lo cual es inválido
    if (cur->background && cur->next_op == OP_PIPE) {
//imprime un error de sintaxis en la salida estándar de errores (stderr)            
    fprintf(stderr, "ucvsh: error sintáctico cerca del elemento inesperado `|'\n");
//retorna -1 indicando que la validacion fallo y la ejecucion debe abortarse
    return -1;
        }
//avanza al siguiente comando en la lista enlazada       
    cur = cur->next;
    }
//la cadena de comandos tiene una sintaxis valida
    return 0;
}
//arreglo de cadenas constante que define rutas de respaldo en caso de que la variable de entorno $PATH no exista
static const char *FALLBACK_PATHS[] = {
    "/bin", "/usr/bin", "/sbin", "/usr/sbin", "/usr/local/bin", NULL
};

//manejador asincrono de seniales para SIGCHLD, se ejecuta automaticamente cuando un proceso hijo cambia de estado (termina o se suspende)
static void sigchld_handler(int signo)
{
//silencia la advertencia del compilador sobre el parametro 'signo' no utilizado
(void)signo;
//variable para almacenar el PID del proceso hijo que genero la señal
pid_t pid;
//variable para almacenar el estado de salida o terminacion del proceso hijo
int   status;

//bucle que usa waitpid con WNOHANG para recolectar procesos "zombies" de forma no bloqueante (retorna > 0 si hay un hijo terminado)
while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
//si la tabla de jobs aun no esta inicializada, salta a la siguiente iteracion para evitar fallos de segmentacion    
    if (g_jobs == NULL) continue;
//puntero para recorrer la tabla de jobs enlazada en busqueda del proceso que acaba de terminar
    Job *j = g_jobs->head;
//bucle para iterar sobre todos los trabajos registrados en la shell
while (j != NULL) {
//si el identificador de grupo de procesos del job coincide con el PID del hijo terminado...    
    if (j->pgid == pid) {
//verifica si el hijo termino normalmente (exited) o fue asesinado por una señal (signaled)       
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
//actualiza el estado del job en la tabla a "DONE" para que sea notificado y limpiado posteriormente       
        j->state = DONE;
        }
        break;
        }
//si no es este job, avanza al siguiente nodo en la lista de trabajos        
        j = j->next;
    }
    }
}

//funcion de configuracion inicial del ejecutor, llamada una sola vez desde main.c antes de arrancar el bucle principal
void executor_init(JobTable *table)
{
//asigna el puntero de la tabla global pasada desde main a la variable estatica local para uso del handler y procesos
g_jobs = table;

//llama a getenv para obtener el valor actual de la variable de entorno PATH del sistema operativo
const char *path_env = getenv("PATH");

//comprueba si $PATH no esta definida o es una cadena vacia
if (path_env == NULL || path_env[0] == '\0') {
//bucle para iterar sobre las rutas de respaldo (FALLBACK_PATHS) predefinidas en Linux
    for (int i = 0; FALLBACK_PATHS[i] != NULL && path_dirs_count < MAX_PATH_DIRS; i++) {
//duplica la cadena de la ruta de respaldo y la almacena en el cache de directorios del executor       
        path_dirs[path_dirs_count] = strdup(FALLBACK_PATHS[i]);
//si la duplicacion fue exitosa, incrementa el contador de directorios cacheados        
        if (path_dirs[path_dirs_count]) path_dirs_count++;
        }
} else {
//crea una copia dinamica de la cadena $PATH porque la funcion strtok modifica la cadena original al separar por tokens
    char *path_copy = strdup(path_env);
//si falla la asignacion de memoria para la copia, imprime un error y aborta la inicializacion de rutas    
    if (!path_copy) { perror("executor_init: strdup"); return; }

//puntero utilizado internamente por strtok_r para recordar el contexto de la separacion (thread-safe)
    char *saveptr = NULL;
//extrae el primer directorio de la variable $PATH, usando los dos puntos ':' como delimitador
    char *token   = strtok_r(path_copy, ":", &saveptr);
//bucle para procesar cada directorio extraido mientras queden tokens y no se exceda el maximo permitido
    while (token != NULL && path_dirs_count < MAX_PATH_DIRS) {
//duplica el directorio extraido y lo guarda en el cache estatico para busquedas futuras        
        path_dirs[path_dirs_count] = strdup(token);
//si la duplicacion fue exitosa, incrementa el contador
        if (path_dirs[path_dirs_count]) path_dirs_count++;
// Obtiene el siguiente directorio de la copia de $PATH
        token = strtok_r(NULL, ":", &saveptr);
    }
//libera la memoria temporal de la copia de $PATH que ya fue tokenizada
    free(path_copy);
    }

//declara una estructura sigaction para configurar de forma robusta el manejo de la senial SIGCHLD
struct sigaction sa;
//asigna el puntero a la funcion sigchld_handler para que sea ejecutada cuando llegue la senial
sa.sa_handler = sigchld_handler;

//inicializa la mascara de seniales para que ninguna otra senial sea bloqueada mientras se ejecuta el handler
sigemptyset(&sa.sa_mask);

//configura las banderas: SA_RESTART para reiniciar llamadas interrumpidas, SA_NOCLDSTOP ignora cuando los hijos se detienen/continuan (solo terminaciones)
sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

//aplica la configuracion de la senial SIGCHLD al sistema; si falla (retorna -1), maneja el error
if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("executor_init: sigaction SIGCHLD");
    }
}

//funcion para liberar la memoria del cache de directorios antes de que la shell termine su ejecucion
void executor_cleanup(void)
{
//bucle para iterar sobre todos los directorios cacheados
for (int i = 0; i < path_dirs_count; i++) {
    
//libera la memoria dinamica asignada por strdup en executor_init   
    free(path_dirs[i]);
//asigna NULL al puntero liberado por seguridad para evitar "dangling pointers"
    path_dirs[i] = NULL;
    }
//reinicia el contador de directorios a 0
    path_dirs_count = 0;
}

//busca si un comando existe y es ejecutable, resolviendo su ruta completa (ejm: "ls" -> "/usr/bin/ls")
int executor_find_binary(const char *name, char *out_path)
{
//si el nombre o el buffer de salida son nulos, aborta retornando -1
if (!name || !out_path) return -1;

//comprueba si el comando incluye un '/' explicito, lo que significa que el usuario paso una ruta relativa o absoluta (ej: ./script)
if (strchr(name, '/') != NULL) {

//usa la funcion access() con X_OK para verificar si el archivo existe y tiene permisos de ejecucion en esa ruta exacta
    if (access(name, X_OK) == 0) {

//copia la ruta proporcionada al buffer de salida de forma segura, respetando el limite maximo       
        strncpy(out_path, name, MAX_BINARY_PATH - 1);

//garantiza que la cadena copiada termine en el caracter nulo de final de cadena
        out_path[MAX_BINARY_PATH - 1] = '\0';
        return 0;
        }
        return -1;
    }

//bucle que itera sobre todos los directorios almacenados en el cache (rutas de $PATH)
for (int i = 0; i < path_dirs_count; i++) {
    
//ensambla la ruta completa (directorio + / + nombre_comando) y la guarda en out_path, devolviendo la longitud escrita    
    int n = snprintf(out_path, MAX_BINARY_PATH, "%s/%s", path_dirs[i], name);
//verifica si ocurrio un error en snprintf o si la ruta resultante es mas larga que el limite permitido, en cuyo caso ignora este directorio
    if (n <= 0 || n >= MAX_BINARY_PATH) continue;
//llama a access() para comprobar si el binario ensamblado existe y es ejecutable en este directorio del $PATH
    if (access(out_path, X_OK) == 0) return 0;
    }

//si termino el bucle y no lo encontro en ningun directorio, retorna -1 indicando que el comando no se encuentra
return -1;
}

//procesa y aplica las redirecciones de entrada/salida (<, >, >>) modificando los descriptores de archivos del proceso
static int apply_redirections(const NodeComando *cmd)
{

//verifica si el nodo del comando tiene definido un archivo para redireccionar su entrada estándar ('<')
if (cmd->input_file != NULL) {
    
//abre el archivo en modo de solo lectura (O_RDONLY)    
    int fd = open(cmd->input_file, O_RDONLY);
//si hubo error al abrir el archivo, imprime el error y retorna -1    
    if (fd == -1) { perror(cmd->input_file); return -1; }
    
//duplica el file descriptor del archivo sobre el file descriptor 0 (STDIN_FILENO), reemplazando el teclado por el archivo    
    if (dup2(fd, STDIN_FILENO) == -1) {
//si la duplicacion falla, imprime error, cierra el archivo abierto y retorna -1        
        perror("dup2 stdin"); close(fd); return -1;
    }
//cierra el file descriptor original devuelto por open, ya que STDIN_FILENO ahora apunta a ese archivo    
    close(fd);
}

//verifica si el nodo del comando tiene definido un archivo para redireccionar su salida estandar ('>' o '>>')
if (cmd->output_file != NULL) {
//variable para almacenar la configuracion de las banderas de apertura del archivo (flags)
    int banderas;
        
//verifica si la estructura del comando indica que es un modo "append" (agregado, equivalente a '>>')
    if (cmd->output_append) {
//configura banderas para escritura (O_WRONLY), crear si no existe (O_CREAT) y agregar al final (O_APPEND)
        banderas = O_WRONLY | O_CREAT | O_APPEND;
    } else {
//configura banderas para escritura, creacion y truncamiento (O_TRUNC), borrando el contenido previo (equivalente a '>')
        banderas = O_WRONLY | O_CREAT | O_TRUNC;
    }

//abre o crea el archivo destino con las banderas calculadas y permisos 0644 (rw-r--r--)
    int fd = open(cmd->output_file, banderas, 0644);
//si la apertura o creacion falla, imprime el error y aborta la redireccion
    if (fd == -1) { perror(cmd->output_file); return -1; }
    
//duplica el file descriptor sobre el file descriptor 1 (STDOUT_FILENO), redirigiendo la impresion de la consola al archivo    
    if (dup2(fd, STDOUT_FILENO) == -1) {
//si falla el remapeo de salida, imprime error, limpia y aborta   
        perror("dup2 stdout"); close(fd); return -1;
    }
    
//cierra el file descriptor sobrante ahora que STDOUT apunta al archivo correctamente   
    close(fd);
    }
    return 0;
}

//funcion principal que ejecuta un comando simple aislado (sin pipes)
static int run_single_command(const NodeComando *cmd)
{
//si el comando es nulo o no tiene argumentos de ejecucion, retorna error silencioso
if (!cmd || cmd->argc == 0 || !cmd->args[0]) return -1;

//comprueba si el primer argumento introducido es el comando incorporado 'exec', que reemplaza el proceso de la shell
if (strcmp(cmd->args[0], "exec") == 0) {

//si el usuario teclea 'exec' pero no proporciona ningun programa adicional a ejecutar, finaliza la ejecución sin hacer nada
    if (cmd->argc < 2) return 0; 

//variable local para guardar la ruta resuelta del ejecutable que viene despues del token 'exec'
    char binary_path[MAX_BINARY_PATH];

//invoca a la funcion de búsqueda para verificar que el binario a ejecutar exista
    if (executor_find_binary(cmd->args[1], binary_path) != 0) {
//imprime un error si el ejecutable que se intento hacer 'exec' no se encontro
        fprintf(stderr, "ucvsh: %s: comando no encontrado\n", cmd->args[1]);
        
//eetorna 127, el codigo de salida estándar de POSIX para "comando no encontrado"        
        return 127;
    }

//aplica redirecciones directamente en el proceso principal de la shell, ya que exec reemplazara a este proceso padre entero
    if (apply_redirections(cmd) != 0) return 1;

//ejecuta el nuevo programa cargandolo en la memoria del proceso actual
    execv(binary_path, &cmd->args[1]);

//solo se ejecuta si la llamada al sistema execv() falla, se imprime el error
    perror("ucvsh: exec");
    return 127; 
}
    
//evita que el usuario ejecute directamente el comando interno auxiliar 'jobsp' destinado al uso por sub-comandos
    if(strcmp(cmd->args[0],"jobsp")== 0) {
//finge ante el usuario que dicho comando "no existe" para proteger la logica de control     
    fprintf(stderr, "ucvsh: %s: comando no encontrado\n", cmd->args[0]);
     return 127;
    }
//intercepta la llamada a "jobs -p" para transformarla y poder enrutarla correctamente en los comandos incorporados (builtins.c)
    if (strcmp(cmd->args[0], "jobs") == 0 && cmd->argc > 1 && cmd->args[1] != NULL && strcmp(cmd->args[1], "-p") == 0) {
//libera la cadena original "jobs" del argumento principal  
        free(cmd->args[0]);
//sobrescribe el primer argumento con el alias interno "jobsp" que el sistema de builtins reconocera        
        cmd->args[0] = strdup("jobsp");
    }

//comprueba mediante una funcion en builtins.c si el comando solicitado es propio de la shell (cd, fg, bg, exit, etc.)
    if (is_builtin(cmd->args[0])) {
 
//si es builtin, lo ejecuta directamente en el proceso padre (sin bifurcarse) y retorna su estado de salida
    return run_builtin((NodeComando *)cmd);
    }

//buffer local para obtener la ruta absoluta del ejecutable externo del sistema
    char binary_path[MAX_BINARY_PATH];

//intenta encontrar el binario usando la cache de PATH o la ruta explicita ingresada
    if (executor_find_binary(cmd->args[0], binary_path) != 0) {
//si la busqueda falla, avisa al usuario por consola       
        fprintf(stderr, "ucvsh: %s: comando no encontrado\n", cmd->args[0]);
        return 127;
    }

    //Mascara evitar condiciones de carrera antes del fork.

    sigset_t mask, prev_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    
    // bloqueamos SIGCHLD temporalmente
    sigprocmask(SIG_BLOCK, &mask, &prev_mask);

//llamada al sistema para crear un nuevo proceso (hijo) duplicando el proceso de la shell (padre)
pid_t pid = fork();
    
//si la creacion del proceso falla por recursos insuficientes, avisa y aborta
if (pid == -1) { 
    perror("fork");
    sigprocmask(SIG_SETMASK, &prev_mask, NULL); // Por si falla , restauramos igual.
    return -1; 
}

//bloque de codigo que SOLO se ejecutara en el contexto del proceso HIJO (donde fork devuelve 0)
    if (pid == 0) {
   sigprocmask(SIG_SETMASK, &prev_mask, NULL); // restauramos la mascara.
//restaura el comportamiento predeterminado del sistema para la senial SIGCHLD dentro del proceso hijo
    signal(SIGCHLD, SIG_DFL);

//aplica las redirecciones al archivo destino dentro de este proceso hijo aislado, para no afectar la terminal del padre
    if (apply_redirections(cmd) != 0) exit(1);

//reemplaza la imagen de memoria de este hijo clonado con el programa externo localizado en binary_path
    execv(binary_path, cmd->args);
//si execv() sobrevive es porque fallo al cargar el programa, por lo que imprime la razón del fallo
    perror(binary_path); 
    exit(127);
}
//comprueba si el usuario solicito la ejecucion asincrona ("&")
    if (cmd->background) {

//llama a la funcion que revisa, notifica al usuario y elimina los trabajos previamente terminados de la tabla global
    delete_DONE_and_Print(g_jobs);
//genera un string legible del comando completo para guardarlo en la estructura de logs visual del job
    char *cmd_str = CMD_string((NodeComando *)cmd);
//registra el proceso hijo (su PID) en la tabla de trabajos indicando que actualmente se encuentra corriendo
    int job_id = add_job(g_jobs, pid, cmd_str ? cmd_str : cmd->args[0], RUNNING);
//libera la memoria del string generado
    free(cmd_str);
//imprime en la consola el Job ID asignado y el PID del sistema operativo en formato estandar "[ID] PID"
    fprintf(stderr, "[%d] %d\n", job_id, pid);
    sigprocmask(SIG_SETMASK, &prev_mask, NULL);
//retorna control inmediato a la shell sin bloquearla, regresando estado de salida 0
    return 0;
} else {
//si no es background (foreground): la shell debe detenerse hasta que este proceso hijo culmine
    int status;
    int exit_val = -1; //Variable para guardar nuestra salida.
//llamada bloqueante a waitpid indicando que el padre esperara especificamente a que muera el 'pid' recién creado
    if (waitpid(pid, &status, 0) == -1) {
//si la espera falla (ejm. por interrupcion), avisa y retorna error de ejecucion
    perror("waitpid"); 
    exit_val = -1;
    }
    else{
//evalua a traves de macros si el hijo termino de forma voluntaria usando exit() o return en main
        if (WIFEXITED(status))   exit_val = WEXITSTATUS(status);//extrae y retorna el valor de salida (0-255)
//evalua si el proceso fue terminado bruscamente por una senial externa (como un Segfault)
        if (WIFSIGNALED(status)) exit_val = 128 + WTERMSIG(status);//retorna codigo estándar UNIX (128 + numero de la señal fatal)
    }
    sigprocmask(SIG_SETMASK, &prev_mask, NULL); // restauramos el manejador.
    return exit_val;
    }
}

//funcion encargada de ejecutar cadenas complejas de comandos conectadas por tuberias ('|')
static int run_pipeline(NodeComando *first)
{
//verificacion de seguridad si el pipeline viene nulo
if (!first) return -1;

//inicializa contador para calcular la cantidad de procesos individuales involucrados en el pipeline
    int n = 0;
//puntero auxiliar para iterar
    NodeComando *cur = first;
//bucle para recorrer la lista enlazada sumando el total de eslabones conectados por el operador PIPE
    while (cur != NULL) {
//incrementa la cantidad total de comandos detectados en la cadena
    n++;
//si el operador con el que conecta al siguiente comando es un PIPE, sigue avanzando
    if (cur->next_op == OP_PIPE) cur = cur->next;
//si no es un PIPE, hemos alcanzado el final de este segmento encadenado, rompe el bucle
    else break;
    }

//si el analisis determino que hay solo 1 comando en el "pipeline", en realidad es un comando simple, delega el trabajo
    if (n == 1) return run_single_command(first);
//calcula la cantidad de tuberias fisicas necesarias: siempre es el numero de comandos (n) menos 1
    int num_pipes = n - 1;

//asigna memoria dinamica para almacenar los descriptores de lectura/escritura (2 ints) de todos los pipes requeridos
    int *pipe_fds = malloc(num_pipes * 2 * sizeof(int));
//asigna memoria dinamica para almacenar los identificadores (PID) de cada proceso hijo que participara en el pipeline
    pid_t *child_pids = malloc(n * sizeof(pid_t));
//validamos que el sistema operativo concediera la memoria requerida
    if (!pipe_fds || !child_pids) {
//reporta error en caso de fallo de asignacion de memoria dinamica
    perror("run_pipeline: malloc");
//libera posibles parciales antes de abortar el pipeline
    free(pipe_fds); free(child_pids);
    return -1;
}

//bucle para solicitar al sistema operativo la creacion fisica de cada una de las tuberias de comunicacion
for (int i = 0; i < num_pipes; i++) {
//crea una tuberia bidireccional anonima, guarda el lado de lectura en index 0 y el de escritura en index 1 de cada bloque    
    if (pipe(pipe_fds + i * 2) == -1) {
//imprime error si se agotaron los descriptores de archivos del sistema
        perror("ucvsh: pipe");
//cierra cuidadosamente todos los extremos que ya habian sido abiertos con exito en iteraciones previas para evitar "leaks"
        for (int j = 0; j < i * 2; j++) close(pipe_fds[j]);
//libera la memoria solicitada antes de fracasar
        free(pipe_fds); free(child_pids);
        return -1;
    }
}

//reinicia el iterador al inicio de la lista de comandos para empezar la creacion de los procesos concurrentes
cur = first;
//variable bandera que determinara si toda la linea de tuberias correra en segundo plano (background)
int is_background = 0;

// Mascara para seniales, evitando problemas con el manejador
    sigset_t mask, prev_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);

// Bloqueamos SIGCHLD antes de crear los hijos
  sigprocmask(SIG_BLOCK, &mask, &prev_mask);

//bucle que iterara n veces para realizar los "forks" creando simultaneamente a todos los actores del pipeline
for (int i = 0; i < n; i++) {
//la convencion de bash establece que el caracter '&' al final del ultimo comando dicta el background de todo el pipeline
    if (i == n - 1) {
//captura la propiedad background del ultimo nodo procesado  
    is_background = cur->background;
    }

//de manera analoga al single_command, transforma la bandera "-p" de "jobs" a un formato de comando de manejo interno
    if (strcmp(cur->args[0], "jobs") == 0 && cur->argc > 1 && cur->args[1] != NULL && strcmp(cur->args[1], "-p") == 0) {
        free(cur->args[0]);
        cur->args[0] = strdup("jobsp");
    }
//bifurca el proceso de la shell, se genera el proceso numero 'i' del pipeline
    pid_t pid = fork();
//control de error en la clonacion, falla si hay agotamiento de tablas de procesos del sistema
    if (pid == -1) {
        perror("ucvsh: fork pipeline");
//nnte fallo critico, el padre debe cerrar y matar cualquier pipe que haya creado para evitar un bloqueo absoluto
        for (int k = 0; k < num_pipes * 2; k++) close(pipe_fds[k]);
        free(pipe_fds); free(child_pids);
        return -1;
    }

//logica del proceso hijo clonado
    if (pid == 0) {

        sigprocmask(SIG_SETMASK, &prev_mask, NULL); //desbloqueamos la senial para el hijo
//desvincula el proceso hijo del manejador de seniales zombi del padre para evitar bucles    
        signal(SIGCHLD, SIG_DFL);

//si este proceso no es el primero de la cadena, debe leer del pipe anterior a el
        if (i > 0) {
//duplica el extremo de lectura del pipe izquierdo y lo posiciona en la entrada estandar (teclado virtual)
            dup2(pipe_fds[(i - 1) * 2 + 0], STDIN_FILENO);
        }
//si este proceso no es el último de la cadena, debe escribir en el pipe posterior a el
        if (i < n - 1) {
//duplica el extremo de escritura del pipe derecho y lo posiciona en la salida estandar (consola virtual)
            dup2(pipe_fds[i * 2 + 1], STDOUT_FILENO);
        }

//el hijo copia todo el arreglo de pipes del padre, debe cerrar todos sus descriptores originales para no dejar escritores fantasmas y generar bloqueos de EOF
        for (int k = 0; k < num_pipes * 2; k++) {
            close(pipe_fds[k]);
        }

//como los hijos van a mutar (exec), liberamos la memoria dinamica local del clon 
        free(pipe_fds);
        free(child_pids);

//permite al usuario sobreescribir la entrada/salida de un segmento de la tuberia mediante redirecciones fisicas con archivos (<, >)
        if (apply_redirections(cur) != 0) exit(1);

//evaluamos si uno de los componentes de la tuberia resulta ser un comando interno nativo
        if (is_builtin(cur->args[0])) {
//ejecuta la logica construida en C del comando nativo
            int ret = run_builtin(cur);
//a diferencia del padre, el clon hijo debe cerrarse usando 'exit' tras realizar su comando nativo
            exit(ret);
        }

//gestion de casos donde el comando solicita realizar un "exec" interno que mate a este clon de forma particular
        if (strcmp(cur->args[0], "exec") == 0) {
            if (cur->argc < 2) exit(0);
            char binary_path[MAX_BINARY_PATH];
//verifica la existencia en los PATH del comando objetivo post-exec
            if (executor_find_binary(cur->args[1], binary_path) != 0) {
                fprintf(stderr, "ucvsh: %s: comando no encontrado\n", cur->args[1]);
                exit(127);
            }
//sobrescribe al hijo mutando la memoria
            execv(binary_path, &cur->args[1]);
            perror("ucvsh: exec");
            exit(127);
        }
//validacion auxiliar si el usuario intenta inyectar arbitrariamente el subcomando de trabajos
        if (strcmp(cur->args[0], "jobsp") == 0) {
            fprintf(stderr, "ucvsh: %s: comando no encontrado\n", cur->args[0]);
            exit(127);
        }

//ruta estandar de ejecucion de tuberia para programas binarios externos
        char binary_path[MAX_BINARY_PATH];
//resuelve la ubicacion del ejecutable del eslabon actual
        if (executor_find_binary(cur->args[0], binary_path) != 0) {
            fprintf(stderr, "ucvsh: %s: comando no encontrado\n", cur->args[0]);
            exit(127);
        }
//carga la imagen de memoria final del binario con los pipes ya cableados
        execv(binary_path, cur->args);
//aviso en caso de fallo critico en el mapeo de memoria o ejecucion binaria incompatible
        perror(binary_path);
        exit(127);
    }
//guarda el PID del proceso clonado en el arreglo correspondiente para control futuro
    child_pids[i] = pid;
//avanza el iterador hacia el siguiente comando de la cadena para prepararlo para el proximo ciclo de Fork
    cur = cur->next;
    }
//tras crear todos los hijos, el proceso de la shell original (el padre) debe cerrar todos sus propios file descriptors vinculados a los pipes
//si no hace esto, los hijos creeran que el padre siempre les escribira, trabando permanentemente la consola por un deadlock al no emitir el EOF
    for (int k = 0; k < num_pipes * 2; k++) {
        close(pipe_fds[k]);
    }
//variable para rastrear el codigo de error resultante del pipeline general
    int last_exit = 0;

//evaluamos si el ultimo comando marco que todo el pipeline debe ser delegado al fondo
    if (is_background) {
//limpia la consola y los registros de procesos que terminaron asincronamente mientras tanto
        delete_DONE_and_Print(g_jobs);

//buffer temporal para generar una representacion visual completa de todo el pipeline para los logs de la shell
        char full_cmd[1024] = {0}; 
        NodeComando *tmp = first;

//bucle de concatenacion para reconstruir la cadena original de texto que escribio el usuario
       while (tmp != NULL) {
//itera sobre todos los argumentos del nodo actual para imprimirlos uno tras otro
        for (int i = 0; i < tmp->argc; i++) {
//aniade el argumento al string gigante en construccion
            strcat(full_cmd, tmp->args[i]);
            
//coloca espacios separadores entre los parametros, exceptuando tras el ultimo parametro del comando
            if (i < tmp->argc - 1) {
                strcat(full_cmd, " ");
            }
        }

//si el nodo actual esta enlazado por un pipe con el posterior, imprime visualmente el delimitador ' | '
        if (tmp->next_op == OP_PIPE) {
            strcat(full_cmd, " | ");
            tmp = tmp->next;
        } else {
//cuando ya no hay operadores de pipe de por medio, terminamos de armar el string visual
            break;
        }
    }
//ssigna todo este ensamble visual como un solo trabajo gigante en la tabla de trabajos controlados, usando el PID del primer eslabon como guia
        int job_id = add_job(g_jobs, child_pids[0], full_cmd, RUNNING);

//notifica por consola que el pipeline paso al estado background indicando su ID referencial y PID base
        fprintf(stderr, "[%d] %d\n", job_id, child_pids[0]);
//define el estado exitoso porque la ejecucion asincrona no reporta un error
        last_exit = 0;

        sigprocmask(SIG_SETMASK, &prev_mask, NULL); // desbloqueamos para el padre background.
    } else {
//ejecucion foreground, la shell debe congelarse esperando a que finalicen secuencialmente cada uno de los eslabones creados
      for (int i = 0; i < n; i++) {
    int status;
//instruye al padre a pausar hasta que el hijo con PID especifico culmine
    if (waitpid(child_pids[i], &status, 0) == -1) {
//en Linux, si waitpid reporta un error por ECHILD, implica que la señal asincrona del handler lo recolecto primero
        if (errno == ECHILD) {
//no hacemos nada, es un error inofensivo, el hijo ya fue atendido por el recolector de basura de señales
        } else {
//si el error de interrupcion o fallo es distinto a ECHILD, debe imprimirse a la consola por seguridad
            perror("waitpid");
        }
//fuerza el ciclo a seguir con el siguiente proceso del pipeline que podría seguir en ejecucion
        continue; 
    }
    
//por convenciones de Linux/Bash, el valor de salida (error code) de todo un pipeline lo determina unicamente el del ultimo comando evaluado
    if (i == n - 1) {
//registra de forma oficial si salio voluntariamente o fue abortado violentamente para retornarlo a main.c
        if (WIFEXITED(status))        last_exit = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) last_exit = 128 + WTERMSIG(status);
    }
}
  // Si un background murió mientras esperábamos, el handler se ejecutará en este exacto instante
   sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    }

//libera de la memoria ram del padre los arreglos de seguimiento de PIPES
    free(pipe_fds);
//libera el listado de PID de hijos ya esperados
    free(child_pids);
//regresa a la logica orquestadora el codigo general de error/exito obtenido del bloque
    return last_exit;
}

//funcion encargada de consumir el arbol Sintactico (o Lista Enlazada) enviada por el parser general
int executor_run(NodeComando *head)
{
//verificacion contra listas de comandos vacias, evita Segmentations Fault
    if (!head) return -1;

//registra interactivamente el codigo de salida que dejara cada linea tras completarse
    int exit_code  = 0;
//enum tracker que permite decidir la ejecucion condicional basada en el encadenamiento previo (&&, ||)
    OpType prev_op = OP_NONE;
//apuntador principal utilizado para el recorrido integral del conjunto de mandos emitidos por el usuario en una linea
    NodeComando *cur = head;

//ejecuta indefinidamente hasta terminar la linea parseada
    while (cur != NULL) {

//variable booleana por defecto seteada a "Verdadero" (1) indicando que un comando es elegible a ejecución
    int should_run = 1;
//si el separador logico anterior fue un "AND" (&&), pero el comando de la izquierda frascaso (exit_code distinto de 0), desactiva la ejecucion
    if (prev_op == OP_AND && exit_code != 0) should_run = 0;
//si el separador logico anterior fue un "OR" (||), y el comando de la izquierda funciono sin errores (0), realiza una evaluacion perezosa desactivando el lado derecho
    if (prev_op == OP_OR  && exit_code == 0) should_run = 0;

//si la matematica booleana determino que no debemos correr esto, bloque para avanzar a ciegas e ignorarlo
    if (!should_run) {
//rescata y registra el operador que sigue a este comando condenado a omision
        OpType op = cur->next_op;
//avanza el puntero ignorando definitivamente este primer nodo ignorado
        cur = cur->next;

//si por consiguiente el comando ignorado estaba fuertemente acoplado en un pipeline, el pipeline enteno debe saltarse
        while (op == OP_PIPE && cur != NULL) {
//sigue robando el proximo operador de la cola acoplada
            op  = cur->next_op;
//adelanta el puntero hasta romper la cadena de pipes acoplada logicamente       
            cur = cur->next;
        }
//refresca la memoria del ciclo global indicando en que clase de separador nos topamos al salir de la zona ciega
        prev_op = op;
//fuerza una nueva evaluacion sin pasar por el motor de ejecucion pesado del final del ciclo       
        continue;
        }
//si por el contrario el comando es candidato a correr, verificamos si el, como puntero primario, empieza una cadena PIPE
    if (cur->next_op == OP_PIPE) {
//enlaza la lista actual al motor masivo de enrutamiento multiple y captura su estado resultante
        exit_code = run_pipeline(cur);

//como "run_pipeline" procesa a todos internamente, el motor central debe adelantar su contador mas alla de toda la cadena ya procesada
        while (cur->next_op == OP_PIPE && cur->next != NULL) {
            cur = cur->next;
        }
//rescata el operador condicional (Ej: el ';' o el '&&') dictado justo tras terminar toda la inmensa cadena PIPE
        prev_op = cur->next_op;
//asienta el cursor en el siguiente bloque de comandos no relacionado para la iteracion contigua
        cur     = cur->next;
    } else {
//en caso de que se trate de una instruccion singular libre de PIPES acoplados, usa el motor de un solo disparo directo
        exit_code = run_single_command(cur);
//guarda el operador vinculante asignado despues para logica boolean de la proxima vuelta (;; || &&)
        prev_op   = cur->next_op;
//avanza limpia y singularmente el apuntador logico en la lista maestra        
        cur       = cur->next;
    }
}
//finaliza todo y devuelve el estado de la ultima evaluacion ocurrida para que el MAIN.C reaccione
    return exit_code;
}