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
    pid_t pgid; //representa el pid de grupo = pid de proceso lider
    char command[MAX_CHAR_IN_LINE];
    JobState state;
    int job_id;
    int showedJob; //Para trackear la aparicion de los estados en JOBS al hacer el builtin
    struct JobNode *next;
    
} Job; 

typedef struct{

    Job *head;
    int next_id;
    int total_jobs;

}JobTable;



void init_JobTable(JobTable *table);
int add_job(JobTable *table, pid_t pid, const char *cmd, JobState state);
int delete_job(JobTable *table, pid_t pgid);
void builtin(JobTable *table);
void clear_JobTable(JobTable* table);

#endif