#ifndef HISTORY_H
#define HISTORY_H

#define HISTFILESIZE 2000 //valor maximo que se puede almacenar en mem
#define HISTSIZE 2000  // "" "" "" en disco 
#define MAX_CHAR_ON_LINE 256 
#define HISTORY_FILE ".ucvsh_history" //archivo del historial
#define HISTORYINDEX_FILE ".ucvsh_index" //archivo que almacena el index del head


typedef struct HistoryNode{

int pos; //posicion del comando en la lista
char command[MAX_CHAR_ON_LINE]; //comando que va a persistir al ejecutarlo
char commandEditable[MAX_CHAR_ON_LINE]; //Comando en edicion durante la sesion
struct HistoryNode *next;
struct HistoryNode *previous;

} HistoryLine;

typedef struct{

HistoryLine *head; 
HistoryLine *tail; 
HistoryLine *cursor; //Para saber en que parte esta actualmente el historial con las felchitas
int memLines;
int nextLine;

}HistoryPersistent;

void init_history(HistoryPersistent *history);
void add_to_history(HistoryPersistent *history, const char *cmd);
void builtin_history(HistoryPersistent *history);
void clear_history(HistoryPersistent *history);
void load_history_from_file(HistoryPersistent *history);
void save_history_to_file(const HistoryPersistent *history);
char* directional_arrows(HistoryPersistent *historial, char direction);

void save_index_to_file( const HistoryPersistent *historial);

#endif