#define _POSIX_C_SOURCE 200809L //macro necesaria para evitar problemas con el sigaction

#include "signKeyboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>


void handler(int sign){

    //Solo implementa que no se cierre el programa
    if(sign == SIGINT){ //Ctrl+C
        printf("\nucvsh> ");
        fflush(stdout);
    }
    if(sign == SIGTSTP){//Crtl+Z
        //Por ahora no esta implementado
    } 

}


void init_signals(){
    struct sigaction zorin;
    zorin.sa_handler = handler;
    zorin.sa_flags = SA_RESTART; 
    sigemptyset(&zorin.sa_mask);

    
    sigaction(SIGINT, &zorin, NULL);
   // sigaction(SIGTSTP, &zorin, NULL);
    

    return;

}