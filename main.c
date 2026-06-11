#include "parser.h"    
#include "executor.h"   
#include "jobs.h"     
#include "history.h"   
#include "input.h"       
#include "builtins.h"    
#include "signKeyboard.h"  
#include <stdio.h>         
#include <stdlib.h>       

JobTable g_job_table;             //tabla global de jobs para que executor.c y builtins.c compartan el mismo estado
HistoryPersistent g_history;      //historial global para que input.c, history.c y builtins.c trabajen sobre la misma lista

int main(void)
{
    init_signals();               //maneja la señal CTRL + C para que no se cierre la shell

    init_JobTable(&g_job_table);  //prepara la lista de jobs antes de registrar procesos en background

    init_history(&g_history);     //inicializa la estructura enlazada del historial en memoria

    load_history_from_file(&g_history); //recupera comandos anteriores desde el archivo persistente del historial

    executor_init(&g_job_table);  //entrega la tabla de jobs al executor y prepara recursos internos como PATH y SIGCHLD

    while (1) {  //ciclo principal de la shell: leer, parsear, ejecutar y liberar en cada iteracion
        char *line = read_line(&g_history); //lee una linea completa desde el prompt y permite navegar el historial

        if (line == NULL) {       //si la entrada termina o hay error irrecuperable, se sale limpiamente del ciclo
            break;
        }

        add_to_history(&g_history, line); //guarda la linea valida en el historial en memoria para uso posterior

        NodeComando *lista = Parser(line); //convierte la linea en una lista enlazada de comandos y operadores

        if (lista != NULL) { 
        //solo se ejecuta si el parser produjo al menos un comando valido
            if(validarsyntax(lista)==0){
            int status= executor_run(lista);  //ejecuta comandos simples, pipelines, redirecciones, &&, ||, ; y background simple
            liberarListaCMD(lista); //libera todos los nodos, argumentos y archivos creados dinamicamente por el parser
            free(line);// libera la linea devuelta por read_line(), porque esa funcion reserva memoria dinamica
            if(status==-42) break; // Solicito un exit el usuario.
            }
            else{ // Error de sintaxis , liberamos igual.
            liberarListaCMD(lista); //libera todos los nodos, argumentos y archivos creados dinamicamente por el parser
            free(line);// libera la linea devuelta por read_line(), porque esa funcion reserva memoria dinamica
            }
        } 
        else{ // si la lista es NULL, no se genero nada.
            free(line); // libera la linea devuelta por read_line(), porque esa funcion reserva memoria dinamica
            }
    }

    //LIMPIEZA GLOBAL COMPLETA
    clear_history(&g_history); // Limpiamos el historial en memoria. (funcion en history.c)
    clear_JobTable(&g_job_table); // limpiamos el job table en memoria. (funcion en jobs.c)
    executor_cleanup();  //libera recursos privados del executor, principalmente el cache de PATH
    return 0;
}
