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
            else if (currentJob->state == DONE) currentState = "Done";

            printf("[%d] %s\t\t%s\n", currentJob->job_id, currentState, currentJob->command);
            currentJob = currentJob->next;
        }
        return;

}

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

