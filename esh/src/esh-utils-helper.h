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

void
esh_command_helper(struct esh_command *cmd, struct esh_pipeline *pipe);//, struct termios * terminal);

void
esh_pipeline_helper(struct esh_pipeline *pipe, struct esh_command_line * cmdline);//, struct list * p_jobs_list, int * p_job_id, pid_t shell_pid);//, struct termios * terminal);

void 
esh_command_line_helper(struct esh_command_line *cmdline);//, struct list * p_jobs_list, int * p_job_id, pid_t shell_pid); //, struct termios * terminal);

#endif //__ESH_UTILS_HELPER_H