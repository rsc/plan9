int ungzip(int);
void _ungzip(int, int);
int unbflz(int);
int xexpand(int);
void *emalloc(ulong);
void *erealloc(void*, ulong);
char *estrdup(char*);

void ramfsmain(int, char**);
extern int chatty;
void error(char*, ...);
