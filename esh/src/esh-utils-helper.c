/*
 * esh-utils.c
 * A set of utility routines to manage esh objects.
 *
 * Developed by Godmar Back for CS 3214 Fall 2009
 * Virginia Tech.
 *
 * credit: Definitions of functions and implementations are excerpt from https://www.gnu.org/
 */
#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "esh.h"
#include "esh-utils-helper.h"
#include "esh-sys-utils.h"
#include "esh-utils-jobs.h"


#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"
#endif
static const char rcsid [] = "$Id: esh-utils.c,v 1.5 2011/03/29 15:46:28 cs3214 Exp $";
#ifdef __clang__
#pragma clang diagnostic pop
#endif

//#define DEBUG 0
//#define DEBUG_JOBS
#define DEBUG_PIPE

/**
 * Assign ownership of the terminal to process group
 * pgid, restoring its terminal state if provided.
 *
 * Before printing a new prompt, the shell should
 * invoke this function with its own process group
 * id (obtained on startup via getpgrp()) and a
 * same terminal state (obtained on startup via
 * esh_sys_tty_init()).
 */
static void
give_terminal_to(pid_t pgid, struct termios *pg_tty_state)
{
    #ifdef DEBUG
        printf("\nIn give_terminal_to");
    #endif
    
    esh_signal_block(SIGTTOU);
    int rc = tcsetpgrp(esh_sys_tty_getfd(), pgid);
    if (rc == -1)
        esh_sys_fatal_error("tcsetpgrp: ");

    if (pg_tty_state)
        esh_sys_tty_restore(pg_tty_state);
    esh_signal_unblock(SIGTTOU);
    
    #ifdef DEBUG
        printf("\nDone give_terminal_to\n\n");
    #endif
}

/* 
 *
 */
static void
child_status_change(pid_t child_pid, int status) {
    #ifdef DEBUG
        printf("In child_status_change\n");
    #endif 
    
    if (child_pid < 0) {
        esh_sys_fatal_error("Error: Child PID not yet assigned");
    }
    
    struct list_elem * list_elem_job_list;
    for (list_elem_job_list = list_begin(&jobs_list); list_elem_job_list != list_end(&jobs_list); list_elem_job_list = list_next(list_elem_job_list)) {
        
        struct esh_pipeline * pipeline = list_entry(list_elem_job_list, struct esh_pipeline, elem);
        struct list_elem * list_elem_commands;
        for (list_elem_commands = list_begin(&pipeline -> commands); list_elem_commands != list_end(&pipeline -> commands); list_elem_commands = list_next(list_elem_commands)) {
            
            struct esh_command * command = list_entry(list_elem_commands, struct esh_command, elem);
            if (command -> pid == child_pid) {
                 // Stopped by Ctrl + Z
                if (WIFSTOPPED(status)) {
                    pipeline -> status = STOPPED;
                    esh_sys_tty_save(&pipeline -> saved_tty_state);
                    printf("[%d]\tStopped\t\t\t", pipeline -> jid);
                    print_job(pipeline);
                    //give_terminal_to(getpgrp(), terminal);
                }
                else if (WIFCONTINUED(status)) {
                    list_remove(list_elem_commands);
                }
                // KILLED by kill command [TERMINATED]
                else if (WTERMSIG(status)) {
                    #ifdef DEBUG_SIGNAL
                        printf("Signal: Processed is interrupted and is terminated\n");
                    #endif
                    pipeline -> status = TERMINATED;
                    list_remove(list_elem_commands);
                    //give_terminal_to(getpgrp(), terminal);
                    
                    // Use this instead of list_remove when you want to replicate
                    // Exactly like how shell is behaving
                    // list_remove(list_elem_commands);
                    // if (!pipeline -> bg_job) {
                        // list_remove(list_elem_commands);
                    // }
                }
                // Exited normally [DONE]
                else if (WIFEXITED(status)) {
                    #ifdef DEBUG_SIGNAL
                        printf("Signal: Process is terminated normally\n");
                    #endif
                    pipeline -> status = DONE;
                    list_remove(list_elem_commands);
                    //give_terminal_to(getpgrp(), terminal);
                    
                    // Use this instead of list_remove when you want to replicate
                    // Exactly like how shell is behaving
                    // if (!pipeline -> bg_job) {
                        // list_remove(list_elem_commands);
                    // }
                }                
            }
            
            if (list_empty(&pipeline -> commands)) {
                list_remove(list_elem_job_list);
            }
        }
    }
    #ifdef DEBUG
        printf("Done child_status_change\n");
    #endif 
 }
 
 /*
 * SIGCHLD handler.
 * Call waitpid() to learn about any child processes that
 * have exited or changed status (been stopped, needed the
 * terminal, etc.)
 * Just record the information by updating the job list
 * data structures.  Since the call may be spurious (e.g.
 * an already pending SIGCHLD is delivered even though
 * a foreground process was already reaped), ignore when
 * waitpid returns -1.
 * Use a loop with WNOHANG since only a single SIGCHLD 
 * signal may be delivered for multiple children that have 
 * exited.
 */
