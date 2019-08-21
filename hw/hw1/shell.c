#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);
int cmd_wait(struct tokens *tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_exit, "exit", "exit the command shell"},
  {cmd_pwd, "pwd", "prints the current working directory"},
  {cmd_cd, "cd", "changes the current working directory to that directory"},
  {cmd_wait, "wait", "waits until all background jobs have terminated before returning to the prompt"},
};

/*waits until all background jobs have terminated before returning to the prompt*/
int cmd_wait(struct tokens *tokens) {
  while (wait(NULL) != -1) {

  }
  return 0;
}

/*changes the current working directory to that directory*/
int cmd_cd(unused struct tokens *tokens) {
  int rtv = chdir(tokens_get_token(tokens, 1));
  if (rtv == -1) {
    perror("chdir");
  }
  return 1;
}

/*prints the current working directory*/
int cmd_pwd(unused struct tokens *tokens) {
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    perror("getcwd");
  }
  fprintf(stdout, "%s\n", cwd);
  return 1;
}

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens *tokens) {
  exit(0);
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

void program_execution(struct tokens *tokens, int STDIN_FILENO_BACKUP, int STDOUT_FILENO_BACKUP, int run_bg);
void myexecv(const char *path, char *const argv[], int STDIN_FILENO_BACKUP, int STDOUT_FILENO_BACKUP, int run_bg);
char* getpath(struct tokens *tokens);
char** getargv(struct tokens *tokens);
char* getPATH();
char* IO_redirection_andsetrun_bg(struct tokens *tokens, int *run_bg);
void signal_ignore() {
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGKILL, SIG_IGN);
  signal(SIGTERM, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);
  signal(SIGCONT, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
}
void signal_default() {
  signal(SIGINT, SIG_DFL);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGKILL, SIG_DFL);
  signal(SIGTERM, SIG_DFL);
  signal(SIGTSTP, SIG_DFL);
  signal(SIGCONT, SIG_DFL);
  signal(SIGTTIN, SIG_DFL);
  signal(SIGTTOU, SIG_DFL);
}

int strisallspace(char *line) {
  for (unsigned int i = 0; i < strlen(line); i++){
    if (!isspace(line[i])) {
      return 0;
    }
  }
  return 1;
}

int main(unused int argc, unused char *argv[]) {
  init_shell();
  signal_ignore();

  static char line[4096];
  int line_num = 0;

  int STDIN_FILENO_BACKUP = dup(STDIN_FILENO);
  int STDOUT_FILENO_BACKUP = dup(STDOUT_FILENO);

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {

    if (strisallspace(line)) {
      if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);
      continue;
    }
    
    /* Split our line into words. */
    struct tokens *tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      int run_bg = 0;
      char * newline = IO_redirection_andsetrun_bg(tokens, &run_bg);
      tokens_destroy(tokens);
      if (newline == NULL) {
        if (shell_is_interactive)
        /* Please only print shell prompts when standard input is not a tty */
        fprintf(stdout, "%d: ", ++line_num);
        continue;
      }
      tokens = tokenize(newline);

      program_execution(tokens, STDIN_FILENO_BACKUP, STDOUT_FILENO_BACKUP, run_bg);
      
      free(newline);
      dup2(STDIN_FILENO_BACKUP, STDIN_FILENO);
      dup2(STDOUT_FILENO_BACKUP, STDOUT_FILENO);
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
    
  }
  return 0;
}

/*set fd and change line*/
char* IO_redirection_andsetrun_bg(struct tokens *tokens, int *run_bg) {
  char *newline = calloc(4096, sizeof(char));
  for (unsigned int i = 0; i < tokens_get_length(tokens); i++) {
    if (strcmp(tokens_get_token(tokens, i), "<") == 0) {
      i++;
      char *inputfile = tokens_get_token(tokens, i);
      int inputfd = open(inputfile, O_RDONLY);      
      if (inputfd == -1) {
        perror("open");
        return NULL;
      } else {
        dup2(inputfd, STDIN_FILENO);
        close(inputfd);
      }
    } else if (strcmp(tokens_get_token(tokens, i), ">") == 0) {
      i++;
      char *outputfile = tokens_get_token(tokens, i);
      int outputfd = open(outputfile, O_WRONLY|O_CREAT|O_TRUNC, 0777);
      if (outputfd == -1) {
        perror("open");
        return NULL;
      } else {
        dup2(outputfd, STDOUT_FILENO);
        close(outputfd);
      }
    } else if (strcmp(tokens_get_token(tokens, i), "&") == 0) {
      *run_bg = 1;
    } else {
      strcat(newline, tokens_get_token(tokens, i));
      strcat(newline, " ");
    }
  }
  return newline;
}

