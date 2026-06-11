#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"  //necesario porque executor_run() recibe una lista de NodeComando creada por el parser.
#include "jobs.h"    //necesario porque executor_init() recibe la tabla de jobs compartida con la shell.

#define MAX_BINARY_PATH 768  //longitud maxima usada para construir rutas como /usr/bin/ls sin desbordar buffers.
#define MAX_PATH_DIRS 64     //cantidad maxima de directorios de PATH que se cachean al inicializar el executor.

void executor_init(JobTable *table);                  // inicializa el modulo executor, cachea PATH y registra SIGCHLD.
void executor_cleanup(void);                          // libera recursos privados del executor antes de cerrar la shell.
int executor_find_binary(const char *name, char *out_path); // busca un ejecutable por ruta explicita o dentro de PATH.
int executor_run(NodeComando *head);                  //ejecuta la lista enlazada de comandos respetando operadores del parser.
int validarsyntax(NodeComando *head);

#endif // EXECUTOR_H