static void
sigchld_handler(int sig, siginfo_t *info, void *_ctxt)
{
    #ifdef DEBUG
        printf("In sigchld_handler\n");
    #endif
    pid_t child;
    int status;
    assert(sig == SIGCHLD);
    while ((child = waitpid(-1, &status, WUNTRACED|WNOHANG)) > 0) {
        child_status_change(child, status);
    }
    #ifdef DEBUG
        printf("Done sigchld_handler\n");
    #endif
}
 
/* 
 * Wait for all processes in this pipeline to complete, or for
 * the pipeline's process group to no longer be the foreground 
 * process group. 
 * You should call this function from a) where you wait for
 * jobs started without the &; and b) where you implement the
 * 'fg' command.
 * 
 * Implement child_status_change such that it records the 
 * information obtained from waitpid() for pid 'child.'
 * If a child has exited or terminated (but not stopped!)
 * it should be removed from the list of commands of its
 * pipeline data structure so that an empty list is obtained
 * if all processes that are part of a pipeline have 
 * terminated.  If you use a different approach to keep
 * track of commands, adjust the code accordingly.
 */
static void 
wait_for_job(struct esh_pipeline * pipeline)
{
    #ifdef DEBUG
        printf("\nIn wait_for_job\n");
    #endif 
    
    assert(esh_signal_is_blocked(SIGCHLD));

    while (pipeline -> status == FOREGROUND && !list_empty(&pipeline->commands)) {
        int status;

        pid_t child_pid = waitpid(-1, &status, WUNTRACED);
        if (child_pid != -1) {
            //give_terminal_to(getpgrp(), terminal);
            child_status_change(child_pid, status);
        }
    }
    #ifdef DEBUG
        printf("\nDone wait_for_job\n\n");
    #endif
}

// static void esh_command_stop(struct esh_command_line * cmdline) {
    // if (kill(-
// }

static bool
is_esh_command_built_in(struct esh_command * esh_cmd, struct esh_pipeline * pipeline, struct esh_command_line *cmdline) {//, struct list * p_jobs_list, int * p_job_id) {
    char * command = esh_cmd -> argv[0];
    
    char * built_in[] = {"jobs", "fg", "bg", "kill", "stop", "exit", NULL};
    int i = 0;
    char * cmd = NULL;
    bool is_built_in = true;        // Initialize is_built_in                                    
    
    while (built_in[i]) {
        if (strcmp(built_in[i], command) == 0) {
            cmd = built_in[i];
            break;
        }
        i++;
    }
    
    if (cmd == NULL) {
        return false;
    }
    else if (strcmp(cmd, "jobs") == 0) {
        esh_command_jobs(cmdline);
    }
    else if (strcmp(cmd, "exit") == 0) {
        #ifdef DEBUG
            printf("Exiting\n");
        #endif
        list_pop_front(&cmdline -> pipes);
        exit(0);
    }
    else {
        
        // Argument for the command
        char * char_jid;
        int jid;
        if ((char_jid = esh_cmd -> argv[1]) != NULL) {
            jid = atoi(char_jid);
        }
        else {
            jid = job_id;
        }
        
        assert(job_id > 0);    
        
        struct esh_pipeline * found_job = find_job(jid);
        
        if (strcmp(cmd, "fg") == 0) {
            esh_signal_block(SIGCHLD);                              // 1. Block the signal
            found_job -> status = FOREGROUND;                       // 2. Set the status = FOREGROUND
            give_terminal_to(found_job -> pgid, terminal);          // 3. Give terminal access to the found_job
            if(kill(-found_job -> pgid, SIGCONT) < 0) {             // 4. Send SIGCONT signal to continue the process
                esh_sys_fatal_error("Error ['fg']: SIGCONT");
            }
            
            print_job(found_job);
            wait_for_job(found_job);                                // 5. Wait for the job to terminate
            give_terminal_to(shell_pid, terminal);                  // 6. Terminal Back to Shell
            
            esh_signal_unblock(SIGCHLD);                            // 7. Unblock signal
        }
        else if (strcmp(cmd, "bg") == 0) {
            found_job -> status = BACKGROUND;                       // Similar to fg but without giving terminal access and waiting for job                          
            if (kill(-found_job -> pgid, SIGCONT) < 0) {            // Send SIGCONT signal to continue the process from STOPPED
                esh_sys_fatal_error("Error ['bg']: SIGCONT");
            }
        }
        else if (strcmp(cmd, "kill") == 0) {                        //
            if (kill(-found_job -> pgid, SIGKILL) < 0) {            //
                esh_sys_fatal_error("Error ['kill']: SIGKILL");     //
            }
        }
        else if (strcmp(cmd, "stop") == 0) {
            if (kill(-found_job -> pgid, SIGSTOP) < 0) {
                esh_sys_fatal_error("Error ['stop']: SIGSTOP");
            }
        }
        list_pop_front(&cmdline -> pipes);
    }
    
    return is_built_in;
}