void forloop_path_resolution(char *program_name, char* onePATH, char * argv[], int STDIN_FILENO_BACKUP, int STDOUT_FILENO_BACKUP, int run_bg);

void program_execution(struct tokens *tokens, int STDIN_FILENO_BACKUP, int STDOUT_FILENO_BACKUP, int run_bg) {
  char *path = getpath(tokens);
  char **argv = getargv(tokens);
  char *PATH = getPATH();
  if (strchr(path, '/') != NULL) {
    myexecv(path, argv, STDIN_FILENO_BACKUP, STDOUT_FILENO_BACKUP, run_bg);
  } else {
    char *onePATH = strtok(PATH, ":");
    forloop_path_resolution(path, onePATH, argv, STDIN_FILENO_BACKUP, STDOUT_FILENO_BACKUP, run_bg);
  }
  free(path);
  free(argv);
  free(PATH);
}

char *getPATH() {
  char *PATH = calloc(strlen(getenv("PATH")) + 1, sizeof(char));
  strcpy(PATH, getenv("PATH"));
  return PATH;
}

void forloop_path_resolution(char *program_name, char* onePATH, char * argv[], int STDIN_FILENO_BACKUP, int STDOUT_FILENO_BACKUP, int run_bg) {
  pid_t child = fork();
  if (child == -1) {
    perror("fork");
  } else if (child == 0) {

    if (setpgid(0, 0) == -1) {
      perror("setpgid");
    }
    if (run_bg == 0) {
      tcsetpgrp(STDIN_FILENO_BACKUP, getpgrp());
    }
    signal_default();

    while (onePATH != NULL) {
      int full_path_len = strlen(program_name) + strlen(onePATH) + 2;
      char full_path[full_path_len];
      memset(full_path, 0, full_path_len);
      strcat(full_path, onePATH);
      strcat(full_path,"/");
      strcat(full_path, program_name);
      argv[0] = full_path;
      execv(full_path, argv);
      onePATH = strtok(NULL, ":");
    }
    fprintf(stdout, "can't excute\n");
    exit(1);  
  } else {
    if (run_bg == 0) {
      if (waitpid(child, NULL, 0) == -1) {
        perror("waitpid");
      }
      tcsetpgrp(STDIN_FILENO_BACKUP, shell_pgid);
    }
  }
}

char* getpath(struct tokens *tokens) {
  char *path = tokens_get_token(tokens, 0);
  char *newpath = calloc(strlen(path) + 1, sizeof(char));
  strcpy(newpath, path);
  return newpath;
}

char** getargv(struct tokens *tokens) {
  int tokens_length = tokens_get_length(tokens);
  char **argv = calloc(tokens_length + 1, sizeof(char*));
  for (unsigned int i = 0; i < tokens_length; i++) {
    argv[i] = tokens_get_token(tokens, i);
  }
  argv[tokens_length] = NULL;
  return argv;
}

void myexecv(const char *path, char *const argv[], int STDIN_FILENO_BACKUP, int STDOUT_FILENO_BACKUP, int run_bg) {
  pid_t child;
  child = fork();
  if (child == -1) {
    perror("fork");
  } else if (child == 0) {

    if (setpgid(0, 0) == -1) {
      perror("setpgid");
    }
    if (run_bg == 0) {
      tcsetpgrp(STDIN_FILENO_BACKUP, getpgrp());
    }
    signal_default();  

    if (execv(path, argv) == -1) {
      perror("execv");
      exit(1);
    }
  } else {
    if (run_bg == 0) {
      if (waitpid(child, NULL, 0) == -1) {
        perror("waitpid");
      }
      tcsetpgrp(STDIN_FILENO_BACKUP, shell_pgid);
    }
  }  
}