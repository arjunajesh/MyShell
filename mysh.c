#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <glob.h>
#include "macros.h"

#define PROMPT() write(STDOUT_FILENO, "mysh> ", 6);

#define BUFFER_SIZE 1024
#define TOKENS_SIZE_INITIAL 8
#define INITIAL_TOKEN_CAPACITY 24
#define USER_LOCAL_BIN "/usr/local/bin"
#define USER_BIN "/usr/bin"
#define BIN "/bin"
#define PIPELINE_SIZE_INITIAL 8
#define OUTPUT_REDIRECT_MODE S_IRUSR|S_IWUSR|S_IRGRP
#define OUTPUT_REDIRECT_FLAGS O_WRONLY|O_CREAT|O_TRUNC

typedef enum CONDITIONAL {
    NONE, ELSE, THEN
} Conditional;

typedef struct JOB_ORDER {
    char** argv;
    int argc;
    char* inputRedirect;
    char* outputRedirect;
    char* execPath;
    Conditional conditional;
} JobOrder;

int lastCommandSuccessful = 1;

void println (char* message) {
    if (write(STDOUT_FILENO, message, strlen(message)) == -1) PERROR();
    if (write(STDOUT_FILENO, "\n", 1) == -1) PERROR();
}

void printJobOrder (JobOrder job) {
    printf("execPath: %s\n", job.execPath);
    printf("argc: %d\n", job.argc);
    printf("argv: ");
    for (int i = 0; i < job.argc; i++) {
        printf("%s ", job.argv[i]);
    }
    printf("\n");
    printf("inputRedirect: %s\n", job.inputRedirect);
    printf("outputRedirect: %s\n", job.outputRedirect);
    printf("conditional: %d\n", job.conditional);
}

char* getPath (char* pathStart, char* fileName) {  //? CHANGES: sprintf
    char* path = malloc(strlen(fileName)+strlen(pathStart)+2);
    if (!path) {
        OUT_OF_MEMORY();
    }
    sprintf(path, "%s/%s", pathStart, fileName);
    return path;
}

char* copyToken (char* token) {
    char* copy = malloc(strlen(token)+1);
    if (!copy) {
        OUT_OF_MEMORY();
    }
    strcpy(copy, token);
    return copy;
}

// returns 0/-1, copies path to buffer, checks working directory for executable
int getExecutablePath(char* cmd, char** buf) {
    //if path is a built in, set buff to cmd
    if (!strcmp(cmd, "cd") || !strcmp(cmd, "pwd") ||
        !strcmp(cmd, "which") || !strcmp(cmd, "exit")) {
        *buf = copyToken(cmd);
        return 0;
    }
    // cmd is a path to an executable:
    // If cmd path points to a valid file, set buff to cmd
    // Else, print error and 
    if (strstr(cmd, "/")) {
        struct stat statBuff;
        if (stat(cmd, &statBuff) != -1 && S_ISREG(statBuff.st_mode)) {
            *buf = copyToken(cmd);
            return 0;
        }
        return -1;
    }

    char* path;
    // search /usr/local/bin
    if (access(USER_LOCAL_BIN, F_OK) == 0) {
        path = getPath(USER_LOCAL_BIN, cmd);
        if(!access(path, X_OK)) {
            *buf = path;
            return 0;
        }
        free(path);
    }
    // search /usr/bin
    if (access(USER_BIN, F_OK) == 0) {
        path = getPath(USER_BIN, cmd);
        if(!access(path, X_OK)) {
            *buf = path;
            return 0;
        }

        free(path);
    }
    // search /bin
    if (access(BIN, F_OK) == 0) {
        path = getPath(BIN, cmd);
        if(!access(path, X_OK)) {
            *buf = path;
            return 0;
        }
        free(path);
    }
    return -1;
}

void executePWD() {
    char wd[BUFFER_SIZE];
    getcwd(wd, BUFFER_SIZE);
    println(wd);
}

// RETURN 0 on success, -1 on failure
int executeCD(JobOrder job) {
    if (job.argc < 2) {
        printf("cd: no argument provided\n");
        return -1;
    }
    if (job.argc > 2) {
        printf("cd: too many arguments");
        return -1;
    }
    if((chdir(job.argv[1])) != 0) {
        perror("cd");
        return -1;
    }
    return 0;
}

// return 0 if fail, 1 if succeed
int executeWHICH(JobOrder job) {
    if (job.argc < 2) return 0; 
    char* path = NULL;
    if((getExecutablePath(job.argv[1], &path)) == 0) {
        println(path);
        free(path);
        return 1;
    }
    if (path) free(path);
    return 0;
}

