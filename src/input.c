#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include "history.h"
#include "input.h"

char* read_line(HistoryPersistent *historial){

    struct termios old_t, new_t;
    
    // 1. OBTENER Y CONFIGURAR TERMIOS
    // Guardamos la configuración actual de la terminal para no romperla al salir
    tcgetattr(STDIN_FILENO, &old_t); 
    new_t = old_t;
    
    // Apagamos el modo canónico (ICANON) y el eco automático (ECHO)
    new_t.c_lflag &= ~(ICANON | ECHO); 
    // Aplicamos los cambios inmediatamente (TCSANOW)
    tcsetattr(STDIN_FILENO, TCSANOW, &new_t); 

    // 2. PREPARAR BUFFERS
    int buffer_size = MAX_CHAR_ON_LINE;
    char *buffer = malloc(buffer_size);
    int position = 0;
    buffer[0] = '\0';

    int c;
    while (1) {
        c = getchar(); // Lee un solo carácter en bruto del teclado

        // Si el usuario presiona ENTER
        if (c == '\n') {
            buffer[position] = '\0';
            printf("\n");
            break;
        }

        // Si el usuario presiona BACKSPACE (Manejo manual del borrado)
        if (c == 127 || c == 8) {
            if (position > 0) {
                position--;
                buffer[position] = '\0';
                // El truco visual de terminal: retrocede, sobreescribe con espacio, retrocede
                printf("\b \b"); 
                fflush(stdout);
            }
            continue;
        }

        // 3. CAPTURAR SECUENCIAS DE ESCAPE (FLECHAS)
        if (c == '\033') { 
            getchar(); // Consumimos y descartamos el segundo byte: '['
            int letra_flecha = getchar(); // Leemos el tercer byte ('A' o 'B')

            // Llamamos a tu función modular O(1) del historial
            char *cmd_historial = directional_arrows(historial, letra_flecha);

            // Borramos visualmente todo lo que el usuario tenía escrito en la pantalla actual
            while (position > 0) {
                printf("\b \b");
                position--;
            }

            if (cmd_historial != NULL) {
                // Copiamos el comando del historial al buffer actual de lectura
                strncpy(buffer, cmd_historial, buffer_size - 1);
                buffer[buffer_size - 1] = '\0';
                
                // Limpiamos saltos de línea molestos que arrastre el archivo
                if (buffer[strlen(buffer) - 1] == '\n') {
                    buffer[strlen(buffer) - 1] = '\0';
                }
                
                position = strlen(buffer);
                printf("%s", buffer); // Pintamos el comando viejo en pantalla
            } else {
                // Si directional_arrows devolvió NULL, regresamos a la línea vacía
                buffer[0] = '\0';
                position = 0;
            }
            fflush(stdout);
            continue; // Saltamos al siguiente ciclo para no guardar los bytes de control
        }

        // 4. CARÁCTER NORMAL
        // Si escribe texto normal, lo guardamos y hacemos el ECO de forma manual
        if (position < buffer_size - 1) {
            buffer[position++] = c;
            putchar(c); // Lo pintamos en pantalla manualmente ya que apagamos el ECHO nativo
            fflush(stdout);
        }
    }

    // 5. RESTAURAR LA TERMINAL
    // Devolvemos a Linux su configuración original antes de entregar el string al main
    tcsetattr(STDIN_FILENO, TCSANOW, &old_t);
    
    return buffer; 
}

