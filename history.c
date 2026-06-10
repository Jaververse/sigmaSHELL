#include "history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>




//inicializar y quitar basura de cada campo
void init_history(HistoryPersistent *historial){
    historial->head = NULL;
    historial->tail = NULL;
    historial->cursor = NULL;
    historial->memLines = 0;
    historial->nextLine = 1;
}

//Cada funcion evalua por seguridad si el historial o si su head es nulo para evitar parametros incorrectos
// La unica que no evalua valor del head es la de cargado de memoria secundaria porque esa se llama al inicio del programa



//buil-int del historial
void builtin_history(HistoryPersistent *historial){
    if(historial == NULL || historial->head == NULL || historial->memLines == 0) return; 

    //recorrido de la lista imprimiendo por pantalla cada nodo

    HistoryLine *current = historial->head;

    while(current != NULL){

        printf("%d  %s", current->pos, current->commandEditable);

        //por si no tiene el salto de linea integrado, se aniade 
        if (current->commandEditable[strlen(current->commandEditable) - 1] != '\n') {
            printf("\n");
        }
        current = current->next;
    }
}


//aniadir en el historial de memoria
void add_to_history(HistoryPersistent *historial, const char *command){

    if(historial == NULL || command == NULL) return;

    //Comprobaciones para no guardar espacios en blanco que vengan de un ENTER
    if (strcmp(command, "\n") == 0 || strlen(command) == 0) {
        return; 
    }

    //para no agregar comandos repetidos
    if (historial->tail != NULL) {
        if (strcmp(historial->tail->command, command) == 0) {
            return; 
        }
    }

//inicializar los valores del nuevo valor al historial
HistoryLine *newLine = (HistoryLine*) malloc(sizeof(HistoryLine));

//Inicializacion de los valores del comando
strncpy(newLine->command, command, MAX_CHAR_ON_LINE - 1);
strncpy(newLine->commandEditable, command, MAX_CHAR_ON_LINE - 1);
newLine->commandEditable[MAX_CHAR_ON_LINE - 1] = '\0';
newLine->command[MAX_CHAR_ON_LINE - 1] = '\0';

newLine->next = NULL;
newLine->previous = NULL;
newLine->pos = historial->nextLine;

//Comprobacion para eliminar el head si se excede el numero
if(historial->memLines >= HISTSIZE){
    HistoryLine *aux = historial->head;
    historial->head = historial->head->next;
    
    if (historial->head != NULL)
    {
        historial->head->previous = NULL;
    }
    free(aux);
    historial->memLines--;
}

//caso donde sea el primer comando guardado
if (historial->head == NULL)
{
    historial->head = newLine;
    historial->tail = newLine;
}
//caso de comando n agregado
else {
    historial->tail->next = newLine;
    newLine->previous = historial->tail;
    historial->tail = newLine;
}

historial->memLines++;
historial->nextLine++;
historial->cursor = NULL; //despues de cada comando el cursor debe volver null
}


//Borrado para evitar memory leaks al final del programa
void clear_history(HistoryPersistent *historial){

    HistoryLine *current = historial->head;
  
    while(current != NULL){
        HistoryLine *aux = current->next;
        free(current);
        current = aux;
    }
    historial->head = NULL;
    historial->tail = NULL;
    historial->memLines = 0;

}

//persistencia en disco

//cargar al historial de memoria desde el archivo de disco
void load_history_from_file(HistoryPersistent *historial){

    if(historial == NULL) return;


    FILE *fd = fopen(HISTORYINDEX_FILE, "r"); //archivo que contiene la posicion del head anterior para trackear correctamente 
    if(fd == NULL){
        return;
    }
    
    if(fscanf(fd, "%d", &historial->nextLine)){ //El proximo que se agregue desde el fichero del historial va a tener la posicion original
        
    };
    
    fclose(fd);

    fd = fopen(HISTORY_FILE, "r"); //archivo para cargar cada comando de secundaria
    if(fd == NULL){
        return;
    }
    
    char buffer[MAX_CHAR_ON_LINE];
    int lines = 0;

    while (fgets(buffer, sizeof(buffer), fd)!= NULL && lines < HISTSIZE)
    {
        add_to_history(historial, buffer); 
        lines++;
    }
    fclose(fd);

}


//guardar al realizar exit el historial de memoria en disco
void save_history_to_file(const HistoryPersistent *historial){

    if(historial == NULL || historial->head == NULL) return;

    FILE *fd = fopen(HISTORY_FILE, "w");
    if(fd == NULL){
        perror("Error al abrir el archivo");
        return;
    }

    HistoryLine *current = historial->head;
    int disklines = 0;

    while(current != NULL && disklines < HISTFILESIZE){
        fprintf(fd, "%s", current->command);
         if (current->command[strlen(current->command) - 1] != '\n') {
            fprintf(fd, "\n");
        }
        current = current->next;
        disklines++;
    }
    fclose(fd);
}


void save_index_to_file( const HistoryPersistent *historial){

     if (historial == NULL || historial->head == NULL) {

        return;
    }

    FILE *fd = fopen(HISTORYINDEX_FILE, "w");
    if(fd == NULL){
        return;
    } 

    fprintf(fd,"%d",historial->head->pos); //se guarda el valor usado luego en la proxima carga
    fclose(fd);

    return;
}


//Para viajar en el historial a traves de las flechas arriba y abajo
char* directional_arrows(HistoryPersistent *historial, char direction) {

    if (historial == NULL || historial->head == NULL) {
        return NULL; 
    }
    //la direccion hacia arriba se denota con el char A
    if (direction == 'A') { 
        
        if (historial->cursor == NULL) { //Cursor esta en el blanco default, osea el primero al abrir el programa 
       
            historial->cursor = historial->tail;
        } else if (historial->cursor->previous != NULL) { //Para subir en el historial en una pos cualesquiera, excepto la primera
            
            historial->cursor = historial->cursor->previous;
        }
       
    } 
    //hacia abajo es con B
    else if (direction == 'B') { 
        if (historial->cursor == NULL) { //ultimo al que podemos bajar 
           
            return NULL; 
        } 
        
        historial->cursor = historial->cursor->next; 
    }

        //la letra C representa derecha, y la letra C es la secuencia para Izquierda
    else if(direction == 'C' || direction == 'D'){
       //no se hace nada pq por defecto lo borra
    }

    
    if (historial->cursor != NULL) {
        return historial->cursor->commandEditable; //valor esperado a retornar
    }
    
    return NULL; 
}

