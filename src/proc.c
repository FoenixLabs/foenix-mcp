/*
 * Routines to support processes
 *
 * NOTE: at the moment, FoenixMCP is single tasking only, so there is only
 *       one "process" besides the kernel.
 *
 */

#include "log_level.h"
#ifndef DEFAULT_LOG_LEVEL
    #define DEFAULT_LOG_LEVEL LOG_ERROR
#endif

#include "errors.h"
#include "log.h"
#include "dev/fsys.h"
#include "sys_general.h"
#include "asmcall.h"
#include <string.h>
#include "dev/channel.h"
#include "topbar.h"

static const long k_default_stack = 0x00010000;     /* For now... we're just going to put the user stack under 0x00010000 */
static int g_proc_result;

/*
 * Assembly routine: reset the supervisor stack pointer and restart the CLI
 */
extern ASMCALL void restart_cli();

extern ASMCALL void call_user(long start, long stack, int argc, char * argv[]);

#if MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_GENX || MODEL == MODEL_FOENIX_A2560X
/*
 * 2.0.33: where a running program returns to when it ends (see call_user in startup_m68040.s).
 * 0 = nowhere: the program was not started by call_user (e.g. through the debug port), or the
 * command line was restarted since. Set and cleared by the assembly code only.
 */
void * proc_return_point = 0;

/*
 * Assembly routine: go back to the newest return point; call_user then returns to its caller
 */
extern ASMCALL void proc_return(int result);
#define PROC_CAN_RETURN 1
#else
#define PROC_CAN_RETURN 0           /* the 68000 start-up code has no return points (yet) */
#endif

/*
 * Start a user mode process
 *
 * Inputs:
 * start = the address to start execution
 * stack = the location to start the user mode stack
 * argc = the number of parameters
 * argv = the array of parameters
 */
void proc_exec(long start, long stack, int argc, char * argv[]) {
    TRACE("proc_exec");

    //INFO1(LOG_INFO, "proc_exec start: %d");
    //INFO1(LOG_INFO, "proc_exec stack: %d");

    g_proc_result = 0;
    call_user(start, stack, argc, argv);
}

/*
 * Quit the current user process
 *
 * 2.0.33: returns to whoever started the program (the command line, a line of mcp.init, or another
 * program): proc_run() then returns 0 there. Only when there is nowhere to return to is the command
 * line restarted, as all versions before did. Why: with the restart, the rest of mcp.init after a
 * program never ran, and the command line lost its state (history, clean-up after the program).
 *
 * Inputs:
 * result = the program's exit code (read it with proc_get_result)
 */
void proc_exit(int result) {
    g_proc_result = result;
#if PROC_CAN_RETURN
    if (proc_return_point != 0) {
        proc_return(result);        /* does not come back here */
    }
#endif
    restart_cli();
}

/*
 * Return the result code of the previously running user process
 */
int proc_get_result() {
    return g_proc_result;
}

/*
 * Find an executable binary matching the path, load it, and execute it
 *
 * Inputs:
 * path = the path to try to load
 * argc = the number of parameters
 * argv = the array of parameters
 *
 * Returns:
 * returns an error code on failure; 0 when the program has run and ended (2.0.33; before, it did not
 * return on success). The program's own exit code is not returned here: a program ending with, say,
 * -1 is not a failure to run it. Read that code with proc_get_result.
 */
/**
 * Make the full path of a program for the top bar: a path without a folder or drive in front is
 * relative to the current folder.
 *
 * @param path the path the program was started with
 * @param full the result, TOPBAR_PATH_MAX characters
 */
static void proc_full_path(const char * path, char * full) {
    short len;

    if ((path[0] == '/') || (strchr(path, ':') != 0)) {
        strncpy(full, path, TOPBAR_PATH_MAX - 1);
        full[TOPBAR_PATH_MAX - 1] = 0;
        return;
    }

    full[0] = 0;
    fsys_get_cwd(full, TOPBAR_PATH_MAX);
    full[TOPBAR_PATH_MAX - 1] = 0;
    len = (short)strlen(full);
    if ((len > 0) && (full[len - 1] != '/') && (len < TOPBAR_PATH_MAX - 1)) {
        full[len++] = '/';
        full[len] = 0;
    }
    strncat(full, path, TOPBAR_PATH_MAX - 1 - len);
}

short proc_run(const char * path, int argc, char * argv[]) {

    TRACE1("proc_run(\"%s\")", path);

    /* TODO: allow for commands without extensions */
    /* TODO: allow for a search PATH */
    /* TODO: allocate stack more dynamically */

    long start = 0;
    short result = fsys_load(path, 0, &start);
    if (result == 0) {
        if (start != 0) {
            /* 2.1.0: while the program runs, the top bar of the console screen shows its path.
               Afterwards the bar shows what it showed before (the folder, or the program that
               started this one) and the program's own text in the USER part is cleared. */
            short screen = chan_device(0);
            char before[TOPBAR_PATH_MAX];
            char full[TOPBAR_PATH_MAX];

            topbar_get_app(screen, before);
            proc_full_path(path, full);
            topbar_set_app(screen, full, 1);

            proc_exec(start, k_default_stack, argc, argv);

            topbar_set_user(screen, 0);
            topbar_set_app(screen, before, 0);
            return 0;
        } else {
            ERROR1("Couldn't execute file: %d", result);
            return ERR_NOT_EXECUTABLE;
        }
    } else {
        ERROR1("Couldn't load file: %d", result);
        return result;
    }
}
