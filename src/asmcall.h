/**
 * @file asmcall.h
 *
 * ASMCALL: mark a function that is written in assembly and called from C, or written in C and
 * called from assembly (the start-up code, the TRAP #15 handler).
 *
 * The assembly takes its parameters from the stack. VBCC always passes them that way. Calypsi
 * normally passes the first ones in D0, D1, A0 and A1; its simple_call attribute makes it push
 * all of them on the stack as well (D0, D1, A0 and A1 may still be changed by the call, and the
 * result comes back in D0). With any other compiler ASMCALL is empty.
 */

#ifndef __ASMCALL_H
#define __ASMCALL_H

#if defined(__CALYPSI_TARGET_68000__)
#define ASMCALL __attribute__((simple_call))
#else
#define ASMCALL
#endif

#endif
