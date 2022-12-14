#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
const char *sysname = "shellax";

enum return_codes {
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t {
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};

struct dict_t { //dictionary to use in custom Uniq command
    char *line;
    int value;
    struct dict_t *next;
};

void addToDict(struct dict_t *dict, char *item){ //to use in uniq
	bool is_in = 0;
	
	while(dict){ //iterating over dictionary to see if the line has been written before
		if(strcmp(dict->line, item)==0){
			dict->value++;
			is_in = 1;  
		}
		dict = dict->next;
	}
	
	if(!is_in){
		struct dict_t *d = malloc(sizeof(struct dict_t));
		d->line = malloc(strlen(item)+1);
		strcpy(d->line, item);
		d->value = 1;
		dict = d;
	}
	
}

void printDict(struct dict_t *dict){ //to use in uniq
	while(dict){
		printf("%s", dict->line);
		dict = dict->next;
	}
}

void printDictValues(struct dict_t *dict){ //to use in uniq
	while(dict){
		printf("%d\t", dict->value);
		printf("%s", dict->line);
		dict = dict->next;
	}
}

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next) {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}
/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
  if (command->arg_count) {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next) {
    free_command(command->next);
    command->next = NULL;
  }
  free(command->name);
  free(command);
  return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
  char cwd[1024], hostname[1024];
  gethostname(hostname, sizeof(hostname));
  getcwd(cwd, sizeof(cwd));
  printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
  return 0;
}
/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
  const char *splitters = " \t"; // split at whitespace
  int index, len;
  len = strlen(buf);
  while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
  {
    buf++;
    len--;
  }
  while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
    buf[--len] = 0; // trim right whitespace

  if (len > 0 && buf[len - 1] == '?') // auto-complete
    command->auto_complete = true;
  if (len > 0 && buf[len - 1] == '&') // background
    command->background = true;

  char *pch = strtok(buf, splitters);
  if (pch == NULL) {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  } else {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1) {
    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue; // empty arg, go for next
    while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
    {
      arg++;
      len--;
    }
    while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
      arg[--len] = 0; // trim right whitespace
    if (len == 0)
      continue; // empty arg, go for next

    // piping to another command
    if (strcmp(arg, "|") == 0) {
      struct command_t *c = malloc(sizeof(struct command_t));
      int l = strlen(pch);
      pch[l] = splitters[0]; // restore strtok termination
      index = 1;
      while (pch[index] == ' ' || pch[index] == '\t')
        index++; // skip whitespaces

      parse_command(pch + index, c);
      pch[l] = 0; // put back strtok termination
      command->next = c;
      continue;
    }

    // background process
    if (strcmp(arg, "&") == 0)
      continue; // handled before

    // handle input redirection
    redirect_index = -1;
    if (arg[0] == '<')
      redirect_index = 0;
    if (arg[0] == '>') {
      if (len > 1 && arg[1] == '>') {
        redirect_index = 2;
        arg++;
        len--;
      } else
        redirect_index = 1;
    }
    if (redirect_index != -1) {
      command->redirects[redirect_index] = malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;

  // increase args size by 2
  command->args = (char **)realloc(command->args,
                                   sizeof(char *) * (command->arg_count += 2));

  // shift everything forward by 1
  for (int i = command->arg_count - 2; i > 0; --i)
    command->args[i] = command->args[i - 1];

  // set args[0] as a copy of name
  command->args[0] = strdup(command->name);
  // set args[arg_count-1] (last) to NULL
  command->args[command->arg_count - 1] = NULL;

  return 0;
}

void prompt_backspace() {
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
  int index = 0;
  char c;
  char buf[4096];
  static char oldbuf[4096];

  // tcgetattr gets the parameters of the current terminal
  // STDIN_FILENO will tell tcgetattr that it should write the settings
  // of stdin to oldt
  static struct termios backup_termios, new_termios;
  tcgetattr(STDIN_FILENO, &backup_termios);
  new_termios = backup_termios;
  // ICANON normally takes care that one line at a time will be processed
  // that means it will return if it sees a "\n" or an EOF or an EOL
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1) {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0) {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0) {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

    putchar(c); // echo the character
    buf[index++] = c;
    if (index >= sizeof(buf) - 1)
      break;
    if (c == '\n') // enter key
      break;
    if (c == 4) // Ctrl+D
      return EXIT;
  }
  if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
    index--;
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  // print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}
int process_command(struct command_t *command);
int main() {
  while (1) {
    struct command_t *command = malloc(sizeof(struct command_t));
    memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

    int code;
    code = prompt(command);
    if (code == EXIT)
      break;

    code = process_command(command);
    if (code == EXIT)
      break;

    free_command(command);
  }

  printf("\n");
  return 0;
}

int process_command(struct command_t *command) {
  int r;
  if (strcmp(command->name, "") == 0)
    return SUCCESS;

  if (strcmp(command->name, "exit") == 0)
    return EXIT;

  if (strcmp(command->name, "cd") == 0) {
    if (command->arg_count > 0) {
      r = chdir(command->args[0]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }
  
  //custom Uniq command
  if (strcmp(command->name, "uniq") == 0){
	
	
	char *buf[1024];


	for (int i=0; i<1024; i++){
	    scanf("%s", buf[i]);
	}
	
	struct dict_t *dict;
	
	int i=0;
	while (buf[i] != NULL){
		addToDict(dict, buf[i]);
		i++;
	} 
	
	if ((command->args[2] == NULL)){
	printDict;
	} else if ( (strcmp(command->args[2],"-c") == 0) || (strcmp(command->args[2],"--count") == 0)){
	printDictValues;
	}
	
	
  }

  pid_t pid = fork();
  if (pid == 0) // child
  {
    /// This shows how to do exec with environ (but is not available on MacOs)
    // extern char** environ; // environment variables
    // execvpe(command->name, command->args, environ); // exec+args+path+environ

    /// This shows how to do exec with auto-path resolve
    // add a NULL argument to the end of args, and the name to the beginning
    // as required by exec

    // TODO: do your own exec with path resolving using execv()
    // do so by replacing the execvp call below
 
    // I/O REDIRECTION: (modified code from: https://stackoverflow.com/a/52940098)
  
    int in;
    int out; 
   
    if (command->redirects[0] != NULL) {        // looking for input character
		
	if ((in = open(command->redirects[0], O_RDONLY)) < 0) {   // open file for reading
		fprintf(stderr, "error opening file\n");
	}
	dup2(in, STDIN_FILENO);         // duplicate stdin to input file  
	close(in);                      // close after use
		
    }                                   // end input chech

    if (command->redirects[1] != NULL) {        // looking for output character
		
	out = creat(command->redirects[1], 0777); // create new output file
	dup2(out, STDOUT_FILENO);       // redirect stdout to file  
	close(out);                     // close after use
		
    }                                   // end output check

    if (command->redirects[2] != NULL) {       // looking for append
		
	int append = open(command->redirects[2], O_CREAT | O_RDWR | O_APPEND, 0777);

	dup2(append, STDOUT_FILENO);
	close(append);
		
    }
    
    //Handling piping (modified the code from: https://stackoverflow.com/a/8439286)
    
    int pipe_count = 0; //counter to get number of pipes
    
    struct command_t *first_command = command;
    
    while(command->next != NULL){
    	pipe_count = pipe_count + 1;
    	command = command->next;
    }
    
    command = first_command;
    
    printf("Pipe Count: %d\n", pipe_count);
    
    if(pipe_count>0){
	    int numPipes = pipe_count;


	    int status;
	    int i = 0;
	    pid_t pid_pipe;

	    int pipefds[2*numPipes];

	    for(i = 0; i < (numPipes); i++){
		if(pipe(pipefds + i*2) < 0) {
		    perror("couldn't pipe");
		    exit(EXIT_FAILURE);
		}
	    }


	    int j = 0;
	    while(command) {
		pid_pipe = fork();
		if(pid_pipe == 0) {

		    //if not last command
		    if(command->next){
		        if(dup2(pipefds[j + 1], 1) < 0){
		            perror("dup2");
		            exit(EXIT_FAILURE);
		        }
		    }

		    //if not first command&& j!= 2*numPipes
		    if(j != 0 ){
		        if(dup2(pipefds[j-2], 0) < 0){
		            perror(" dup2");///j-2 0 j+1 1
		            exit(EXIT_FAILURE);

		        }
		    }


		    for(i = 0; i < 2*numPipes; i++){
		            close(pipefds[i]);
		    }

		    if( execvp(command->name, command->args) < 0 ){
		            perror(command->name);
		            exit(EXIT_FAILURE);
		    }
		} else if(pid_pipe < 0){
		    perror("error");
		    exit(EXIT_FAILURE);
		}

		command = command->next;
		j+=2;
	    }
	    /**Parent closes the pipes and wait for children*/

	    for(i = 0; i < 2 * numPipes; i++){
		close(pipefds[i]);
	    }

	    for(i = 0; i < numPipes + 1; i++)
		wait(&status);
	    
	    exit(1);
    
    }
    /*if (pipe_count > 0) { //checking if there are "|"s
    	int fd[2]; //two pipes for each consecutive process pair as in -> first process | Nx process | last process. One for writing one for reading
		   //int fd[n][2]; // fd[n][0]->READ END; fd[n][1]->WRITE END
	    
	    	
	if (pipe(fd) == -1) { //creating pipe
		fprintf(stderr,"Pipe failed");
		return 1;
	}

  	
    	pid_t pid_first = fork(); 
    	
    	if (pid_first == 0) { //first process as in first_process | second_process
	    	dup2(fd[1], STDOUT_FILENO); //redirecting output of the first process to the pipe()
		close(fd[0]);
		close(fd[1]);
		execvp(command->name, command->args);
	}
	
	
	
	pid_t pid_last = fork();
	
	if (pid_last == 0) { //last process
		dup2(fd[0], STDIN_FILENO); //redirecting output of the latest process to the input of the last process
		close(fd[0]);
		close(fd[1]);
		execvp(command->next->name, command->next->args);
	}
	
	close(fd[0]);
	close(fd[1]);
	waitpid(pid_first, NULL, 0);
	waitpid(pid_last, NULL, 0);
	exit(1);
    	
    	
    	
    	
    }*/
    
    //Specifying which environment to get value of 
    char *envvar = "PATH";

    
    // Make sure envvar actually exists
    if(!getenv(envvar)){
        fprintf(stderr, "The environment variable %s was not found.\n", envvar);
        exit(1);
    }

    char *path = getenv(envvar);
    
    int cur_bufsize = strlen(path);
    
    int position = 0;
    char **tokens = malloc(cur_bufsize * sizeof(char)); //the maximum number of tokens is the length of the pre-tokenized string 
    char *token;
    
    //Tokenising $PATH to get different paths
    token = strtok(path, ":");
    while(token != NULL){
        printf("PATH: %s\n", token);
        
        tokens[position] = token;
        printf("PATH: %s\n", tokens[position]);
        
        token = strtok(NULL, ":");
        
        position = position + 1;
    }
    
    int tokens_length = strlen(path);
    printf("Tokens length: %d\n", tokens_length);
    //Iterating until a path works
    printf("%s %s\n", command->name, *(command->args));
    
    
    position = 0;
    
    //concatting name of the command at the end of each path
    int token_length = strlen(tokens[position]);
    int command_name_length = strlen(command->name);
    int total_length = token_length + command_name_length + 2; //+1 is due to "/"
    
    char *execv_path = (char *) malloc(total_length * sizeof(char));
    strcat(execv_path, tokens[position]);
    strcat(execv_path, "/");
    strcat(execv_path, command->name);
    printf("%s\n", execv_path);
    
    int err = execv(execv_path, command->args);
    
    
    
    while ( (err == -1) && (position<tokens_length) ){  //execv returns -1 if the path is not executable
    	printf("Error occured\n");
    	printf("Position: %d\n", position);
    	
    	position = position + 1;
    	
    	token_length = strlen(tokens[position]);
    	command_name_length = strlen(command->name);
    	total_length = token_length + command_name_length + 2; //+1 is due to "/"
    	
    	char *execv_path = (char *) malloc(total_length * sizeof(char));
    	strcat(execv_path, tokens[position]);
   	strcat(execv_path, "/");
    	strcat(execv_path, command->name);
    	printf("%s\n", execv_path);
    	
    	
    	err = execv(execv_path, command->args);
    	
    	
    }
    
    if(err>=0){ printf("Executed");}
    //execvp(command->name, command->args); // exec+args+path
    exit(0);
  } else {
    // TODO: implement background processes here
    bool background = 0;
    int c=0;
    
    while(command->args[c]){
    	if ( strcmp(command->args[c], "&")==0) background=1;
    	c++;
    }
    
    
    if (!background) wait(0); // wait for child process to finish if there is no "&"
    return SUCCESS;
  }

   // TODO: your implementation here
  
   
      
   //execvp(command->name, command->args);                  // execute in parent
   fprintf(stderr, "error in child execi \n"); // error
   exit(0);
  

  printf("-%s: %s: command not found\n", sysname, command->name);
  return UNKNOWN;
}