int checkConditional(JobOrder job) {
    // printf("ENUM: %d, lastCMD: %d\n", job.conditional, lastCommandSuccessful);
    if(job.conditional == THEN && !lastCommandSuccessful) return -1;
    if(job.conditional == ELSE && lastCommandSuccessful) return -1;
    return 0;
}


void executeSingleJob (JobOrder job){
    if(checkConditional(job) != 0){
        lastCommandSuccessful = 0;
        return;
    }

    if (!strcmp(job.argv[0], "exit")) {
        exit(EXIT_SUCCESS);
    }

    // cd should be run in parent process
    if (!strcmp(job.argv[0], "cd")) {
        if(executeCD(job) != 0) lastCommandSuccessful = 0; 
        return;
    }

    pid_t pid = fork();

    if(pid == -1){
        perror("error forking in ExecuteProgram()");
        exit(EXIT_FAILURE);
    }
    //CHILD PROCESS
    if(pid == 0) {

        if(job.inputRedirect) {
            int input_file = open(job.inputRedirect, O_RDONLY);
            if (input_file == -1) {
                perror("error");
                return; 
            }
            if(dup2(input_file, STDIN_FILENO) == -1){
                perror("dup2");
                return; 
            }
            close(input_file);
        }
        if(job.outputRedirect) {
            int output_file = open(job.outputRedirect, OUTPUT_REDIRECT_FLAGS, OUTPUT_REDIRECT_MODE);
            if (output_file == -1){
                perror("error");
                return; 
            }
            if (dup2(output_file, STDOUT_FILENO) == -1){
                perror("dup2");
                return; 
            }
            close(output_file);
        }
        if (!strcmp(job.argv[0], "pwd")) {
            executePWD();
            exit(EXIT_SUCCESS);
        }
        else if (!strcmp(job.argv[0], "which")) {
            if (executeWHICH(job)) exit(EXIT_SUCCESS);
            exit(EXIT_FAILURE);
        }
        if (execv(job.execPath, job.argv) == -1) {
            perror("Error executing program in child process");
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    }
    //PARENT PROCESS
    else {
        int status;
        waitpid(pid, &status, 0); 
        lastCommandSuccessful = (WEXITSTATUS(status) == EXIT_SUCCESS);
    }
}

void executeCommand(JobOrder job){
    if (!strcmp(job.argv[0], "cd")) {
        if(executeCD(job) == 0) exit(EXIT_SUCCESS);
        else exit(EXIT_FAILURE);
    }
    if (!strcmp(job.argv[0], "pwd")) {
        executePWD();
        exit(EXIT_SUCCESS);
    }
    else if (!strcmp(job.argv[0], "which")) {
        if (executeWHICH(job)) exit(EXIT_SUCCESS);
        exit(EXIT_FAILURE);
    }
    else if (!strcmp(job.argv[0], "exit")) {
        exit(EXIT_SUCCESS);
    }
    if (execv(job.execPath, job.argv) == -1) {
        perror("Error executing program in child process");
        exit(EXIT_FAILURE);
    }
}

// This assumes that pipeline has 2 jobs: "pipeline[0] | pipeline[1]"
void executePipedJobs (JobOrder* pipeline) {
    JobOrder job1 = pipeline[0];
    JobOrder job2 = pipeline[1];
    int pipefd[2];
    if(pipe(pipefd) == -1){
        perror("pipe");
        return;
    }

    pid_t pid1 = fork();
    if(pid1 == -1) {
        perror("fork");
        return;
    }
    // CHILD PROCESS: JOB 1 
    if (pid1 == 0) { 
        if (job1.inputRedirect) {
            int input_file = open(job1.inputRedirect, O_RDONLY);
            if (input_file == -1) {
                perror("error");
                return; 
            }
            if(dup2(input_file, STDIN_FILENO) == -1) {
                perror("dup2");
                return; 
            }
            close(input_file);
        }
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
            perror("dup2");
            return;
        }
        close(pipefd[1]);

        if (job1.outputRedirect) {
            int output_file = open(job1.outputRedirect, OUTPUT_REDIRECT_FLAGS, OUTPUT_REDIRECT_MODE);
            if (output_file == -1) {
                perror("error");
                return;
            }
            if (dup2(output_file, STDOUT_FILENO) == -1) {
                perror("dup2");
                return;
            }
            close(output_file);
        }
        
        executeCommand(job1);
        exit(EXIT_SUCCESS);
    }

    pid_t pid2 = fork();
    if(pid2 == -1){
        perror("fork");
        return;
    }
    //CHILD PROCESS: JOB 2
    if(pid2 == 0){
        if(job2.outputRedirect){
            int output_file = open(job2.outputRedirect, OUTPUT_REDIRECT_FLAGS, OUTPUT_REDIRECT_MODE);
            if (output_file == -1) {
                perror("error");
                return; 
            }
            if(dup2(output_file, STDOUT_FILENO) == -1){
                perror("dup2");
                return; 
            }
            close(output_file);
        }
        close(pipefd[1]);
        if(dup2(pipefd[0], STDIN_FILENO) == -1){
            perror("dup2");
            return;
        }
        close(pipefd[0]);

        if (job2.inputRedirect) {
            int input_file = open(job2.inputRedirect, O_RDONLY);
            if (input_file == -1) {
                perror("error");
                return;
            }
            if (dup2(input_file, STDIN_FILENO) == -1) {
                perror("dup2");
                return;
            }
            close(input_file);
        }
        
        executeCommand(job2);
        exit(EXIT_SUCCESS);
    }
    
    close(pipefd[0]);
    close(pipefd[1]);
    int job1Status, job2Status;

    waitpid(pid1, &job1Status, 0);
    waitpid(pid2, &job2Status, 0);

    lastCommandSuccessful = (WEXITSTATUS(job1Status) == EXIT_SUCCESS 
                            && WEXITSTATUS(job2Status) == EXIT_SUCCESS);
    
}

