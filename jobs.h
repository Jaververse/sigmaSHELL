#ifndef JOBS_H
#define JOBS_H

#include <string.h>
#include <sys/types.h>

#define MAX_CHAR_IN_LINE 256

//estados de un JOB
typedef enum{
    RUNNING,
    SUSPENDED,
    DONE
} JobState;

typedef struct JobNode{
    pid_t pgid; //pid del proceso 
    char command[MAX_CHAR_IN_LINE]; //comando que genero el proceso
    JobState state; //estado
    int job_id; //id para trackear la correcta enumracion en la tabla de jobs, si es 1 ya se puede borrar, si es 0 todavia no se ha mostrado
    int showedJob; //Para trackear la aparicion de los estados DONE en JOBS al hacer el builtin
    struct JobNode *next;   
    
} Job; 

typedef struct{

    Job *head;
    int next_id;    //Proximo id a asignarle al proximo job
    int total_jobs; 

}JobTable;



void init_JobTable(JobTable *table);
int add_job(JobTable *table, pid_t pid, const char *cmd, JobState state);
int delete_job(JobTable *table, pid_t pgid);
void builtin(JobTable *table);
void clear_JobTable(JobTable* table);
int delete_DONE_jobs(JobTable *table);
void delete_DONE_and_Print(JobTable *tabla);
void builtinPID(JobTable *table);


#endif