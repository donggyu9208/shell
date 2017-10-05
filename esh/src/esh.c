/*
 * esh - the 'pluggable' shell.
 *
 * Developed by Godmar Back for CS 3214 Fall 2009
 * Virginia Tech
 * your name.
 */
#include <stdio.h>
#include <readline/readline.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "esh.h"
#include "esh-utils-helper.h"
#include "esh-sys-utils.h"

#define DEBUG 1
static void
usage(char *progname)
{
    printf("Usage: %s -h\n"
        " -h            print this help\n"
        " -p  plugindir directory from which to load plug-ins\n",
        progname);

    exit(EXIT_SUCCESS);
}

/* Build a prompt by assembling fragments from loaded plugins that 
 * implement 'make_prompt.'
 *
 * This function demonstrates how to iterate over all loaded plugins.
 */
static char *
build_prompt_from_plugins(void)
{
    char *prompt = NULL;
    struct list_elem * e = list_begin(&esh_plugin_list);

    for (; e != list_end(&esh_plugin_list); e = list_next(e)) {
        struct esh_plugin *plugin = list_entry(e, struct esh_plugin, elem);

        if (plugin->make_prompt == NULL)
            continue;

        /* append prompt fragment created by plug-in */
        char * p = plugin->make_prompt();
        if (prompt == NULL) {
            prompt = p;
        } else {
            prompt = realloc(prompt, strlen(prompt) + strlen(p) + 1);
            strcat(prompt, p);
            free(p);
        }
    }

    /* default prompt */
    if (prompt == NULL)
        prompt = strdup("esh> ");

    return prompt;
}

/* The shell object plugins use.
 * Some methods are set to defaults.
 */
struct esh_shell shell =
{
    .build_prompt = build_prompt_from_plugins,
    .readline = readline,                        /* GNU readline(3) */ 
    .parse_command_line = esh_parse_command_line /* Default parser */
    
};

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
        printf("/nIn give_terminal_to\n");
    #endif
    esh_signal_block(SIGTTOU);
    int rc = tcsetpgrp(esh_sys_tty_getfd(), pgid);
    if (rc == -1)
        esh_sys_fatal_error("tcsetpgrp: ");

    if (pg_tty_state)
        esh_sys_tty_restore(pg_tty_state);
    esh_signal_unblock(SIGTTOU);
    #ifdef DEBUG
        printf("/nDone give_terminal_to\n");
    #endif
}

int
main(int ac, char *av[])
{
    int opt;
    list_init(&esh_plugin_list);
    
    // Job List
    job_id = 0;
    list_init(&jobs_list);
    
    //struct list *p_jobs_list = &jobs_list;
    //int * p_job_id = &job_id;
    
    /* Process command-line arguments. See getopt(3) */
    while ((opt = getopt(ac, av, "hp:")) > 0) {
        switch (opt) {
        case 'h':
            usage(av[0]);
            break;

        case 'p':
            esh_plugin_load_from_directory(optarg);
            break;
        }
    }

    esh_plugin_initialize(&shell);
    setpgrp();                              // set current pid to pgid
    shell_pid = getpid();                    
    terminal = esh_sys_tty_init();
    give_terminal_to(shell_pid, terminal);
    
    /* Read/eval loop. */
    for (;;) {
        /* Do not output a prompt unless shell's stdin is a terminal */
        char * prompt = isatty(0) ? shell.build_prompt() : NULL; 	// Name of the shell i.e: esh>
        char * cmdline = shell.readline(prompt);			        // cmdline: Commands i.e: ls
                                                                    // Reads the command here
        free (prompt);
        if (cmdline == NULL)  /* User typed EOF */
            break;
    
        struct esh_command_line * cline = shell.parse_command_line(cmdline);    // cmdline parsed into cline
        
        free (cmdline);
        if (cline == NULL)                  /* Error in command line */
            continue;

        if (list_empty(&cline->pipes)) {    /* User hit enter */
            esh_command_line_free(cline);                                       
            continue;
        }
        
                                                  // parsed command line passed to print out its command
        //esh_command_line_print(cline);
        esh_command_line_helper(cline);
        esh_command_line_free(cline);
        
        
    }
    return 0;
}
