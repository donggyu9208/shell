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

#include "esh.h"
#include "esh-utils-helper.h"
#include "esh-sys-utils.h"



#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"
#endif
static const char rcsid [] = "$Id: esh-utils.c,v 1.5 2011/03/29 15:46:28 cs3214 Exp $";
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#define DEBUG 1

bool
is_esh_command_built_in(char * command, struct esh_pipeline * pipe, struct list * p_jobs_list, int * p_job_id) {
    char * built_in[] = {"jobs", "fg", "bg", "kill", "stop", "exit", NULL};
    int i = 0;
    char * cmd = NULL;
    bool is_built_in = true;
    
    while (built_in[i]) {
        if (strcmp(built_in[i], command) == 0) {
            cmd = built_in[i];
            break;
        }
        i++;
    }
    
    if (cmd == NULL) {
        is_built_in = false;
    }
    else if (strcmp(cmd, "jobs") == 0) {
        #ifdef DEBUG
             printf("Jobs:\n");
        #endif
        
        struct list_elem * e;
        
        struct esh_pipeline * job_pipe;
        for (e = list_begin (&(*p_jobs_list)); e != list_end (&(*p_jobs_list)); e = list_next (e)) {
            job_pipe = list_entry(e, struct esh_pipeline, elem);
            
            if (job_pipe != NULL) {
                #ifdef DEBUG
                    printf("Jobs running\n");
                #endif
                printf("[%d]\n", job_pipe -> jid);
            }
            else {
                #ifdef DEBUG
                    printf("No Jobs running\n");
                #endif
            }
        }
    }
    else if (strcmp(cmd, "bg") == 0) {
        pipe -> status = BACKGROUND;
        if (kill(-pipe -> pgid, SIGCONT) < 0) {
            esh_sys_fatal_error("Error: failed to bg");
        }
    }
    else if (strcmp(cmd, "exit") == 0) {
        printf("Exiting\n");
        exit(0);
    }
    else {
        
    }
    
    return is_built_in;
}

/* --------- CMD --------- */
/* Print esh_command structure to stdout */
// i.e. : 1.    Command: ls
// for each Process
void
esh_command_helper(struct esh_command   * cmd, 
                   struct esh_pipeline  * pipe//,
                   )//struct termios       * terminal)
{
    char **p = cmd->argv;
    #ifdef DEBUG
        printf("  Command:");
        while (*p)
            printf(" %s", *p++);

        printf("\n");
	#endif
    
    // -------------------------------------------- //
    // --------- Setting PID and PGID ------------- //
    // -------------------------------------------- //
    
    pid_t current_pid = getpid();          // Child pid
    cmd -> pid = current_pid;               // Each process PID = getpid();
    
    if (pipe -> pgid == -1) {
        pipe -> pgid = current_pid;
    }
    else {
        if (setpgid(cmd->pid, pipe -> pgid) < 0) {
            esh_sys_fatal_error("Error [Parent]: Cannot set pgid\n");
        }
    }
    
    // --------- Foreground and Background -------- //
    if (pipe -> bg_job) {
        #ifdef DEBUG
            printf("Pipeline status: BACKGROUND\n");
        #endif
        pipe -> status = BACKGROUND;
    }
    else {
        #ifdef DEBUG
            printf("Pipeline status: FOREGROUND\n");
        #endif
        pipe -> status = FOREGROUND;
        give_terminal_to(pipe -> pgid, terminal);
    }
    
    // ---------- Input and Output ----------- //
    if (cmd->iored_output)
        printf("  stdout %ss to %s\n", 
               cmd->append_to_output ? "append" : "write",
               cmd->iored_output);

    if (cmd->iored_input) {
        printf("  stdin reads from %s\n", cmd->iored_input);
    }
    
    // ------------- Execute Command ------------ //
    char * command = cmd -> argv[0];
    if (execvp(command, cmd -> argv) < 0) {
        esh_sys_fatal_error("execvp error\n");
    }
}

