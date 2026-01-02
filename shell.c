#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "tokenizer/tokens.h"

#define BUFFER_SIZE 256
#define PROMPT "shell $ "

char prev_cmd[BUFFER_SIZE] = "";

void execute_line(char *line);
void execute_sequence(char *cmd);
void execute_redirect(char *cmd);
void execute_pipeline(char **cmd);
void execute_with_io(char **args, char *in_file, char *out_file);
void execute_source(char *filename);
int is_builtin(char **args);
char **vect_to_array(vect_t *tokens);
void free_array(char **array);
char **parse_redirection(char *cmd, char **input_file, char **output_file);
int count_cmd(char **cmd);
char **punctuation_split(char *line, char punctuation);

// Convert vector of tokens to array
char **vect_to_array(vect_t *tokens) {
  if (tokens == NULL) {
    return NULL;
  }
  size_t size = vect_size(tokens);
  char **array = malloc((size + 1) * sizeof(char *));
  for (size_t i = 0; i < size; i++) {
    array[i] = strdup(vect_get(tokens, i));
  }
  array[size] = NULL;
  return array;
}

// Free array of strings
void free_array(char **array) {
  if (array == NULL) {
    return;
  }
  for (int i = 0; array[i] != NULL; i++) {
    free(array[i]);
  }
  free(array);
}

// Count number of commands in array
int count_cmd(char **cmd) {
  int count = 0;
  while (cmd[count] != NULL) {
    count++;
  }
  return count;
}

// Split by a given punctuation
char **punctuation_split(char *line, char punctuation) {
  char *line_copy = strdup(line);
  int count = 1;
  int quotes = 0;

  char *temp = line_copy;
  while (*temp) {
    if (*temp == '"') {
      quotes = !quotes;
    } else if (*temp == punctuation && !quotes) {
      count++;
    }
    temp++;
  }

  char **result = malloc((count + 1) * sizeof(char *));
  int index = 0;
  char *start = line_copy;
  temp = line_copy;
  quotes = 0;
  while (*temp) {
    if (*temp == '"') {
      quotes = !quotes;
    } else if (*temp == punctuation && !quotes) {
      *temp = '\0';
      char *token = start;
      while (*token == ' ' || *token == '\t') {
        token++;
      } 
      char *end = temp - 1;
      while (end > token && (*end == ' ' || *end == '\t')) {
        *end = '\0';
        end--;
      }
      if (strlen(token) > 0) {
        result[index++] = strdup(token);
      }
      start = temp + 1;
    }
    temp++;
  }
  
  // Last token
  char *token = start;
  while (*token == ' ' || *token == '\t') {
    token++;
  } 
  char *end = token + strlen(token) - 1;
  while (end > token && (*end == ' ' || *end == '\t')) {
    *end = '\0';
    end--;
  }
  if (strlen(token) > 0) {
    result[index++] = strdup(token);
  }
  result[index] = NULL;
  free(line_copy);
  return result;
}

// Parse redirection operators and return clean args
char **parse_redirection(char *cmd, char **in_file, char **out_file) {
  *in_file = NULL;
  *out_file = NULL;

  vect_t *tokens = tokenize(cmd);
  vect_t *clean_tokens = vect_new();

  size_t size = vect_size(tokens);
  for (size_t i = 0; i < size; i++) {
    const char *token = vect_get(tokens, i);
    if (strcmp(token, "<") == 0 && i + 1 < size) {
      *in_file = strdup(vect_get(tokens, i + 1));
      i++;
    } else if (strcmp(token, ">") == 0 && i + 1 < size) {
      *out_file = strdup(vect_get(tokens, i + 1));
      i++;
    } else {
      vect_add(clean_tokens, token);
    }
  }
  
  char **result = vect_to_array(clean_tokens);
  vect_delete(tokens);
  vect_delete(clean_tokens);
  return result;
}

// Check and execute builtin commands
int is_builtin(char **args) {
  if (args == NULL || args[0] == NULL) {
    return 0;
  }
  
  // exit command
  if (strcmp(args[0], "exit") == 0) {
    printf("Bye bye.\n");
    exit(0);
  }

  // cd command
  if (strcmp(args[0], "cd") == 0) {
    if (args[1] == NULL) {
      fprintf(stderr, "cd: expected argument\n");
    } else if (chdir(args[1]) != 0) {
      perror("cd");
    }
    return 1;
  }

  // help command
  if (strcmp(args[0], "help") == 0) {
    printf("Built-in commands:\n");
    printf("  exit - Exit the shell\n");
    printf("  cd [dir] - Change directory to 'dir'\n");
    printf("  help - Show this help message\n");
    printf("  prev - Execute the previous command\n");
    printf("  source [file] - Execute commands from 'file'\n");
    return 1;
  }
  
  // prev command
  if (strcmp(args[0], "prev") == 0) {
    if (strlen(prev_cmd) == 0) {
      printf("No previous command.\n");
    } else {
      printf("%s\n", prev_cmd);
      execute_line(prev_cmd);
    }
    return 1;
  }

  // source command
  if (strcmp(args[0], "source") == 0) {
    if (args[1] == NULL) {
      fprintf(stderr, "source: expected filename\n");
    } else {
      execute_source(args[1]);
    }
    return 1;
  }
  
  return 0;
}

