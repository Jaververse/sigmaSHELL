#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include "history.h"
#include "input.h"

char* read_line(HistoryPersistent *historial){

    

    struct termios old_t, new_t;

    //Guarda el estado en old_t
    if (tcgetattr(STDIN_FILENO, &old_t) != 0) {
        return NULL;
    }
    
    new_t = old_t;
    new_t.c_lflag &= ~(ICANON | ECHO); //Para deshabilitar el modo canonico

    tcsetattr(STDIN_FILENO, TCSANOW, &new_t); 

    int buffer_size = MAX_CHAR_ON_LINE;
    char *buffer = malloc(buffer_size);
    int position = 0;
    buffer[0] = '\0';

    printf("ucvsh> ");
    fflush(stdout);

    int c;
    while (1) {
        c = getchar(); 

       
        if (c == '\n') {
            buffer[position] = '\0';
            printf("\n");
            break;
        }

        if (c == 127 || c == 8) {
            if (position > 0) {
                position--;
                buffer[position] = '\0';
               
                printf("\b \b"); 
                fflush(stdout);
            }
            continue;
        }

        //Cadena de caracteres que denotan una secuencia de escape ANSI ESC
        //Una flecha es \033[(A o B dependiendo de la flecha)
        if (c == '\033') { 
            getchar(); 
            int letra_flecha = getchar(); 

            if (historial->cursor != NULL) {

            strncpy(historial->cursor->commandEditable, buffer, MAX_CHAR_ON_LINE - 1);
            historial->cursor->commandEditable[MAX_CHAR_ON_LINE - 1] = '\0';
            }

            char *cmd_historial = directional_arrows(historial, letra_flecha);

            while (position > 0) {
                printf("\b \b");
                position--;
            }

            if (cmd_historial != NULL) {

                strncpy(buffer, cmd_historial, buffer_size - 1);
                buffer[buffer_size - 1] = '\0';
                
                if (buffer[strlen(buffer) - 1] == '\n') {
                    buffer[strlen(buffer) - 1] = '\0';
                }
                position = strlen(buffer);
                printf("%s", buffer); 

            }
            else {
                buffer[0] = '\0';
                position = 0;
            }

            fflush(stdout);
            continue; 
        }

        if (position < buffer_size - 1) {
            buffer[position++] = c;
            putchar(c); 
            fflush(stdout);
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old_t); //Volver a activar el modo canonico

    return buffer; 
}

