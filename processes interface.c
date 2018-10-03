//The program is an environment that creates processes which execute execute any program.
//The processes are run with a timer

#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>

#define RUNNING 1
#define NOT_RUNNING 0
#define MAX_SIZE 80
#define MAX_WORD_LEN 20
#define SUCCESS 0
#define FAIL 1
#define TERMINATED 1
#define NOT_TERMINATED 0
#define ALARMED 1
#define NOT_ALARMED 0
#define TIMER_SEC 20
#define TIMER_USEC 0

struct process {
	pid_t pid;
	int args_no;
	char **args;
	unsigned int running:1;
	struct process *next;
	struct process *prev;
};

struct process head = {0};

//Find the running process
struct process *running () {
	struct process *curr;
	
	head.running = RUNNING;
	for (curr = head.next ; curr->running != RUNNING ; curr = curr->next);
	
	head.running = NOT_RUNNING;
	
	if(curr->running == head.running) {
		return &head;
	}
	
	return curr;
}

//Find the next process to run
struct process *find_next (struct process *running) {
	
	if (running->pid == head.pid) {
		return &head;
	}
	
	if (running->next == &head) {
		return head.next;
	}
	
	return running->next;
}

//Find and delete from processes list the process with this pid
void delete (pid_t pid) {
	struct process *curr;
	int i;
	
	head.pid = pid;
	for (curr = head.next ; curr->pid != pid ; curr = curr->next);
	
	head.pid = 0;
	
	if (curr->pid == head.pid) { 
		printf("Not found\n");
	}
	else {
		for (i = 0 ; i < curr->args_no - 1 ; i++) {
			free(curr->args[i]);
			curr->args[i] = NULL;
		}
		
		free(curr->args);
		curr->args = NULL;
		
		curr->prev->next = curr->next;
		curr->next->prev = curr->prev;
		free(curr);
		curr = NULL;
	}
	
	return;
}

//The Hundler for SICHLD signal finds finds the running process and its next one, deletes the one running
//and signals SIGCONT to the next one
static void handler_sigchld(int sig) {
	struct process *curr_p, *next_p;
	
	curr_p = running();
	next_p = find_next(curr_p);
	delete (curr_p->pid);
	if (next_p->pid != head.pid) {
		next_p->running = RUNNING;
		kill(next_p->pid, SIGCONT);
	}
}

//The hundler for SIGALRM signal finds the running process and its next one, stops the running one
//and signals SIGCONT to the next one
static void handler_sigalarm (int sig) {
	struct process *curr_p, *next_p;
	
	curr_p = running();
	next_p = find_next(curr_p);
	if (next_p->pid != head.pid) {
		kill (curr_p->pid, SIGSTOP);
		curr_p->running = NOT_RUNNING;
		kill(next_p->pid, SIGCONT);
		next_p->running = RUNNING;
	}
}

//Prints all processes of the lis with their arguments
void print () {
	struct process *curr;
	int i;
	
	if (head.next == &head) {
		printf ("No process running\n");
	}
	else {
		for (curr = head.next ; curr->pid != head.pid ; curr = curr->next) {
			printf("pid: %d, name: (", curr->pid);
			for (i = 0 ; i < curr->args_no ; i++) {
				if (i != 0) {
					putchar(' ');
				}
				printf("%s", curr->args[i]);
				if (i != curr->args_no - 1) {
					putchar(',');
				}
			}
			printf(")");
			if (curr->running == RUNNING) {
				printf ("(R)");
			}
			putchar('\n');
		}
	}
	
	return;
}

//Adds a new process to the list by creating a new node
int add (pid_t pid, int args_no, char **args) {
	struct process *new_node;
	
	new_node = (struct process *) malloc(sizeof(struct process));
	if (new_node == NULL) {
		perror ("Malloc Problem");
		return FAIL;
	}
	
	head.prev->next = new_node;
	new_node->prev = head.prev;
	head.prev = new_node;
	head.prev->next = &head;
	
	new_node->pid = pid;
	new_node->args_no = args_no;
	new_node->args = args;
	new_node->running = NOT_RUNNING;
	
	return SUCCESS;
}

//Runs the program which the user asks
int exec (char buffer[MAX_SIZE]) {
	int i = 0, k, j;
	pid_t pid;
	char **args_ptr = NULL, **args = NULL;
	
	//Reads the name of the program and its arguments and stores the in the args matrix
	do {
		args_ptr = (char **) realloc (args_ptr, sizeof(char *) * (i + 1));
		if (args_ptr == NULL) {
			perror ("Realloc Problem");
			return FAIL;
		}
		if (i == 0) {
			args_ptr[i] = strtok(buffer, " ");
		}
		else {
			args_ptr[i] = strtok(NULL, " ");
		}
		i ++;
	}
	while (args_ptr[i-1] != NULL);
	
	for (j = 1, k = 0 ; j < i ; j++, k++) {
		args = (char **) realloc (args, (k + 1) * sizeof(char *));
		if (args == NULL) {
			perror ("Realloc Problem");
			return FAIL;
		}
		if (j == i - 1) {
			args[k] = NULL;
			break;
		}
		args[k] = strdup(args_ptr[j]);
		if (args[k] == NULL) {
			perror ("Realloc Problem");
			return FAIL;
		}
		if (j == i - 2) {
			args[k][strlen(args[k]) - 1] = '\0';
		}
	}
	
	//Create new process
	pid = fork ();
	if(pid == -1) {
		perror("Fork");
		free(args);
		args = NULL;
		return FAIL;
	}
	else {
		if (pid == 0) {
			//Execute the program asked
			execvp(args[0],args);
			perror("Execvp");
			free(args);
			args = NULL;
			return FAIL;
		}
		else {
			//Add new process to the list and signal SIGSTOP in to wait for its turn to come to run
			if (add(pid, k, args) == FAIL) {
				if (kill (pid, SIGTERM) == -1) {
					perror ("Kill Problem");
					return FAIL;
				}
				return FAIL;
			}
			if (head.next->pid == head.prev->pid) {
				head.next->running = RUNNING;
			}
			else {
				kill (pid, SIGSTOP);
			}
		}
	}
	
	//Free all dynamically allocated memory
	free(args_ptr);
	args_ptr = NULL;
	
	return SUCCESS;
}