// Execute commands from a source file
void execute_source(char *filename) {
  FILE *file = fopen(filename, "r");
  if (!file) {
    perror("source");
    return;
  }

  char line[BUFFER_SIZE];
  while (fgets(line, BUFFER_SIZE, file)) {
    line[strcspn(line, "\n")] = 0; 
    if (strlen(line) > 0) {
      execute_line(line);
    }
  }

  fclose(file);
}

// Execute command with input/output redirection
void execute_with_io(char **args, char *input_file, char *output_file) {
  pid_t pid = fork();
  if (pid == 0) {
    // Child process
    // Input redirection
    if (input_file) {
      int in_fd = open(input_file, O_RDONLY);
      if (in_fd < 0) {
        perror("Input redirection");
        exit(1);
      }
      dup2(in_fd, STDIN_FILENO);
      close(in_fd);
    }

    // Output redirection
    if (output_file) {
      int out_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (out_fd < 0) {
        perror("Output redirection");
        exit(1);
      }
      fflush(stdout);
      dup2(out_fd, STDOUT_FILENO);
      close(out_fd);
    }
    
    execvp(args[0], args);
    fprintf(stderr, "%s: command not found\n", args[0]);
    exit(1);
  } else if (pid > 0) {
    int status;
    waitpid(pid, &status, 0);
  } else {
    perror("fork");
  }
}

// Execute a pipeline of commands
void execute_pipeline(char **cmd) {
  int num_cmds = count_cmd(cmd);
  int pipes[num_cmds - 1][2];

  // Create all pipes
  for (int i = 0; i < num_cmds - 1; i++) {
    if (pipe(pipes[i]) < 0) {
      perror("pipe");
      return;
    }
  }

  // Fork for each command
  for (int i = 0; i < num_cmds; i++) {
    pid_t pid = fork();
    if (pid == 0) {
      // Child process

      // Parse redirection first
      char *in_file = NULL;
      char *out_file = NULL;
      char **args = parse_redirection(cmd[i], &in_file, &out_file);
      
      if (args == NULL || args[0] == NULL) {
        exit(1);
      }

      // Redirect input from previous pipe
      if (i > 0 && in_file == NULL) {
        dup2(pipes[i - 1][0], STDIN_FILENO);
      }
      // Redirect output to next pipe
      if (i < num_cmds - 1 && out_file == NULL) {
        dup2(pipes[i][1], STDOUT_FILENO);
      }

      // Handle input redirection from file
      if (in_file) {
        int in_fd = open(in_file, O_RDONLY);
        if (in_fd < 0) {
          perror("Input redirection");
          exit(1);
        }
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
      }

      // Handle output redirection to file
      if (out_file) {
        int out_fd = open(out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd < 0) {
          perror("Output redirection");
          exit(1);
        }
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
      }
      
      // Close all pipe fds in child
      for (int j = 0; j < num_cmds - 1; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }

      execvp(args[0], args);
      fprintf(stderr, "%s: command not found\n", args[0]);
      exit(1);
    }
  }

  // Parent process - close all pipes
  for (int i = 0; i < num_cmds - 1; i++) {
    close(pipes[i][0]);
    close(pipes[i][1]);
  }
  
  // Wait for all children
  for (int i = 0; i < num_cmds; i++) {
    wait(NULL);
  }
}

// Execute command with possible redirection
void execute_redirect(char *cmd) {
  char *input_file = NULL;
  char *output_file = NULL;
  char **args = parse_redirection(cmd, &input_file, &output_file);

  if (args == NULL || args[0] == NULL) {
    if (input_file) free(input_file);
    if (output_file) free(output_file);
    free_array(args);
    return;
  }
  
  if (is_builtin(args)) {
    if (input_file) free(input_file);
    if (output_file) free(output_file);
    free_array(args);
    return;
  }

  execute_with_io(args, input_file, output_file);
  
  if (input_file) free(input_file);
  if (output_file) free(output_file);
  free_array(args);
}

// Execute a sequence of piped commands
void execute_sequence(char *cmd) {
  char **pipes = punctuation_split(cmd, '|');
  
  if (count_cmd(pipes) > 1) {
    execute_pipeline(pipes);
  } else {
    execute_redirect(pipes[0]);
  }

  free_array(pipes);
}

// Execute a line 
void execute_line(char *line) {
  char **sequences = punctuation_split(line, ';');
  for (int i = 0; sequences[i] != NULL; i++) {
    execute_sequence(sequences[i]);
  }
  free_array(sequences);
}

int main() {
  char line[BUFFER_SIZE];
  printf("Welcome to mini-shell.\n");

  while (1) {
    printf("%s", PROMPT);
    fflush(stdout);

    // Read input line and exit msg
    if (!fgets(line, BUFFER_SIZE, stdin)) {
      printf("Bye bye.\n");
      break;
    }

    // Remove newline character
    line[strcspn(line, "\n")] = 0; 

    // Ignore empty lines
    if (strlen(line) == 0) {
      continue; 
    }
    
    // Store previous command 
    if (strcmp(line, "prev") != 0) {
      strcpy(prev_cmd, line);
    }

    // Process the command
    execute_line(line);
  }
  
  return 0;
}