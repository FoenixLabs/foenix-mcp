/* g1tests.h - frame for the Gen1 board tests in the MCP (2026-11-06, MCP 2.0.22; tools/make_g1tests.py). */
#ifndef __G1TESTS_H
#define __G1TESTS_H

typedef unsigned long u32;
typedef unsigned short u16;
typedef unsigned char u8;

extern u32 g1t_r[64];           /* the host programs' results block ($0038_0200) */
extern u32 g1t_d[128];          /* their diagnostics block ($0038_0300) */
extern long g1t_end;            /* deadline for the endless test loops (timers_jiffies) */
extern long timers_jiffies(void);

int g1t_dac_main(void);
int g1t_gdma_main(void);
int g1t_blit_main(void);

#endif
