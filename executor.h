
#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h" 
#include "jobs.h"   

//longitud maxima de una ruta construida al buscar en $PATH
#define MAX_BINARY_PATH  768

//numero maximo de directorios que se leen de $PATH
#define MAX_PATH_DIRS     64

void executor_init(JobTable *table);


void executor_cleanup(void);


int executor_find_binary(const char *name, char *out_path);

int executor_run(NodeComando *head);

#endif