/* --------- PIPELINE --------- */ 
/* Print esh_pipeline structure to stdout */
void
esh_pipeline_helper(struct esh_pipeline * pipe, 
                    struct list         * p_jobs_list, 
                    int                 * p_job_id)//, 
                    //struct termios      * terminal)
{
    #ifdef DEBUG
        printf("    Pipeline\n");
    #endif
    
    // Handling Job_Id for pipelines
    if (list_empty(&(*p_jobs_list))) {
        *p_job_id = 1;
        pipe -> jid = *p_job_id;
    }
    
    pipe -> jid = (*p_job_id)++;
    printf("Job ID is %d\n", pipe -> jid);
    
    
    int process_i = 1;  
    // Initialize the list of pipes
    struct list_elem * e = list_begin (&pipe->commands); 
    
    
    // Initialize pipe pgid
    pipe -> pgid = -1;
    pid_t pid;
    
    // List through the pipelines
    for (; e != list_end (&pipe->commands); e = list_next (e)) {
        int child_status;                     //
        
        printf("process %d. ", process_i++);
        
        struct esh_command * cmd = list_entry(e, struct esh_command, elem);
        char * command = cmd -> argv[0];
   
        // Iterates through the command separated by "|"
        if (!is_esh_command_built_in(command, pipe, p_jobs_list, p_job_id)) {
            // Block Child Process Signal to prevent race running condition
            esh_signal_block(SIGCHLD);
            
            pid = fork();
            if (pid == 0) {
                // In the Child Process
                esh_command_helper(cmd, pipe);//, terminal);
            }
            else if (pid < 0) {
                // If Fork Failed
                esh_sys_fatal_error("Fork Error\n");
            }
            else {
                // In the Parent Process
                cmd -> pid = pid;
                
                if (pipe -> pgid == -1) {
                    pipe -> pgid = pid;             // Child PID set to parent PID
                }
                
                if (setpgid(pid, pipe -> pgid) < 0) {
                    esh_sys_fatal_error("Error [Parent]: Cannot set pgid\n");
                }  
                     
                esh_signal_unblock(SIGCHLD);
                
                waitpid(WAIT_ANY, &child_status, WUNTRACED);    
                //waitpid(WAIT_ANY, &child_status, WUNTRACED|WNOHANG);   
                        // waitpid (pid_t pid, int *status-ptr, int options)
                        // 
                        // pid_t pid:
                        // pid == -1 (WAIT_ANY)     any child processes
                        // pid == 0  (WAIT_MYGRP)   any child processes in the same process group
                        // pid == -pgid             any child processes whose process group ID is pgid
                        //
                        // * status-ptr:
                        // Status information from child process is stored in "status-ptr" unless is a NULL-pointer
                        //
                        // int options:
                        // Its value must be bitwise or zero or WNOHANG and WUNTRACED flags
                        // WNOHANG                  parent process shouldn't wait/ Don't block waiting
                        // WUNTRACED                request status from stopped processes as well as processes that have terminated
                        // WUNTRACED|WNOHANG        To report terminated or stopped foreground jobs to the user as well as background jobs
                        //
                        // return:
                        // PID of the child process whose status is reported.
                        // If there are child processes but there are none that to signal, waitpid will block until one signals
                        // However if WNOHANG is specified, waitpid will return 0 instead of blocking.
                        //
                        // If specific PID is given to waitpid, all other child processes are ignored.
                        //
                        // Note: wait(NULL) waitpid(-1, NULL, 0);                           
            }
            // Out of Parent
                
        }
        // Out of pipeline loop
        //wait_for_job(pipe);
    }
}

/* --------- CMD LINE --------- */
/* Print esh_command_line structure to stdout */
void 
esh_command_line_helper(struct esh_command_line * cmdline, 
                        struct list             * p_jobs_list, 
                        int                     * p_job_id)//, 
                        //struct termios          * terminal)
{
    struct list_elem * e = list_begin (&cmdline->pipes); 

    printf("Command line\n");
    for (; e != list_end (&cmdline->pipes); e = list_next (e)) {
        struct esh_pipeline * pipe = list_entry(e, struct esh_pipeline, elem);

        printf(" ------------- \n");
        esh_pipeline_helper(pipe, p_jobs_list, p_job_id);//, terminal);
    }
    printf("==========================================\n");
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
// void
// wait_for_job(struct esh_pipeline *pipeline)
// {
    // assert(esh_signal_is_blocked(SIGCHLD));

    // while (pipeline->status == FOREGROUND && !list_empty(&pipeline->commands)) {
        // int status;

        // pid_t child = waitpid(-1, &status, WUNTRACED);
        // if (child != -1)
            // child_status_change(child, status);
    // }
// }

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
// static void
// sigchld_handler(int sig, siginfo_t *info, void *_ctxt)
// {
    // pid_t child;
    // int status;

    // assert(sig == SIGCHLD);

    // while ((child = waitpid(-1, &status, WUNTRACED|WNOHANG)) > 0) {
        // child_status_change(child, status);
    // }
// }

// /* 
 // *
 // */
 // static void
 // child_status_change(pid_t child_pid, int status) {
     // if (childpid < 0) {
         // esh_sys_fatal_error("Error: Child PID not yet assigned");
     // }
     
     
 // }