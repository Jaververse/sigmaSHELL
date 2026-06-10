#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "parser.h"


typedef struct {
char *elementos[256];
int cantidad;
} tokens_c; // Creo un struct para almacenar hasta 256 tokens y entero para llevar su cantidad todo ordenadamente.

static NodeComando *newnode(void) {
NodeComando *n  = calloc(1, sizeof(NodeComando)); // Utilizamos la estructura del Node en .h para crear un nodo nuevo y iniciliazado con 0.
if (!n) return NULL; //Si no funciona por memoria. (Seguridad)
n->args = calloc(256, sizeof(char *)); // reservamos espacio para los argumentos.
n->argc = 0; //Incializamos
n->next_op = OP_NONE; //Ningun operador del enum
n->output_append=0;
return n; //Devolvemos el nodo
}

//Ahora reconstruimos el comando pasando a string.
char *CMD_string(NodeComando *cmd) {

if (!cmd || cmd->argc == 0) return strdup(""); // Seguridad por si no existe o no argumentos.
size_t total = 0; //Variable para tomar el tamaño del string completo

for (int i = 0; i < cmd->argc; i++) { // Itermaos sobre la cantidad de argumentos para tener el total.
total += strlen(cmd->args[i]) + 1; //Sumamos 1 por el espacio que separa.
}

char *s = malloc(total + 1); // Nuestro string (el +1 por el terminar nulo)
if (!s) return NULL; // Seguridad
s[0] = '\0'; // Inicializamos

for (int i = 0; i < cmd->argc; i++) { // Volvemos a iterar , concatenando los argumentos.
strcat(s, cmd->args[i]); // Concatenamos
if (i < cmd->argc - 1) 
{
strcat(s, " "); // Agregamos espacio entre los argumentos , pero no en el ultimo.
}
}

return s; // La cadena unificada.
}

//Recorremos la entrada de linea, letra por letra para tokenizar.
static void Lexer(const char *entrada, tokens_c *contenedor) {
    int i = 0; // Indice para iterar sobre la entrada.
    int longitud = strlen(entrada); // Longitud de la entrada.
    contenedor->cantidad = 0; // Inicializamos.

    while (i < longitud && contenedor->cantidad < 256) { // Mientras que no sobrepasemos la longitud y estamos con los 256.
        
    while (i < longitud && isspace((unsigned char)entrada[i])) { // Saltamos espacios.
         i++;
    }
    if (i >= longitud) break; // Seguridad

    char buffer[2048]; //Almacenamos temporalmente el token, para luego pasarlo como puntero al contenedor.
    int bufferi = 0; // Indice para iterar con el buffer.

    // Tomamos en cuenta los operados dobles.
    if (i + 1 < longitud && ((entrada[i] == '&' && entrada[i+1] == '&') || (entrada[i] == '|' && entrada[i+1] == '|') || (entrada[i] == '>' && entrada[i+1] == '>'))) {
        buffer[bufferi++] = entrada[i++]; // El buffer toma el valor del operador y avanzamos
        buffer[bufferi++] = entrada[i++]; // De la misma manera pero con el segundo caracter.
        buffer[bufferi] = '\0'; // Caracter nulo para terminar.
        contenedor->elementos[contenedor->cantidad++] = strdup(buffer); // Copiamos el string del buffer al contenedor.
        continue; //Vamos a otro ciclo para seguir el segmentado.
        }
    // Tomamos en cuenta los operados simples.
    if (entrada[i] == ';' || entrada[i] == '|' || entrada[i] == '&' || entrada[i] == '>' || entrada[i] == '<') {
        buffer[bufferi++] = entrada[i++]; //Copiamos el operador en el buffer y avanzamos.
        buffer[bufferi] = '\0'; //Caracter nulo terminamos.
        contenedor->elementos[contenedor->cantidad++] = strdup(buffer); // Copiamos el contenido del buffer en el contenedor.
        continue; // Vamos a otro ciclo para seguir segmentando.
        }
    // palabras simples, ignoramos los espacios y operadores.
    while (i < longitud && !isspace((unsigned char)entrada[i]) && entrada[i] != ';' && entrada[i] != '|' && entrada[i] != '&' && entrada[i] != '>' && entrada[i] != '<') {
    // Si tenemos comillas, tomamos todo lo que contenga hasta la proxima comilla.
    if (entrada[i] == '"' || entrada[i] == '\'') {
    char comilla = entrada[i++]; // Tomamos el valor de la comilla y avanzmaos
    while (i < longitud && entrada[i] != comilla) { 

        buffer[bufferi++] = entrada[i++]; // Guardamos en el buffer todo lo que esta adentro de las comillas, hasta llegar al otro extremo.

    }
    if (i < longitud) 
    { 
        i++; // Aqui llegamos a la comilla donde cerramos y avanzamos para seguir el resto de la linea.
    }
    } 
    //Caracter comun.
    else {
    buffer[bufferi++] = entrada[i++]; //guardamos y avanzamos.
    }
    }
        
    buffer[bufferi] = '\0'; // Cerramos la palabra simple.
    if (bufferi > 0) { // Si tenemos algo en el almacenamiento , lo guardamos en el contenedor , igual que antes copiando.
        contenedor->elementos[contenedor->cantidad++] = strdup(buffer);
    }
    }
}