/* --------- CMD --------- */
/* Print esh_command structure to stdout */
// i.e. : 1.    Command: ls
// In the Child Process
// 
void
esh_command_helper(struct esh_command * cmd, struct esh_pipeline * pipeline)
{
    // --------- Setting PID and PGID ------------- //
    pid_t child_pid = getpid();           // Child pid
    cmd -> pid = child_pid;               // Each process PID = getpid();
    
    if (pipeline -> pgid == -1) {
        pipeline -> pgid = child_pid;
    }
    
    if (setpgid(child_pid, pipeline -> pgid) < 0) {
        esh_sys_fatal_error("Error [Parent]: Cannot set pgid\n");
    }        
    
    // --------- Foreground and Background -------- //
    if (pipeline -> bg_job) {        
        pipeline -> status = BACKGROUND;
    }
    else {        
        pipeline -> status = FOREGROUND;
        give_terminal_to(pipeline -> pgid, terminal);
    }
    
    
    // ---------- Input and Output ----------- //
    //  O_RDONLY:   Read-Only
    //  O_WRONLY:   Write-Only
    //  O_RDWR:     Read/Write
    //  O_APPEND:   Append mode
    //  O_CREAT:    If file does not exits, it will be created
    //  O_TRUNC:    If the file already exists and is a regular file and the
    //              access mode allows writing (i.e., is O_RDWR or O_WRONLY) it
    //              will be truncated to length 0.
    
    if (cmd -> iored_output) {
        // Symbol (>)
       printf("  stdout %ss to %s\n", 
       cmd -> append_to_output ? "append" : "write",
       cmd -> iored_output);
       
       int out;
       
       // credit: http://www.cs.loyola.edu/~jglenn/702/S2005/Examples/dup2.html
       if (cmd -> append_to_output) {
           out = open(cmd -> iored_output, 
                      O_WRONLY | O_APPEND | O_CREAT, 
                      0666);
       }
       else {
           out = open(cmd -> iored_output, 
                      O_WRONLY | O_TRUNC | O_CREAT, 
                      0666);
       }
       
       if (dup2(out, STDOUT_FILENO) < 0) {                          // replace standard output with output file
            esh_sys_fatal_error("Error: dup2(out, STDOUT_FILENO)");
       }
       close(out);
    }

    // credit: http://www.cs.loyola.edu/~jglenn/702/S2005/Examples/dup2.html
    if (cmd -> iored_input) {
        // Symbol (< or <<)
        printf("  stdin reads from %s\n", cmd->iored_input);
        
        
        int in = open(cmd -> iored_input, O_RDONLY);
        if (dup2(in, STDIN_FILENO) < 0) {                           // replace standard input with input file
            esh_sys_fatal_error("Error: dup2(in, STDIN_FILENO)");
        }
        close(in);
    }
    
    // ------------- Execute Command ------------ //
    if (execvp(cmd -> argv[0], cmd -> argv) < 0) {
        esh_sys_fatal_error("execvp error\n");
    }
}

