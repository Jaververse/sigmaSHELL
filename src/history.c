#include "history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void init_history(HistoryPersistent *historial){
    historial->head = NULL;
    historial->tail = NULL;
    historial->memLines = 0;
    historial->nextLine = 1;
}

void builtin_history(HistoryPersistent *historial){
    if(historial == NULL || historial->head == NULL || historial->memLines == 0) return;

    HistoryLine *current = historial->head;

    while(current != NULL){

        printf("%d  %s", current->pos, current->command);
        if (current->command[strlen(current->command) - 1] != '\n') {
            printf("\n");
        }
        current = current->next;
    }
}

void add_to_history(HistoryPersistent *historial, const char *command){

if(historial == NULL || command == NULL) return;

HistoryLine *newLine = (HistoryLine*) malloc(sizeof(HistoryLine));
strncpy(newLine->command, command, MAX_CHAR_ON_LINE - 1);
newLine->command[MAX_CHAR_ON_LINE - 1] = '\0';
newLine->next = NULL;
newLine->previous = NULL;
newLine->pos = historial->nextLine;

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

if (historial->head == NULL)
{
    historial->head = newLine;
    historial->tail = newLine;
}
else {
    historial->tail->next = newLine;
    newLine->previous = historial->tail;
    historial->tail = newLine;
}
historial->memLines++;
historial->nextLine++;
}

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

void load_history_from_file(HistoryPersistent *historial){

    if(historial == NULL) return;

    FILE *fd = fopen(HISTORY_FILE, "r");
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


//guardar al realizar exit
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
