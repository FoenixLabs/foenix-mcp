/*
 * cachebench.c - measure how fast the CPU runs under the MCP
 *
 * Made for MCP 2.0.29, which can be built with the 68040 / 68060 caches on
 * (CACHE=1). Run this program once on an MCP with the caches off and once on
 * one with the caches on, and compare the times.
 *
 * Three tests, each timed with the MCP's jiffy clock (1/60 s):
 *   1. CPU:    a sieve of Eratosthenes, many times over (code + data in SRAM)
 *   2. memory: copying a 64 KB block back and forth (SRAM, with memcpy)
 *   3. screen: printing 200 lines through the MCP console (MCP code in flash,
 *              text memory is a register block, so never cached)
 *
 * Start it from the MCP (it needs the MCP for printf and the clock).
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <mcp/syscalls.h>

#define SIEVE_SIZE   8192    /* numbers checked per sieve run */
#define SIEVE_RUNS   50      /* sieve runs in test 1 */
#define COPY_SIZE    65536   /* bytes per copy in test 2 */
#define COPY_RUNS    64      /* copies in test 2 (4 MB in total) */
#define PRINT_LINES  200     /* lines printed in test 3 */

static char sieve_flags[SIEVE_SIZE];
static char copy_a[COPY_SIZE];
static char copy_b[COPY_SIZE];


/*
 * now - read the MCP clock
 *
 * Input:  none
 * Output: jiffies (1/60 s) since the MCP started
 */
static long now( void )
{
	return sys_time_jiffies();
}


/*
 * sieve - count the primes below SIEVE_SIZE (the classic Byte benchmark)
 *
 * Input:  none
 * Output: the number of primes found (to check that the work was really done)
 */
static int sieve( void )
{
	int i, k, count = 0;

	for ( i = 0; i < SIEVE_SIZE; i++ )
		sieve_flags[i] = 1;

	for ( i = 2; i < SIEVE_SIZE; i++ )
	{
		if ( sieve_flags[i] )
		{
			for ( k = i + i; k < SIEVE_SIZE; k += i )
				sieve_flags[k] = 0;
			count++;
		}
	}
	return count;
}


/*
 * show - print one result line
 *
 * Input:  name    = test name
 *         jiffies = the measured time in 1/60 s
 *         extra   = what was done, for the line
 * Output: none
 */
static void show( const char *name, long jiffies, const char *extra )
{
	/* jiffies * 1000 / 60 = milliseconds, without floating point */
	printf( "%-8s %6ld ms   %s\n", name, jiffies * 1000L / 60L, extra );
}


/*
 * main - run the three tests and print the times
 *
 * Input:  none
 * Output: 0
 */
int main( void )
{
	long t0, t_cpu, t_mem, t_scr;
	int  i, primes = 0;
	char line[64];

	printf( "cachebench: CPU, memory and screen speed under the MCP\n" );
	printf( "(compare the times on an MCP with the caches off and one with them on)\n\n" );

	/* 1. CPU: wait for a fresh jiffy first, so the start is exact */
	t0 = now();
	while ( now() == t0 )
		;
	t0 = now();
	for ( i = 0; i < SIEVE_RUNS; i++ )
		primes = sieve();
	t_cpu = now() - t0;

	/* 2. memory */
	t0 = now();
	for ( i = 0; i < COPY_RUNS; i++ )
	{
		memcpy( copy_b, copy_a, COPY_SIZE );
		memcpy( copy_a, copy_b, COPY_SIZE );
	}
	t_mem = now() - t0;

	/* 3. screen */
	t0 = now();
	for ( i = 1; i <= PRINT_LINES; i++ )
		printf( "cachebench screen test, line %d of %d\n", i, PRINT_LINES );
	fflush( stdout );
	t_scr = now() - t0;

	printf( "\nResults (1 jiffy = 17 ms, so short times are rough):\n" );
	sprintf( line, "%d sieve runs, %d primes each", SIEVE_RUNS, primes );
	show( "CPU", t_cpu, line );
	sprintf( line, "%d KB copied", (int)(2L * COPY_RUNS * COPY_SIZE / 1024) );
	show( "memory", t_mem, line );
	sprintf( line, "%d lines printed", PRINT_LINES );
	show( "screen", t_scr, line );
	return 0;
}