//Signal SIGTERM to the process with this pid
int term (pid_t pid) {
	
	if (kill (pid, SIGTERM) == -1) {
		perror ("Kill Problem");
		return FAIL;
	}
	
	return SUCCESS;
}

//Signals SIGUSR1 to the process with this pid
int sig (pid_t pid) {
	
	if (kill (pid, SIGUSR1) == -1) {
		perror ("Kill Problem");
		return FAIL;
	}
	
	return SUCCESS;
}

int main (int argc, char *argv[]) {
	pid_t pid;
	char buffer[MAX_SIZE], cmd[5];
	struct itimerval time = { {0} };
	struct process *curr_p;
	struct sigaction act_sigalarm = { {0} }, act_sigchld = { {0} };
	sigset_t s;
	
	head.next = &head;
	head.prev = &head;
	
	//Set actions for SIGCHLD signal
	act_sigchld.sa_handler = handler_sigchld;
	act_sigchld.sa_flags = SA_NOCLDSTOP;
	if (sigaction(SIGCHLD, &act_sigchld, NULL) == -1) {
		perror ("Sigaction Problem");
		return FAIL;
	}
	
	//Block SIGALRM signal
	if (sigemptyset(&s) == -1) {
		perror ("Sigemptyset Problem");
		return FAIL;
	}
	if (sigaddset(&s, SIGALRM) == -1) {
		perror ("Sigaddset Problem");
		return FAIL;
	}
	if (sigprocmask(SIG_BLOCK, &s, NULL) == -1) {
		perror ("Sigprocmask Problem");
		return FAIL;
	}
	
	//Set actions for SIGALRM signal
	act_sigalarm.sa_handler = handler_sigalarm;
	act_sigalarm.sa_flags = SA_RESTART;
	if (sigaction(SIGALRM, &act_sigalarm, NULL) == -1) {
		perror ("Sigaction Problem");
		return FAIL;
	}
	
	//Initialize timer
	time.it_interval.tv_sec = TIMER_SEC;
	time.it_interval.tv_usec = TIMER_USEC;
	time.it_value.tv_sec = TIMER_SEC;
	time.it_value.tv_usec = TIMER_USEC;
	
	setitimer (ITIMER_REAL, &time, NULL);
	
	//Read repeatedly the user's commands until command "quit" is given
	fgets(buffer, MAX_SIZE, stdin);
	sscanf(buffer," %4s", cmd);
	
	while (strcmp (cmd, "quit") != 0) {
		//In case the user asks the execution of a program it calls exec function
		if (strcmp (cmd, "exec") == 0) {
			if (exec (buffer) == FAIL) {
				perror ("Problem in program execution\n");
			}
		}
		else {
			//In case the user asks the termination of program call the term function
			if (strcmp (cmd, "term") == 0) {
				sscanf (buffer, " %4s %d", cmd, &pid);
				if (term(pid) == FAIL) {
					if (errno == ESRCH) {
						printf("term: Non-existent pid! Try again.\n");
					}
					else {
						perror("Problem in term.\n");
					}
				}
			}
			else {
				//In case the user asks information about the processes run by this environment call print function
				if (strcmp (cmd, "info") == 0) {
					print ();
				}
				else {
					//In case the user asks to send SIGUSR1 signal to a process call sig function
					if (strcmp (cmd, "sig") == 0) {
						sscanf (buffer, " %4s %d", cmd, &pid);
						if (sig(pid) == FAIL) {
							if (errno == ESRCH) {
								printf("sig: Non-existent pid! Try again.\n");
							}
							else {
								perror("Problem in sig. Try again.\n");
							}
						}
					}
					else {
						printf ("Wrong command name! Try again.\n");
					}
				}
			}
		}
		
		//If there is at least one process to the lisy unblock SIGALRM signal
		if (head.next != &head) {
			if (sigprocmask(SIG_UNBLOCK, &s, NULL) == -1) {
				perror ("Sigprocmask Problem");
				return FAIL;
			}
		}
		else {
			//Else block
			if (head.next == &head) {
				if (sigprocmask(SIG_BLOCK, &s, NULL) == -1) {
					perror ("Sigprocmask Problem");
					return FAIL;
				}
			}
		}
		
		fgets(buffer, MAX_SIZE, stdin);
		sscanf(buffer," %4s", cmd);
	}
	
	//Terminate all process of the list with term function
	for (curr_p = head.next ; curr_p->pid == head.pid ; curr_p = curr_p->next) {
		if (term(curr_p->pid) == FAIL) {
			printf("term problem\n");
			if (errno == ESRCH) {
				printf("term: Non-existent pid! Try again.\n");
			}
			else {
				perror("Problem in term.\n");
			}
		}
	}
	
	return SUCCESS;
}