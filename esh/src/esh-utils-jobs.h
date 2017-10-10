#ifndef __ESH_UTILS_JOBS_H
#define __ESH_UTILS_JOBS_H

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

struct esh_pipeline * find_job(int jid_look);

void esh_command_jobs(struct esh_command_line * cmdline);

void print_job(struct esh_pipeline * job);

const char * print_job_status(enum job_status status);

#endif //__ESH_UTILS_JOBS_H