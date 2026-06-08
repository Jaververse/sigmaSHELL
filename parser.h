
#ifndef PARSER_H
#define PARSER_H

typedef enum { 
OP_NONE, 
OP_SEQ, 
OP_AND, 
OP_OR, 
OP_PIPE } OpType; // Enum para los operadores.

typedef struct NodeComando {
char **args;
int argc;
char *input_file;
char *output_file;
int  background;
OpType next_op;
struct NodeComando *next;
} NodeComando; // Struct para la lista enlazada de comandos.

NodeComando *Parser(const char *line); // Parser
void liberarListaCMD(NodeComando *head); // Liberar Memoria

char *CMD_string(NodeComando *cmd); // Util para jobs , y recostruir el comando, que es lo contario a lo que hacemos en nuestro parser

#endif