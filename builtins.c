#include "builtins.h"
#include "parser.h"
#include "jobs.h"
#include "history.h"
#include "executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

extern JobTable g_job_table;   // ACOMODAR DONDE SE CREARON
extern HistoryPersistent g_history; // =

// Para imprimir nuestro directorio actual.
static int builtin_pwd(void) {
    char cwd[2048]; // Creamos un buffer para almacenar nuestra ruta.
    if (getcwd(cwd, sizeof(cwd)) != NULL) { //Obtenemos el directorio y lo guardamos en nuestro buffer, si no tenemos error.
    printf("%s\n",cwd); // Imprimimos la direccion actual.
    return 0;
    } else {
    perror("ucvsh: pwd"); // Error del sistema si no se encontro la direccion.
    return 1;
    }
}

//Para cambiar de direccion.
static int builtin_cd(char **args) {
    const char *dir = NULL;
    if(args[1] != NULL){
    dir = args[1]; // Usamos el directorio especificado por el user.
    }
    else
    {
    dir = getenv("HOME"); // Si no nos dieron uno , le pedimos el  HOME del sistema con el getenv y lo usamos.
    }
    if (!dir) { 
    fprintf(stderr, "ucvsh: cd: HOME no establecido\n"); 
    return 1; 
    }
    if (chdir(dir) != 0) {  // Si realizamos el cambio y no funciona, tenemos error.
    perror("ucvsh: cd"); 
    return 1; 
    }
    return 0;
}

// Mostrar los trabajos en segundo plano.
static int builtin_jobs(void) {
    // LLamada a funcion en jobs.c
    builtin(&g_job_table); 
    delete_DONE_jobs(&g_job_table);
    return 0;
}

// Principal para terminar la shell.
static int builtin_exit(void) {
    Job *currentJob = g_job_table.head; // Matamos a los procesos hijos que siguen corriendo en segundo plano.
    while (currentJob != NULL) {  // Iteramos sobre la lista de jobs.
    if (currentJob->state == RUNNING || currentJob->state == SUSPENDED) {
    kill(currentJob->pgid, SIGTERM); // Enviamos la señal de terminacion asu pgid.
    }
     currentJob = currentJob->next; // Avanzamos entre cada job.
    }
    
    while (waitpid(-1, NULL, WNOHANG) > 0); // elimina los zombies, sin pausar o esperar el proceso principal.

    save_index_to_file(&g_history);
    save_history_to_file(&g_history); // Utilizamos la funcion en history.c para guardar el historial en un archivo.
    return -42; // devolvemos este (-42) que le da la orden al main de terminar y liberar.
}

//Traemos al frente un proceso de segundo plano.
static int builtin_fg(char **args) {
    Job *job = NULL;

    if (args[1] != NULL) { // Si el usuario pasa un numero de trabajo, lo buscamos por ID.
    int id = atoi(args[1]); // llevamos a entero y asignamos auna variable.
    job = g_job_table.head; // empezamos desde el inicio.
        
    while (job != NULL && job->job_id != id) {
    job = job->next; // Avanzamos en la lista hasta encontrar el trabajo indicado.
    }
    } 
    else { // Si el usuario no especifico un ID, buscamos el ultimo trabajo en segundo plano.
    
    if (g_job_table.head == NULL) { // Verificacion de trabajos en segundo plano.
    fprintf(stderr, "ucvsh: fg: no hay trabajos en segundo plano\n");
    return 1;
    }
        
    job = g_job_table.head; // ya que los trabajos se agregan al final de la lista.
    while (job->next != NULL) {
    job = job->next; // Vamos al ultimo nodo de la lista.
    }
    }

    if (job == NULL) { // Si no encontramos el trabajo.
    fprintf(stderr, "ucvsh: fg: trabajo no encontrado\n");
    return 1;
    }
    if (job->state == DONE) { // SI el estado nos indica que el proceso ya termino.
    fprintf(stderr, "ucvsh: fg: [%d] Done\n", job->job_id);
    return 1;
    }

    printf("%s", job->command); // Imprimimos el comando.
    fflush(stdout); // Aseguramos que se imprima.


    if (job->state == SUSPENDED) { // Si estaba suspendido, lo despertamos.
    kill(job->pgid, SIGCONT); // señal para reanudar.
    }

    job->state = RUNNING; // Cambiamos el estado a corriendo.

    
    int estado; // variable de estado
    waitpid(job->pgid, &estado, WUNTRACED);  // Esperamos que termine o se suspenda.
    printf("\n"); 
    fflush(stdout);
    if (WIFSTOPPED(estado)) { // Si se suspendio. faltaria la señal 
    job->state = SUSPENDED; // Cambiamos el estado.
    printf("\n[%d] Suspendido  %s\n", job->job_id, job->command); // informamos al usuario.
    return 0;
    }

    job->state = DONE; // si ya finalizo normalmente, lo damos por terminado.

    if (WIFEXITED(estado)) { // Si termino correctamente, damos su estado de salida.
    return WEXITSTATUS(estado);
    } else {
    return 1; // Error.
    }
}


