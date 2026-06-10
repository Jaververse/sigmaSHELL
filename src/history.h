#ifndef HISTORY_H
#define HISTORY_H

#define HISTFILESIZE 2000
#define HISTSIZE 2000
#define MAX_CHAR_ON_LINE 256
#define HISTORY_FILE ".ucvsh_history"
#define HISTORYINDEX_FILE ".indexucvsh_history"

typedef struct HistoryNode{

int pos;
char command[MAX_CHAR_ON_LINE];
char commandEditable[MAX_CHAR_ON_LINE];
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

#endif