// jobCounter is either 1 or 2
// If 1, just run the command
// If 2, run the 2 commands in a pipe
void executeJobs (JobOrder* pipeline, int jobCounter) {
    // Handle single command
    if (jobCounter == 1) {
        executeSingleJob(pipeline[0]);
        return;
    }
    // Handle piped commands
    executePipedJobs(pipeline);
    
}

JobOrder initJobOrder () {
    return (JobOrder) {
        .argv = NULL, .argc = 0, .inputRedirect = NULL,
        .outputRedirect = NULL, .execPath = NULL,
        .conditional = NONE
    };
}

void destroyJobOrder (JobOrder job) {
    free(job.execPath);
    for (int i = 0; i < job.argc; i++)
        free(job.argv[i]);
    free(job.argv);
    if (job.inputRedirect) free(job.inputRedirect);
    if (job.outputRedirect) free(job.outputRedirect);
}

void addToTokens (int* tokenCapacity, int tokenCount, char*** tokens, char* token) {
    if (tokenCount == *tokenCapacity) {
        *tokenCapacity *= 2;
        *tokens = realloc(*tokens, (*tokenCapacity)*sizeof(char*));
        if (!(*tokens)) {
            OUT_OF_MEMORY();
        }
    }
    (*tokens)[tokenCount] = token;
}

void addToPipeline (JobOrder** jobPipeline, JobOrder job, int jobCounter, int* jobCapacity) {
    if (jobCounter == *jobCapacity) {
        *jobCapacity *= 2;
        *jobPipeline = realloc(*jobPipeline, *jobCapacity);
        if (!(*jobPipeline)) {
            OUT_OF_MEMORY();
        }
    }
    (*jobPipeline)[jobCounter] = job;
}

void expandWildcard (JobOrder* currentJob, int* argvCapacity, char* wildcard) {
    glob_t pglob;
    int result = glob(wildcard, GLOB_NOESCAPE|GLOB_NOCHECK, NULL, &pglob);
    if (!result) {
        for (int i = 0; i < pglob.gl_pathc; i++) {
            addToTokens(argvCapacity, currentJob->argc, &(currentJob->argv), copyToken(pglob.gl_pathv[i]));
            (currentJob->argc)++;
        }
        globfree(&pglob);
        return;
    }
    // Handle error cases
    switch (result) {
        case GLOB_NOSPACE:
            OUT_OF_MEMORY();
        case GLOB_ABORTED:
            addToTokens(argvCapacity, currentJob->argc, &(currentJob->argv), copyToken(wildcard));
            (currentJob->argc)++;
            break;
    }
    globfree(&pglob);
}