//Vamos a parsear las lineas, aqui utilizamos nuestra funcion anterior del lexer.
NodeComando *Parser(const char *line) {
    if (!line || !*line) return NULL; //Seguridad

    tokens_c contenedor; // Pasamos el contenedor para los tokens, utilizado por nuestra funcion.
    Lexer(line, &contenedor);

    if (contenedor.cantidad == 0) return NULL; // No tenemos tokens.

    NodeComando *head = newnode(); // cabeza de nuestra lista
    NodeComando *curr = head; // para movernos por la lista.

    for (int itok = 0; itok < contenedor.cantidad; itok++) { // Vamos a ir iterando sobre los tokens que obtuvimos en la segmentacion.
        char *token = contenedor.elementos[itok]; // tomamos el token actual.

        if (strcmp(token, ";") == 0) { 
        curr->args[curr->argc] = NULL; // Cerramos los argumentos del comando anterior.
        curr->next_op = OP_SEQ; // Siguiente secuencia.
        curr->next = newnode(); // Creamos un nuevo nodo para el siguiente comando.
        curr = curr->next; // Nos movemos hacia ese nuevo nodo.
        } 
        else if (strcmp(token, "||") == 0) {
        curr->args[curr->argc] = NULL; // Cerramos los argumentos del comando anterior.
        curr->next_op = OP_OR; // Siguiente OR
        curr->next = newnode(); 
        curr = curr->next; 
        } 
        else if (strcmp(token, "&&") == 0) { // Seguimos la misma estructura , pero ahora con Operador AND
         curr->args[curr->argc] = NULL; 
         curr->next_op = OP_AND;
         curr->next = newnode();
         curr = curr->next;
        }
        else if (strcmp(token, ">>") == 0) { // Append
        itok++; // Avanzamos al siguiente , que tiene que ser el nombre del archivo.
        if (itok < contenedor.cantidad) {
        curr->output_file = strdup(contenedor.elementos[itok]);
        curr->output_append = 1; // Tenemos que escribir al final.
        }
        }
        else if (strcmp(token, "|") == 0) { // Operador PIPE
        curr->args[curr->argc] = NULL; 
        curr->next_op = OP_PIPE;
        curr->next = newnode();
        curr = curr->next;
        } 
        else if (strcmp(token, "&") == 0) { // Si es un operador de fondo.
        curr->background = 1; // Marcamos este comando como fondo.
        } 
        else if (strcmp(token, ">") == 0) { // Redireccion de la salida.
        itok++; //Avanzamos siguiente token y tiene que ser el nombre del archivo.
        if (itok < contenedor.cantidad) { // Seguridad
        curr->output_file = strdup(contenedor.elementos[itok]); // Copiamos y guardamos en la salida.
        curr->output_append = 0; // no es al final.
        }
        } 
        else if (strcmp(token, "<") == 0) { // De manera analoga al anterior pero con entrada.
        itok++; 
        if (itok < contenedor.cantidad) {
        curr->input_file = strdup(contenedor.elementos[itok]);
        }
        } 
        else { // Aqui no tenemos ningun operador, es una comando o parametro.
        curr->args[curr->argc++] = strdup(token); // Guardamos el argumento y aumentamos el contador de argc.
        }
    }

    // Ya salimos del bucle, y cerramos el ultimo comando.
    curr->args[curr->argc] = NULL; 

 
    for (int itok = 0; itok < contenedor.cantidad; itok++) {
        free(contenedor.elementos[itok]); // liberamos la memoria del contenedor.
    }

    return head; // Devolvemos el puntero inicial de la lista enlazada.
}
// Liberamos la memoria de la lista enlazada.
void liberarListaCMD(NodeComando *head) {
    while (head) {
    NodeComando *tmp = head;
    for (int i = 0; i < tmp->argc; i++) {
    free(tmp->args[i]); //Liberamos cada argumento uno por uno.
    }
    free(tmp->args); // Liberamos el arreglo de argumentos.
    if (tmp->input_file)  free(tmp->input_file); // liberamos entrada si tenia.
    if (tmp->output_file) free(tmp->output_file); // liberamos salida si tenia.
    head = head->next; // pasamos al siguiente.
    free(tmp); // eliminamos el actual.
    }
}
