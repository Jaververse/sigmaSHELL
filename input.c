#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include "history.h"
#include "input.h"

char* read_line(HistoryPersistent *historial){

    

    struct termios oldTerminal, newTerminal;

    //Guarda el estado de la terminal
    if (tcgetattr(STDIN_FILENO, &oldTerminal) != 0) {
        return NULL;
    }
    
    newTerminal = oldTerminal;
    newTerminal.c_lflag &= ~(ICANON | ECHO); //Para deshabilitar el modo canonico

    //TCANOW para poder aplicar cambios a la terminal 
    tcsetattr(STDIN_FILENO, TCSANOW, &newTerminal); 

    int buffer_size = MAX_CHAR_ON_LINE;
    char *buffer = malloc(buffer_size);
    int position = 0;
    buffer[0] = '\0';

    printf("ucvsh> ");
    fflush(stdout);

    int c;
    while (1) {
        c = getchar(); 

       //Se termina el bucle de recibimiento si presionan ENTER
        if (c == '\n') {
            buffer[position] = '\0';
            printf("\n");
            break;
        }

            //127 es el caracter ASCII de delete, el 8 es el backspace
        if (c == 127 || c == 8) {
            if (position > 0) {
                position--;
                buffer[position] = '\0';
               
                printf("\b \b"); //Borra de la terminal
                fflush(stdout);
            }
            continue;
        }

        //Cadena de caracteres que denotan una secuencia de escape ANSI ESC
        //Una flecha es \033[ seguido de (A o B dependiendo de la flecha)
        if (c == '\033') { 
            getchar(); 
            int letra_flecha = getchar(); 

                //la letra C representa derecha, y la letra C es la secuencia para Izquierda
         if(letra_flecha == 'C' || letra_flecha == 'D'){
            fflush(stdout);
            continue;
         }

            if (historial->cursor != NULL) {

            strncpy(historial->cursor->commandEditable, buffer, MAX_CHAR_ON_LINE - 1);
            historial->cursor->commandEditable[MAX_CHAR_ON_LINE - 1] = '\0';
            }

            char *cmd_historial = directional_arrows(historial, letra_flecha); //Para ver que flecha se presiono y obtener el comando de es pos a la que se movio 

            while (position > 0) {
                printf("\b \b");
                position--;
            }

            if (cmd_historial != NULL) { //Si habia un comando

                strncpy(buffer, cmd_historial, buffer_size - 1);
                buffer[buffer_size - 1] = '\0';
                
                if (buffer[strlen(buffer) - 1] == '\n') {
                    buffer[strlen(buffer) - 1] = '\0';
                }
                position = strlen(buffer);
                printf("%s", buffer); 

            }
            else {  //mantiene el espacio en blanco por default
                buffer[0] = '\0';
                position = 0;
            }

            fflush(stdout);
            continue; 
        }
        //Se escribe en pantalla caracter por caracter
        if (position < buffer_size - 1) {
            buffer[position++] = c;
            putchar(c); 
            fflush(stdout);
        }

    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldTerminal); //Volver a activar el modo canonico y restablecer la config en la terminal

    return buffer; 
}