int isWildcard (char* token) {
    char* firstStar = strchr(token, '*');
    if (!firstStar) return 0;
    // Just pray to every god of every religion that the length of the token doesn't vastly exceed MAX_INT
    // There shouldn't be any '/'s after the star; the star should be in the filename rather than the path
    for (int i = (int)(firstStar-token)+1; i < strlen(token); i++)
        if (token[i] == '/') return 0;
    return 1;
}

void parseTokens (char** tokens) { //? CHANGES: modified to use new findExecuteablePath function
    int jobCounter = 0;
    int jobCapacity = PIPELINE_SIZE_INITIAL;

    JobOrder* jobPipeline = malloc(jobCapacity*sizeof(JobOrder));
    JobOrder currentJob = initJobOrder();;

    int newJob = 1;
    int redirectInput = 0;
    int redirectOutput = 0;
    int error = 0;
    int clipToTwo = 0;

    int argvCapacity = TOKENS_SIZE_INITIAL;

    int i = 0;
    while (tokens[i]) {
        if (!strcmp(tokens[i], "then")) {
            if (!i) {
                currentJob.conditional = THEN;
                i++;
                continue;
            }
        }

        if (!strcmp(tokens[i], "else")) {
            if (!i) {
                currentJob.conditional = ELSE;
                i++;
                continue;
            }
        }

        if (!strcmp(tokens[i], "<")) {
            if (newJob) {
                printf("ERROR: redirect with no specified program\n");
                error = 1;
                break;
            }
            if (redirectOutput || redirectInput) {
                printf("ERROR: no redirect target provided\n");
                error = 1;
                break;
            }
            if (currentJob.inputRedirect) {
                printf("ERROR: cannot redirect input multiple times\n");
                error = 1;
                break;
            }
            redirectInput = 1;
            i++;
            continue;
        }

        if (!strcmp(tokens[i], ">")) {
            if (newJob) {
                printf("ERROR: redirect with no specified program\n");
                error = 1;
                break;
            }
            if (redirectOutput || redirectInput) {
                printf("ERROR: no redirect target provided\n");
                error = 1;
                break;
            }
            if (currentJob.outputRedirect) {
                printf("ERROR: cannot redirect output multiple times\n");
                error = 1;
                break;
            }
            redirectOutput = 1;
            i++;
            continue;
        }

        if (!strcmp(tokens[i], "|")) {
            if (newJob) {
                printf("ERROR: pipe takes no input\n");
                error = 1;
                break;
            }
            if (redirectOutput || redirectInput) {
                printf("ERROR: no redirect target provided\n");
                error = 1;
                break;
            }
            addToTokens(&argvCapacity, currentJob.argc, &(currentJob.argv), NULL);
            addToPipeline(&jobPipeline, currentJob, jobCounter, &jobCapacity);
            jobCounter++;
            argvCapacity = TOKENS_SIZE_INITIAL;
            // Ignore any processes after the second one - we're only handling 2 processes
            if (jobCounter >= 2) {
                clipToTwo = 1;
                break;
            }
            newJob = 1;
            currentJob = initJobOrder();
            i++;
            continue;
        }

        if (redirectInput) {
            // If file doesn't exist, command is invalid.
            struct stat statBuff;
            if (stat(tokens[i], &statBuff) == -1 || !S_ISREG(statBuff.st_mode)) {
                printf("%s: no such file\n", tokens[i]);
                error = 1;
                break;
            }
            currentJob.inputRedirect = copyToken(tokens[i]);
            redirectInput = 0;
            i++;
            continue;
        }

        if (redirectOutput) {
            currentJob.outputRedirect = copyToken(tokens[i]);
            redirectOutput = 0;
            i++;
            continue;
        }

        if (newJob) {
            char* execPath;
            if (getExecutablePath(tokens[i], &execPath)) {
                printf("%s: command not found\n", tokens[i]);
                error = 1;
                break;
            }
            currentJob.argv = malloc(argvCapacity*sizeof(char*));
            if (!(currentJob.argv)) {
                OUT_OF_MEMORY();
            }
            addToTokens(&argvCapacity, currentJob.argc, &(currentJob.argv), copyToken(tokens[i]));
            currentJob.argc++;
            currentJob.execPath = execPath;
            newJob = 0;
            i++;
            continue;
        }

        //If wildcard, handle wildcard expansion
        if (isWildcard(tokens[i])) {
            expandWildcard(&currentJob, &argvCapacity, tokens[i]);
            i++;
            continue;
        }

        //ADD ARGUMENT TO ARGV
        addToTokens(&argvCapacity, currentJob.argc, &(currentJob.argv), copyToken(tokens[i]));
        currentJob.argc++;
        i++;
    }

    if (!error) {
        if (newJob) {
            printf("ERROR: pipe has no specified output target\n");
            destroyJobOrder(currentJob);
        }
        else if (redirectInput || redirectOutput) {
            printf("ERROR: no redirect target provided\n");
            destroyJobOrder(currentJob);
        }
        else {
            if (!clipToTwo) {
                addToTokens(&argvCapacity, currentJob.argc, &(currentJob.argv), NULL);
                addToPipeline(&jobPipeline, currentJob, jobCounter, &jobCapacity);
                jobCounter++;
            }

            // for (int i = 0; i < jobCounter; i++) {
            //     printf("Job %d:\n", i);
            //     printJobOrder(jobPipeline[i]);
            //     printf("\n");
            // }

            executeJobs(jobPipeline, jobCounter);
        }
    }
    else destroyJobOrder(currentJob);

    for (int i = 0; i < jobCounter; i++) destroyJobOrder(jobPipeline[i]);
    free(jobPipeline);
}