static int builtin_bg(char **args) {
    Job *job = NULL;

    if (args[1] != NULL) { // Analogo al fg, especifica el id.
    int id = atoi(args[1]);
    job = g_job_table.head;
        
    while (job != NULL && job->job_id != id) {
    job = job->next; // encontramos nuestro objetivo.
    }
    } 
    else { // Si el usuario no especifico.
    if (g_job_table.head == NULL) {
    fprintf(stderr, "ucvsh: bg: no hay trabajos\n"); //No hay trabajos 
    return 1;
    }
        
    job = g_job_table.head;
    while (job->next != NULL) {
    job = job->next; //avanzamos hasta el ultimo.
    }
    }
    //Verificaciones
    if (job == NULL) { 
    fprintf(stderr, "ucvsh: bg: trabajo no encontrado\n");
    return 1;
    }
    if (job->state == DONE) { // Termino
    fprintf(stderr, "ucvsh: bg: [%d] Done\n", job->job_id);
    return 1;
    }
    if (job->state == RUNNING) { // Ya corriendo.
    fprintf(stderr, "ucvsh: bg: [%d] ya esta corriendo en segundo plano\n", job->job_id);
    return 0; 
    }


    if (job->state == SUSPENDED) { // Despertamos al proceso , si esta suspendido.
    kill(job->pgid, SIGCONT); // para seguir la ejecucion.
        
    job->state = RUNNING; // actualizamos el estado.
        
    printf("[%d] %s &\n", job->job_id, job->command); // Confirmamos.
    }
     
    return 0; // Retornamos sin error, y sin esperar con el waitpid , esto hace que se ejecute en segundo plano.
}

//Muestro history
static int builtin_historyR(void) {
    // LLamo funcion en history.c
    builtin_history(&g_history);
    return 0;
}

static int builtin_jobsp(void)
{
    builtinPID(&g_job_table);
    return 0;
}


int is_builtin(const char *comando) {
    if (comando == NULL) {
    return 0; 
    }
    if (strcmp(comando,"exit") == 0) {
    return 1;
    }
    else if (strcmp(comando,"cd") == 0) {
    return 1;
    }
    else if (strcmp(comando,"pwd") == 0) {
    return 1;
    }
    else if (strcmp(comando,"jobs") == 0) {
    return 1;
    }
    else if (strcmp(comando,"fg") == 0) {
    return 1;
    }
    else if (strcmp(comando,"bg") == 0) {
    return 1;
    }
    else if (strcmp(comando,"history") == 0) {
    return 1;
    }
    else if (strcmp(comando,"jobsp") == 0) {
    return 1;
    }
    return 0; //Es un comando externo 
}

int run_builtin(NodeComando *comando) {
    if (strcmp(comando->args[0], "pwd") == 0) 
    {
    return builtin_pwd();
    }
    if (strcmp(comando->args[0], "cd") == 0) 
    {
    return builtin_cd(comando->args);
    }
    if (strcmp(comando->args[0], "jobs") == 0) 
    {
    return builtin_jobs();
    }
    if (strcmp(comando->args[0],"jobsp") == 0)
    {
    return builtin_jobsp();
    }
    if (strcmp(comando->args[0], "exit") == 0) 
    {
    return builtin_exit();
    }
    if (strcmp(comando->args[0], "fg") == 0) 
    {
    return builtin_fg(comando->args);
    }
    if (strcmp(comando->args[0], "bg") == 0) 
    {
    return builtin_bg(comando->args);
    }
    if(strcmp(comando->args[0],"history") == 0)
    {
    return builtin_historyR();
    }
    //Si no es ninguno , no es un builtin valido.
    return 1;
}