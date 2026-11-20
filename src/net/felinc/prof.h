/***********************************************************************
 * PROF.H - a simple profiler: measures how much time FELINC spends in
 * its parts (reading frames, checksums, copying, sending ...).
 *
 * Only for measuring: with FELINC_PROFILE 0 (the default) all PROF_
 * macros are empty and nothing of this is compiled in.
 * It uses timer 2 of the A2560K, which counts the 25 MHz bus clock, so
 * one count is 40 ns. A measured part is put between
 *     PROF_START( name );
 *     ...
 *     PROF_STOP( PROF_SLOT, name );
 * and prof_print() shows the totals.
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#ifndef __PROF_H__
#define __PROF_H__

#include <stdint.h>

#ifndef FELINC_PROFILE
#define FELINC_PROFILE	0
#endif

/* The measured parts */
enum prof_slot
{
	PROF_POLL = 0,		/* all of felinc_poll() */
	PROF_RX_EMPTY,		/* asking the controller for a frame when none is there */
	PROF_RX_FIFO,		/* reading a frame from the controller */
	PROF_HANDLE,		/* handling a received frame (IP, TCP, the ACK sent) */
	PROF_PENDING,		/* sending queued data (felinc_poll's last part) */
	PROF_TX_FIFO,		/* writing a frame into the controller */
	PROF_CHECKSUM,		/* Internet checksums */
	PROF_COPY,			/* copying into and out of the ring buffers */
	PROF_JIFFIES,		/* asking the MCP for the time (utl_jiffies) */
	PROF_SLOTS
};

#if FELINC_PROFILE

/* Timer 2's counter (see the hardware reference, "Timers") */
#define PROF_TIMER		(*(volatile uint32_t *)0xFEC00218UL)

extern uint32_t prof_cycles[PROF_SLOTS];
extern uint32_t prof_calls[PROF_SLOTS];

#define PROF_START(name)		uint32_t prof_t_##name = PROF_TIMER
#define PROF_STOP(slot, name)	do { prof_cycles[slot] += PROF_TIMER - prof_t_##name; \
									prof_calls[slot]++; } while( 0 )

void prof_reset( void );
void prof_print( void );

#else

#define PROF_START(name)
#define PROF_STOP(slot, name)
#define prof_reset()
#define prof_print()

#endif

#endif
