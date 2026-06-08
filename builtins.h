#ifndef BUILTINS_H
#define BUILTINS_H
#include "parser.h" //Aqui esta el NodeComando

int is_builtin(const char *cmd);
int run_builtin(NodeComando *cmd);

#endif



