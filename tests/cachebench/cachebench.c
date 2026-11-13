/*
 * cachebench.c - measure how fast the CPU runs under the MCP
 *
 * Made for MCP 2.0.29, which can be built with the 68040 / 68060 caches on
 * (CACHE=1). Run this program once on an MCP with the caches off and once on
 * one with the caches on, and compare the times.
 *
 * Five tests, each timed with the MCP's jiffy clock (1/60 s):
 *   1. loop:   arithmetic in CPU registers only, no memory data at all.
 *              Shows the instruction cache alone.
 *   2. read:   reading the same 4 KB over and over (no writes).
 *              Shows the data cache (the 4 KB fits in it).
 *   3. sieve:  a sieve of Eratosthenes, many times over. Mostly byte writes,
 *              which go to memory with or without cache (write-through).
 *   4. memory: copying a 64 KB block back and forth with memcpy.
 *              Half reads, half writes, much more than fits in the cache.
 *   5. screen: printing 200 lines through the MCP console (MCP code in flash,
 *              text memory is a register block, so never cached)
 *
 * Start it from the MCP (it needs the MCP for printf and the clock).
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <mcp/syscalls.h>

#define LOOP_COUNT   2000000L /* rounds of the register loop in test 1 */
#define READ_LONGS   1024     /* 4 KB of longs read in test 2 */
#define READ_RUNS    500      /* times the 4 KB is read (2 MB in total) */
#define SIEVE_SIZE   8192     /* numbers checked per sieve run */
#define SIEVE_RUNS   50       /* sieve runs in test 3 */
#define COPY_SIZE    65536    /* bytes per copy in test 4 */
#define COPY_RUNS    64       /* copies in test 4 (8 MB moved in total) */
#define PRINT_LINES  200      /* lines printed in test 5 */

static unsigned long read_buf[READ_LONGS];
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
 * start_clock - wait for the start of a new jiffy, so a test starts exactly
 *
 * Input:  none
 * Output: the jiffy count at the start
 */
static long start_clock( void )
{
	long t = now();
	while ( now() == t )
		;
	return now();
}


/*
 * reg_loop - arithmetic that stays in CPU registers
 *
 * Input:  n = number of rounds
 * Output: a value that depends on every round, so the compiler cannot
 *         leave the loop out
 */
static unsigned long reg_loop( unsigned long n )
{
	unsigned long x = 1, i;

	for ( i = 0; i < n; i++ )
		x = ( x << 1 ) ^ ( x >> 3 ) ^ i;
	return x;
}


/*
 * read_loop - add up the same 4 KB again and again
 *
 * Input:  none
 * Output: the sum (so the reads cannot be left out)
 *
 * Uses a pointer, not an index, to stay clear of the Calypsi index bug.
 */
static unsigned long read_loop( void )
{
	unsigned long sum = 0;
	const unsigned long *p;
	const unsigned long *end = read_buf + READ_LONGS;
	int r;

	for ( r = 0; r < READ_RUNS; r++ )
		for ( p = read_buf; p < end; p++ )
			sum += *p;
	return sum;
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
 * main - run the five tests and print the times
 *
 * Input:  none
 * Output: 0
 */
int main( void )
{
	long t0, t_loop, t_read, t_sieve, t_mem, t_scr;
	unsigned long x_loop, x_read;
	unsigned long *p;
	int  i, primes = 0;
	char line[80];

	printf( "cachebench 2: CPU, memory and screen speed under the MCP\n" );
	printf( "(compare the times on an MCP with the caches off and one with them on)\n\n" );

	for ( p = read_buf; p < read_buf + READ_LONGS; p++ )
		*p = (unsigned long)p;

	/* 1. register loop */
	t0 = start_clock();
	x_loop = reg_loop( LOOP_COUNT );
	t_loop = now() - t0;

	/* 2. reads */
	t0 = start_clock();
	x_read = read_loop();
	t_read = now() - t0;

	/* 3. sieve */
	t0 = start_clock();
	for ( i = 0; i < SIEVE_RUNS; i++ )
		primes = sieve();
	t_sieve = now() - t0;

	/* 4. memory */
	t0 = start_clock();
	for ( i = 0; i < COPY_RUNS; i++ )
	{
		memcpy( copy_b, copy_a, COPY_SIZE );
		memcpy( copy_a, copy_b, COPY_SIZE );
	}
	t_mem = now() - t0;

	/* 5. screen */
	t0 = start_clock();
	for ( i = 1; i <= PRINT_LINES; i++ )
		printf( "cachebench screen test, line %d of %d\n", i, PRINT_LINES );
	fflush( stdout );
	t_scr = now() - t0;

	printf( "\nResults (1 jiffy = 17 ms, so short times are rough):\n" );
	sprintf( line, "%ld rounds in registers (check %08lX)", LOOP_COUNT, x_loop );
	show( "loop", t_loop, line );
	sprintf( line, "4 KB read %d times (check %08lX)", READ_RUNS, x_read );
	show( "read", t_read, line );
	sprintf( line, "%d sieve runs, %d primes each", SIEVE_RUNS, primes );
	show( "sieve", t_sieve, line );
	sprintf( line, "%d KB copied", (int)(2L * COPY_RUNS * COPY_SIZE / 1024) );
	show( "memory", t_mem, line );
	sprintf( line, "%d lines printed", PRINT_LINES );
	show( "screen", t_scr, line );
	printf( "code at $%08lX, data at $%08lX\n", (unsigned long)&reg_loop, (unsigned long)read_buf );
	return 0;
}
