#ifndef HISTORY_H
#define HISTORY_H

#define HISTFILESIZE 2000
#define HISTSIZE 2000
#define MAX_CHAR_ON_LINE 256
#define HISTORY_FILE ".ucvsh_history"

typedef struct HistoryNode{

int pos;
char command[MAX_CHAR_ON_LINE];
struct HistoryNode *next;
struct HistoryNode *previous;

} HistoryLine;

typedef struct{

HistoryLine *head;
HistoryLine *tail;
HistoryLine *cursor;
int memLines;
int nextLine;

}HistoryPersistent;

void init_history(HistoryPersistent *history);
void add_to_history(HistoryPersistent *history, const char *cmd);
void builtin_history(HistoryPersistent *history);
void clear_history(HistoryPersistent *history);

// Persistencia en Disco
void load_history_from_file(HistoryPersistent *history);
void save_history_to_file(const HistoryPersistent *history);
char* directional_arrows(HistoryPersistent *historial, char direction);

#endif