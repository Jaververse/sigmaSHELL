
#include "parser.h"   
#include "executor.h"  
#include "jobs.h"   
#include "history.h" 

#include "input.h"
#include "builtins.h" 

#include <stdio.h> 
#include <stdlib.h>
#include <string.h>  
#include <unistd.h>   


JobTable         g_job_table;  // tabla de trabajos en background
HistoryPersistent g_history;   // historial persistente de comandos

int main(void)
{

//inicializar la tabla de jobs con valores por defecto
init_JobTable(&g_job_table);

//inicializar la estructura del historial en memoria
init_history(&g_history);

//cargar el historial desde ~/.ucvsh_history
load_history_from_file(&g_history);

executor_init(&g_job_table);

    while (1) {

    char *line = NULL;

line = read_line(&g_history);
if(line == NULL){
    break;
}
//guardar en historial
add_to_history(&g_history, line);

//parsear y ejecutar
NodeComando *lista = Parser(line);

if (lista != NULL) {
    executor_run(lista);
    liberarListaCMD(lista);
        }

//read_line() usa malloc internamente: liberar la línea
free(line);

}

return 0;
}
