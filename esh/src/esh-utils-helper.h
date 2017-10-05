#ifndef __ESH_UTILS_HELPER_H
#define __ESH_UTILS_HELPER_H

/*
 * helper functions for esh-utils
 */
#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <dlfcn.h>
#include <limits.h>


bool
is_esh_command_built_in(char * command, struct esh_pipeline *pipe, struct esh_command_line *cmdline);//, struct list * p_jobs_list, int * p_job_id);

void
esh_command_helper(struct esh_command *cmd, struct esh_pipeline *pipe);//, struct termios * terminal);

void
esh_pipeline_helper(struct esh_pipeline *pipe, struct esh_command_line * cmdline);//, struct list * p_jobs_list, int * p_job_id, pid_t shell_pid);//, struct termios * terminal);

void 
esh_command_line_helper(struct esh_command_line *cmdline);//, struct list * p_jobs_list, int * p_job_id, pid_t shell_pid); //, struct termios * terminal);

// static void wait_for_job(struct esh_pipeline *pipeline);

// static void
// child_status_change(pid_t child_pid, int status);
// static void sigchld_handler(int sig, siginfo_t *info, void *_ctxt);

#endif //__ESH_UTILS_HELPER_H