char* getNewToken (char* currentToken, int currentTokenLength) {
    char* newToken = malloc(currentTokenLength+1);
    if (!newToken) {
        OUT_OF_MEMORY();
    }
    memcpy(newToken, currentToken, currentTokenLength);
    newToken[currentTokenLength] = 0;
    return newToken;
}

void getTokens (int fd) {
    char buffer[BUFFER_SIZE];

    int tokenCount = 0;
    int tokenCapacity = TOKENS_SIZE_INITIAL;
    char** tokens = malloc(tokenCapacity*sizeof(char*));
    if (!tokens) {
        OUT_OF_MEMORY();
    }

    int currentTokenCapacity = INITIAL_TOKEN_CAPACITY;
    char* currentToken = malloc(currentTokenCapacity+1);
    if (!currentToken) {
        OUT_OF_MEMORY();
    }

    int currentTokenLength = 0;

    int bytesRead;
    while ((bytesRead = read(fd, buffer, BUFFER_SIZE)) > 0) {
        for (int i = 0; i < bytesRead; i++) {
            if (buffer[i] == '\n') {
                if (currentTokenLength) {
                    addToTokens(&tokenCapacity, tokenCount, &tokens, getNewToken(currentToken, currentTokenLength));
                    tokenCount++;
                    currentTokenLength = 0;
                }
                if (tokenCount) {
                    addToTokens(&tokenCapacity, tokenCount, &tokens, NULL);
                    
                    parseTokens(tokens);
                    for (int i = 0; i < tokenCount; i++) free(tokens[i]);
                    tokenCount = 0;
                }
                if (fd == STDIN_FILENO) PROMPT();
                continue;
            }

            if (buffer[i] == ' ') {
                if (currentTokenLength) {
                    addToTokens(&tokenCapacity, tokenCount, &tokens, getNewToken(currentToken, currentTokenLength));
                    tokenCount++;
                    currentTokenLength = 0;
                }
                continue;
            }

            //If current token is large, resize token buffer
            if (currentTokenLength == currentTokenCapacity) {
                currentTokenCapacity *= 2;
                currentToken = realloc(currentToken, currentTokenCapacity+1);
                if (!currentToken) {
                    OUT_OF_MEMORY();
                }
            }
            currentToken[currentTokenLength] = buffer[i];
            currentTokenLength++;
        }
    }

    free (currentToken);

    for (int i = 0; i < tokenCount; i++) free(tokens[i]);
    free(tokens);

    if (bytesRead == -1) PERROR();
}

void batchMode (char* path) {
    struct stat statBuff;

    if (stat(path, &statBuff) == -1) {
        perror(path);
        exit(EXIT_FAILURE);
    }

    if (S_ISREG(statBuff.st_mode)) {
        int fd = open(path, O_RDONLY);
        if (fd == -1) {
            PERROR();
            return;
        }
        getTokens(fd);
        close(fd);
        return;
    }
    // path exists, but doesn't point to a file
    println("ERROR: No such file\n");
    exit(EXIT_FAILURE);
}

int main (int argc, char** argv) {
    if (argc > 2) {
        println("ERROR: Too many arguments\n");
        return EXIT_FAILURE;
    }
    if (argc == 1) {
        PROMPT();
        getTokens(STDIN_FILENO);
        return EXIT_SUCCESS;
    }
    
    // If there is a single argument, run batchMode
    batchMode(argv[1]);

   return EXIT_SUCCESS;
}
