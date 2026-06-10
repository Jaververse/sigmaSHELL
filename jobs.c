#include "jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



void builtin(JobTable *table){

    if(table == NULL) return;

    Job *currentJob = table->head;

        while(currentJob != NULL){
            const char *currentState = "";
            if (currentJob->state == RUNNING) currentState = "Running";

            else if (currentJob->state == SUSPENDED) currentState = "Suspended";

            else if (currentJob->state == DONE){
                if(!currentJob->showedJob) currentJob->showedJob = 1;
                currentState = "Done";
            } 

            printf("[%d] %s\t\t%s\n", currentJob->job_id, currentState, currentJob->command);
            currentJob = currentJob->next;
        }
        return;

}


//Para el built in con -p
void builtinPID(JobTable *table){
    if(table == NULL) return;

    Job *currentJob = table->head;

    while (currentJob != NULL)
    {
       printf("%d\n", currentJob->pgid);
       currentJob = currentJob->next;
    }
    return;
}

//Inicializacion
void init_JobTable(JobTable *table){
    if(table == NULL) return;
    table->head = NULL;
    table->next_id = 1;
    table->total_jobs = 0;
}


int add_job(JobTable *table, pid_t pid, const char *cmd, JobState state){

    if(table == NULL) return -1;
    
    Job *newJob = (Job*)malloc(sizeof(Job));
    if(newJob == NULL) return -1;

    strncpy(newJob->command,cmd, MAX_CHAR_IN_LINE - 1); //copia tmb el \n
    newJob->command[MAX_CHAR_IN_LINE - 1] = '\0'; //obtenemos el valor nulo.
    newJob->pgid = pid;
    newJob->state = state;
    newJob->next = NULL;
    newJob->job_id = table->next_id++;
    newJob->showedJob = 0;

     if(table->head == NULL){
        table->head = newJob;
     }
     else{
        Job *current = table->head;
            while(current->next != NULL)
            {
                current = current->next;
            }
            current->next = newJob;
        }
        table->total_jobs++;

        return newJob->job_id;
    
}

//Para cuando se haga kill
int delete_job(JobTable *table, pid_t pgid){
    if(table == NULL || table->head ==NULL) return -1;

    Job*current = table->head;
    Job*prev = NULL;

    while(current != NULL && current->pgid != pgid){
        prev = current;
        current = current->next;
    }

    if(current == NULL) return -1;

    if(prev == NULL){
        table->head = current->next;
    }
    else{
        prev->next = current->next;
    }
 
    free(current);
    table->total_jobs--;

    return 0;
}

//Quitar los jobs en estado Done dew la tabla
int delete_DONE_jobs(JobTable *table){
    // 1. Filtro de seguridad obligatorio
    if(table == NULL || table->head == NULL) return -1;

    Job *current = table->head;
    Job *prev = NULL;

    while(current != NULL){
        if(current->state == DONE && current->showedJob){
          
            Job *temp = current; 

            if(prev == NULL){
                // Es el primer elemento de la lista
                table->head = current->next;
                current = table->head; 
            } else {
                // Está en medio o al final de la lista
                prev->next = current->next;
                current = current->next; 
            }

            free(temp);
            table->total_jobs--; 
            
        } else {
          
            prev = current;
            current = current->next;
        }
    }
    return 0;
}


//Para que se imprima los que se terminaron al crear otro bg
void delete_DONE_and_Print(JobTable *tabla){
    if(tabla == NULL || tabla->head == NULL){
        return;
    }
    Job *current = tabla->head;

    while(current != NULL){
        if(current->state == DONE){

            char *state = "Done";

            printf("[%d] %s\t\t%s\n", current->job_id, state, current->command);

            current->showedJob = 1;
        }
        current = current->next;
    }
  delete_DONE_jobs(tabla);
}


void clear_JobTable(JobTable *table){
    if (table == NULL) return;
    Job *current = table->head;
    while (current != NULL)
    {
        Job *aux = current->next;
        free(current);
        current = aux;
    }
    table->head = NULL;
    table->total_jobs = 0;
    
}

