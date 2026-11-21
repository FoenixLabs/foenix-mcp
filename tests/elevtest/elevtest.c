/*
 * elevtest.c - check the TRAP #15 elevate test (2.1.0)
 *
 * The TRAP #15 handler handles sys_proc_elevate (KFN_ELEVATE, $43) itself. Before 2.1.0 it
 * tested d1, the FIRST ARGUMENT, instead of d0, the function number:
 *
 *   1. a call whose first argument ends in $0043 was taken for an elevate: the call was never
 *      made and the program got the function number back. Here: KFN_TIME_JIFFIES with a first
 *      argument of $43 (the call itself has no arguments). It must give the jiffy count.
 *   2. a real sys_proc_elevate never worked: the program went on with the supervisor stack and
 *      crashed. Since 2.1.0 the MCP refuses it with ERR_NOT_SUPPORTED (-37) and the program
 *      simply goes on, in user mode.
 *
 * Start it from the MCP.
 */

#include <stdio.h>
#include <stdint.h>
#include <mcp/syscalls.h>

#define KFN_ELEVATE         0x43
#define KFN_TIME_JIFFIES    0x50

/*
 * call - make a system call with the first argument given
 *
 * Input:  function = the KFN number, arg1 = its first argument
 * Output: the MCP's result
 */
static int32_t call( int32_t function, int32_t arg1 )
{
	t_syscall_params params = {.arg1 = arg1};
	return syscall( function, &params );
}


int main( int argc, char *argv[] )
{
	int32_t a, b;

	(void)argc; (void)argv;

	/* 1. first argument $43 on a call that is not an elevate */
	a = call( KFN_TIME_JIFFIES, 0 );
	b = call( KFN_TIME_JIFFIES, 0x43 );
	printf( "\nelevtest\n  jiffies with first argument 0: %ld, with $43: %ld -> %s\n", (long)a, (long)b,
		(b >= a && b - a < 60) ? "OK" : "WRONG (the call was taken for an elevate)" );

	/* 2. a real elevate: refused, and the program goes on */
	a = call( KFN_ELEVATE, 0 );
	printf( "  sys_proc_elevate: %ld -> %s\n", (long)a,
		(a == -37) ? "refused (ERR_NOT_SUPPORTED): OK" : "WRONG" );
	printf( "  still running: OK\n" );
	return 0;
}
