/*
 * esh-print.c
 * A set of print routines to manage esh objects.
 *
 * Developed by Dong Gyu Lee
 * Virginia Tech.
 *
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
#include "esh-sys-utils.h"
#include "esh-utils-jobs.h"

#define DEBUG 0 
struct
esh_pipeline * find_job(int jid_look) {
    struct list_elem * e;
    for(e = list_begin(&jobs_list); e != list_end(&jobs_list); e = list_next(e)) {
        
        struct esh_pipeline * current_job = list_entry(e, struct esh_pipeline, elem);
        if (jid_look == current_job -> jid) {
            return current_job;
        }
    }
    
    return NULL;
}


static void 
print_cmd(struct esh_command * cmd) {
    char **p = cmd -> argv;
    while (*p) {
        printf(" %s", *p++);
    }
}

void esh_command_jobs(struct esh_command_line * cmdline) {   
    struct list_elem * e;
    for (e = list_begin (&jobs_list); e != list_end (&jobs_list); e = list_next (e)) {
        struct esh_pipeline * job = list_entry(e, struct esh_pipeline, elem);        
        if (job != NULL) {
            // Use this instead of list_remove when you want to replicate
            // Exactly like how shell is behaving
            // When job status is DONE || TERMINATED, remove from jobs list after displaying "Done"
            if (job -> status == DONE || job -> status == TERMINATED) {
                list_remove(e);
                break;
            }
            struct list_elem * e_job;
            // The most recent job is denoted as [job_id]+
            // The second most recent job is denoted as [job_id]- 
            // Rest jobs are denoted without any +/- symbols [job_id]
            if (list_next(e) == list_end (&jobs_list)) {
                printf("[%d]+\t%s\t\t\t", job -> jid, print_job_status(job -> status));
            }
            else if (list_next(list_next(e)) == list_end(&jobs_list)) {
                printf("[%d]-\t%s\t\t\t", job -> jid, print_job_status(job -> status));
            }
            else {
                printf("[%d]\t%s\t\t\t", job -> jid, print_job_status(job -> status));
            }
            
            for (e_job = list_begin(&job -> commands); e_job != list_end (&job->commands); e_job = list_next (e_job)) {
                if (e_job == list_begin(&job -> commands)) {
                    printf("(");
                }
                struct esh_command * cmd = list_entry(e_job, struct esh_command, elem);
                
                // Prints the name of the job
                print_cmd(cmd);
                
                if (list_next (e_job) != list_end (&job -> commands)) {
                    printf(" | ");
                }
            }
            printf(" )\n");
        }
        else {
            #ifdef DEBUG_JOBS
                printf("No Jobs running\n");
            #endif
        }
    }
    // remove jobs command from pipeline
    list_pop_front(&cmdline -> pipes);      
}

void 
print_job(struct esh_pipeline * job) {
    struct list_elem * e_job;
    for (e_job = list_begin(&job -> commands); e_job != list_end (&job->commands); e_job = list_next (e_job)) {
        if (e_job == list_begin(&job -> commands)) {
            printf("(");
        }
        struct esh_command * cmd = list_entry(e_job, struct esh_command, elem);
        
        // Prints the name of the job
        print_cmd(cmd);
        
        if (list_next (e_job) != list_end (&job -> commands)) {
            printf(" | ");
        }
    }
    printf(" )\n");
}

const char *
print_job_status(enum job_status status) {
    static char * status_output[] = {"Running", "Foreground", "Stopped", "Needs Terminal", "Done"};
    
    switch(status) {
        case BACKGROUND:
            return status_output[0];
            break;
        case FOREGROUND:
            return status_output[1];
            break;
        case STOPPED:
            return status_output[2];
            break;
        case NEEDSTERMINAL:
            return status_output[3];
            break;
        case DONE:
            return status_output[4];
            break;
        default:
            return NULL;
            break;
    }
}


