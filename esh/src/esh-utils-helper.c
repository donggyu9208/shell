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
        
        struct esh_pipeline * pipe = list_entry(list_elem_job_list, struct esh_pipeline, elem);
        struct list_elem * list_elem_commands;
        for (list_elem_commands = list_begin(&pipe -> commands); list_elem_commands != list_end(&pipe -> commands); list_elem_commands = list_next(list_elem_commands)) {
            
            struct esh_command * command = list_entry(list_elem_commands, struct esh_command, elem);
            if (command -> pid == child_pid) {
                if (WIFSTOPPED(status)) {
                    #ifdef DEBUG
                        printf("Signal: Child Process is Stopped\n");
                    #endif
                    
                    printf("[%d]+   Stopped\n", pipe -> jid);
                    pipe -> status = STOPPED;
                    // Save current terminal settings. This function is used when a job is suspended.
                    //esh_sys_tty_save(&pipe -> saved_tty_state);
                    //give_terminal_to(getpgrp(), terminal);
                }
                else if (WIFSIGNALED(status)) {
                    #ifdef DEUG
                        printf("Signal: Child Processed is Terminated by a signal\n");
                        printf("Number of signals that caused child process to terminate are %d\n", WTERMSIG(status));
                    #endif
                    if (WTERMSIG(status) == 22) {
                        printf("[%d]+   Stopped\n", pipe->jid);
                        pipe -> status = STOPPED;
                        esh_sys_tty_save(&pipe -> saved_tty_state);
                        give_terminal_to(getpgrp(), terminal);
                    }
                    else {
                        list_remove(list_elem_commands);
                        give_terminal_to(getpgrp(), terminal);
                    }
                    
                }
                else if (WIFEXITED(status)) {
                    #ifdef DEUG
                        printf("Signal: Child is terminated normally\n");
                    #endif
                    list_remove(list_elem_commands);
                }
            }
            
            if (list_empty(&pipe -> commands)) {
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
            child_status_change(child_pid, status);
        }
    }
    #ifdef DEBUG
        printf("\nDone wait_for_job\n\n");
    #endif
}

bool
is_esh_command_built_in(char * command, struct esh_pipeline * pipe) {//, struct list * p_jobs_list, int * p_job_id) {
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
        for (e = list_begin (&jobs_list); e != list_end (&jobs_list); e = list_next (e)) {
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
// In the Child Process
// 
void
esh_command_helper(struct esh_command * cmd, struct esh_pipeline * pipe)
{
    #ifdef DEBUG
        printf("\nIn Child Process:\n");
	#endif
    
    // --------- Setting PID and PGID ------------- //
    pid_t child_pid = getpid();          // Child pid
    cmd -> pid = child_pid;               // Each process PID = getpid();
    
    if (pipe -> pgid == -1) {
        pipe -> pgid = child_pid;
    }
    
    if (setpgid(child_pid, pipe -> pgid) < 0) {
        esh_sys_fatal_error("Error [Parent]: Cannot set pgid\n");
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
    if (cmd -> iored_output)
        printf("  stdout %ss to %s\n", 
               cmd->append_to_output ? "append" : "write",
               cmd->iored_output);

    if (cmd->iored_input) {
        printf("  stdin reads from %s\n", cmd->iored_input);
    }
    
    // ------------- Execute Command ------------ //
    char **p = cmd->argv;
    #ifdef DEBUG
        printf("\tCommand:");
            while (*p)
                printf(" %s", *p++);

        printf("\n");
	#endif
    
    if (execvp(cmd -> argv[0], cmd -> argv) < 0) {
        esh_sys_fatal_error("execvp error\n");
    }
    
    #ifdef DEBUG
        printf("Done Child Process\n\n");
    #endif
}

/* --------- PIPELINE --------- */ 
/* Print esh_pipeline structure to stdout */
void
esh_pipeline_helper(struct esh_pipeline * pipe, struct esh_command_line * cmdline)
{
    #ifdef DEBUG
        printf("\nIn esh_pipeline_helper\n");
    #endif
    
    /* -------- PLUG_IN ---------- */

    /* -------------------------- */
    
    /* -------- Execvp ---------- */
    #ifdef DEBUG
        printf("\n\tIn Not Plug-Ins\n");
    #endif
    
    esh_signal_sethandler(SIGCHLD, sigchld_handler);
    
    job_id++;
    // Handling Job_Id for pipelines
    if (list_empty(&jobs_list)) {
        job_id = 1;
    }
    
    // Initialize pipe pgid
    pipe -> jid = job_id;
    pipe -> pgid = -1;
    pid_t pid;
    
    #ifdef DEBUG
        printf("\tJob ID is %d\n", pipe -> jid);
        int process_i = 1;  
    #endif

    struct list_elem * e;
    for (e = list_begin (&pipe->commands); e != list_end (&pipe->commands); e = list_next (e)) {
        #ifdef DEBUG
            printf("\t\tIn command loop\n");
            printf("\t\tprocess %d. \n", process_i++);
        #endif
        
        struct esh_command * cmd = list_entry(e, struct esh_command, elem);
        char * command = cmd -> argv[0];
   
        // Iterates through the command separated by "|"
        if (!is_esh_command_built_in(command, pipe)) {
            // Block Child Process Signal to prevent race running condition
            esh_signal_block(SIGCHLD);
            
            pid = fork();
            if (pid == 0) {
                // In the Child Process
                esh_command_helper(cmd, pipe);
            }
            else if (pid < 0) {
                // Fork Failed
                esh_sys_fatal_error("Fork Error\n");
            }
            else {
                #ifdef DEBUG
                    printf("\n\t\tFork: In the Parent Process\n");
                    printf("\t\tpid: %d\n", getpid());
                #endif
                
                // In the Parent Process
                pipe -> status = FOREGROUND;
                cmd -> pid = pid;
                
                if (pipe -> pgid == -1) {
                    pipe -> pgid = pid;             // Child PID set to parent PID
                }
                
                // if (setpgid(pid, pipe -> pgid) < 0) {
                    // esh_sys_fatal_error("Error [Parent]: Cannot set pgid\n");
                // }
                                
                #ifdef DEBUG
                    printf("\t\tEnd of Parent Process\n");
                #endif
            }
            // Out of Parent
                
        }
        // Out of pipeline loop

        if (pipe -> bg_job) {
            pipe -> status = BACKGROUND;
            printf("[%d] %d\n", pipe -> jid, pipe ->pgid);
        }
        
        if (!list_empty(&cmdline -> pipes)) {
            struct list_elem * elem = list_pop_front(&cmdline -> pipes);
            list_push_back(&jobs_list, elem);
        }
        
        
        if (!pipe -> bg_job) {
            wait_for_job(pipe);
        }
        give_terminal_to(shell_pid, terminal);
        esh_signal_unblock(SIGCHLD);
    }
    #ifdef DEBUG
        printf("\nDone esh_pipeline_helper\n\n");
    #endif
}

/* --------- CMD LINE --------- */
/* Print esh_command_line structure to stdout */
void 
esh_command_line_helper(struct esh_command_line * cmdline)
{    
    #ifdef DEBUG
        printf("Command line\n");
    #endif
    
    //struct list_elem * e; 
    //for (e = list_begin (&cmdline -> pipes); e != list_end (&cmdline -> pipes); e = list_next (e)) {
        struct esh_pipeline * pipe = list_entry(list_begin(&cmdline -> pipes), struct esh_pipeline, elem);

        printf(" ------------- \n");
        esh_pipeline_helper(pipe, cmdline);
    //}
    printf("==========================================\n");
}