/* --------- PIPELINE --------- */ 
/* Print esh_pipeline structure to stdout */
void
esh_pipeline_helper(struct esh_pipeline * pipeline, struct esh_command_line * cmdline)
{    
    /* -------- PLUG_IN ---------- */
    
    esh_signal_sethandler(SIGCHLD, sigchld_handler);
    pipeline -> is_piped = (list_size(&pipeline -> commands) > 1) ? true : false;
    
    struct list_elem * e = list_begin (&pipeline -> commands);
    struct esh_command * esh_cmd = list_entry(e, struct esh_command, elem);

    // Iterates through the command separated by "|"
    if (!is_esh_command_built_in(esh_cmd, pipeline, cmdline)) {
        
        int command_i = 0;
        /* ------------- JOB HANDLING --------------- */
        job_id++;
        // Handling Job_Id for pipelines
        if (list_empty(&jobs_list)) {
            job_id = 1;
        }
        
        /* ------------- PID, PGID, JID --------------- */
        // Initialize pipeline pgid
        pipeline -> jid = job_id;
        pipeline -> pgid = -1;
        pid_t pid;
        
        // Block Child Process Signal to prevent race running condition
        esh_signal_block(SIGCHLD);
        
        /* -------------------- Pipes ----------------- */
        // Credit: http://www.cs.loyola.edu/~jglenn/702/S2005/Examples/dup2.html
        //
        //      0                  1
        //      R ----- PIPE ----- W
        //
        // Example commands with pipes: 
        //      cat scores | grep Villanova
        // pipes[0] = [read]    cat-> grep
        // pipes[1] = [write]   cat-> grep 
        
        // Create pipe of size of commands in a pipeline
        int pipefd[list_size(&pipeline -> commands) * 2];
        int i;
        if (pipeline -> is_piped) {
            for (i = 0; i < list_size(&pipeline -> commands); i++) {
                if (pipe(pipefd + i * 2) < 0) {
                    esh_sys_fatal_error("Error: Creating pipes\n");
                }
            }
        }
        
        for (; e != list_end(&pipeline -> commands); e = list_next (e)) {
            #ifdef DEBUG_PIPE
                // printf("Command_i is : [%d]\n", command_i);
            #endif
            pid = fork();
            if (pid == 0) {
                 //  In the Child Process
                 //  ----------------- Pipes ------------------- //
                 //  Basic idea of the alogrithm is as follows:
                 //  command1   command2    command3     command 4
                 //         pipe_1     pipe_2       pipe_3
                 //         [0, 1]     [2, 3]       [4, 5]
                 //
                 //  BUTTTT This doesn't work for some reason
                if (pipeline -> is_piped) {  

                     // if not the last command:
                     // dup2(pipe[1], 1)
                     // command_i   pipefd      STDOUT_FILENO
                     // 0           1           1
                     // 1           3           1
                     // 2           5           1
                     // ...         ...         ...
                     if (list_next(e) != list_tail(&pipeline -> commands)) {
                        if(dup2(pipefd[command_i * 2 + 1], 1) < 0 ) {
                            #ifdef DEBUG_PIPE
                            printf("In if not last command, dup2(1, 1) command_i: %d\n", command_i);
                            #endif
                            esh_sys_fatal_error("Error: Error duplicating pipe to STDOUT_FILENO\n");
                        }
                     }

                     // Close all pipes after its use
                     for(i = 0; i < 2 * list_size(&pipeline -> commands); i++) { 
                        close(pipefd[i]);
                     }
                 }
                 esh_command_helper(esh_cmd, pipeline);
            }
            else if (pid < 0) {
                // Fork Failed
                esh_sys_fatal_error("Fork Error\n");
            }
            else {
                /* --------- PARENT PROCESS ----------- */
                // --------- Setting PID and PGID ------------- //
                
                // if not first command:
                     // dup2(pipe[0], 0)
                     // command_i   pipe_fd     STDIN_FILENO 
                     // 1           0           0
                     // 2           2           0
                     // 3           4           0
                     // ...         ...         ...
                     if (e != list_begin(&pipeline -> commands)) {
                         if (dup2(pipefd[(command_i - 1) * 2], 0) < 0) {
                             
                             #ifdef DEBUG_PIPE
                             
                                printf("In if not first command, dup2(0, 0) command_i: %d\n", command_i);
                             #endif
                             esh_sys_fatal_error("Error: Error duplicating pipe to STDIN_FILENO\n");
                         }
                     }
                     
                pipeline -> status = FOREGROUND;
                esh_cmd -> pid = pid;
            
                if (pipeline -> pgid == -1) {
                    pipeline -> pgid = pid;             // Child PID set to parent PID
                }
                command_i++;
            }
        }
    
        for(i = 0; i < 2 * list_size(&pipeline -> commands); i++) { 
            close(pipefd[i]);
        }
        
        if (pipeline -> bg_job) {
            pipeline -> status = BACKGROUND;
            printf("[%d] %d\n", pipeline -> jid, pipeline ->pgid);
        }
        
        struct list_elem * elem = list_pop_front(&cmdline -> pipes);
        list_push_back(&jobs_list, elem);
        
        int size = list_size(&pipeline -> commands);
        if (!pipeline -> bg_job) {
            for (i = 0; i < size; i++) {
                wait_for_job(pipeline);
            }
        }
        give_terminal_to(shell_pid, terminal);
        esh_signal_unblock(SIGCHLD);
    }
}

/* --------- CMD LINE --------- */
/* Print esh_command_line structure to stdout */
void 
esh_command_line_helper(struct esh_command_line * cmdline)
{        
    while (!list_empty(&cmdline->pipes)) {
        struct esh_pipeline * pipeline = list_entry(list_begin(&cmdline -> pipes), struct esh_pipeline, elem);
        esh_pipeline_helper(pipeline, cmdline);
    }
}