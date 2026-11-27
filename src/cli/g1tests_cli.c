/*
 * g1tests_cli.c - MCP 2.0.22 (2026-11-06): TEST DAC / TEST GDMA / TEST BLIT, the Gen1 K25 board tests that used to run
 * only as debug-port programs (E:\GEN1_ACTIVE_FPGA_Work\host\dactest, gdmatest, blittest; the test code is generated
 * from them by tools/make_g1tests.py). Same checks, same memory areas, same printout as the host scripts.
 *
 * The tests use SDRAM $0300_0000-$04FF_FFFF, SRAM $0010_0000-$0034_0FFF, VRAM A $0050_0000-$007F_FFFF and VRAM B
 * $00A0_0000-$00BF_FFFF. In the flash MCP these areas are free (user programs own SRAM from $1_0000 up); the RAM build
 * of the MCP sits in that SRAM, so the commands refuse to run there.
 *
 * 2.2.0: the highest SRAM address is the list area of TEST BLIT ($0034_0000, 8 entries of 64 bytes); the other areas
 * end below $0031_5000. The "$0038_0000-$003F_FFFF" and "$0038_0200" in the headers of g1t_dac.c, g1t_gdma.c and
 * g1t_blit.c describe the debug-port programs they come from: in the MCP the results are the arrays g1t_r and g1t_d
 * (the R() and D() macros), so the tests never touch the network pool at $0038_0000. What can lie in their SRAM is
 * memory taken with sys_mem_reserve (it grows down from $0038_0000): the tests refuse to run when the top of RAM is
 * below the end of their SRAM.
 */
#include <stdio.h>
#include <string.h>
#include "simpleio.h"
#include "g1tests.h"
#include "cache.h"
#include "memory.h"
#include "cli/mem_cmds.h"

u32 g1t_r[64];
u32 g1t_d[128];
long g1t_end;

static short g1t_from_flash(short ch)
{
    unsigned long here = (unsigned long)&g1t_from_flash;
    if (here < 0xFFC00000UL) {
        print(ch, "These tests use SRAM $0010_0000-$0034_0FFF, where the RAM build of the MCP lives: run them from the flash MCP.\n");
        return 0;
    }
    return 1;
}

static long g1t_arg(int argc, const char * argv[], int i, long def)
{
    long v = def;
    if (argc > i) {
        const char * s = argv[i];
        v = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    }
    return v;
}

static void g1t_run(int (*fn)(void), u32 mode, long secs)
{
    short i;
    for (i = 0; i < 64; i++) g1t_r[i] = 0;
    for (i = 0; i < 128; i++) g1t_d[i] = 0;
    g1t_r[0] = mode;
    g1t_end = timers_jiffies() + secs * 60;

    /* 2.0.29: these tests let the GDMA, the blitter and the DAC write memory that the CPU then checks.
       The CPU does not see those writes, so in a CACHE=1 build its data cache could hold old copies.
       The tests were written for an MCP without caches: run them with the data cache off.
       2.0.30: cache_resume sets the caches up anew, it does not depend on a saved value. */
    cache_data_off();
    fn();
    cache_resume();
}

/* ---- TEST DAC [0|2|3] [seconds]: 0 = the 19 functional checks, 2 = test tone, 3 = scales + chord (listening) ---- */
static const char * const dac_names[19] = {
    "register read-back (wrong values)", "AUD3 constant from SDRAM", "AUD3 constant from SRAM",
    "AUD3 constant from VRAM A", "AUD3 constant from VRAM B", "AUD2 constant from SDRAM", "AUD2 constant from SRAM",
    "AUD2 constant from VRAM A", "AUD2 constant from VRAM B", "PCM8 stereo L / R", "two voices add up",
    "half volume", "STOP: ramp down, then silence", "one-shot end: V_POS << 16 | EVENT", "interrupt: GAVIN bit 47",
    "rate AUD3 / AUD2 x 10000", "address outside the banks: STARVE, silence", "STARVE cleared",
    "test tone: changes seen" };

static short g1t_dac_cmd(short ch, int argc, const char * argv[])
{
    char m[160];
    long mode = g1t_arg(argc, argv, 1, 0), secs = g1t_arg(argc, argv, 2, 5);
    short k;
    if (!g1t_from_flash(ch)) return -1;
    if (mode != 0 && mode != 2 && mode != 3) { print(ch, "TEST DAC [0 | 2 | 3] [seconds]: 0 checks, 2 tone, 3 scales\n"); return -1; }
    g1t_run(g1t_dac_main, (u32)mode, secs);
    if (mode == 0) {
        u32 nerr = g1t_r[3];
        for (k = 0; k < 19; k++) {
            const char * res = ((nerr >> k) & 1) ? "FAIL" : "ok";
            int kk = k;
            unsigned long got = g1t_d[k], exp = g1t_d[40 + k];
            if (k == 15) {
                unsigned long p3 = g1t_d[30], p2 = g1t_d[31];
                sprintf(m, "  %2d %-45s %6lu  (10884 +- 10; positions %lu / %lu)  %s\n", kk, dac_names[k], got, p3, p2, res);
            } else if (k == 18) {
                sprintf(m, "  %2d %-45s %08lX  %s\n", kk, dac_names[k], got, res);
            } else {
                sprintf(m, "  %2d %-45s %08lX  expected %08lX  %s\n", kk, dac_names[k], got, exp, res);
            }
            print(ch, m);
        }
        sprintf(m, "TEST DAC: %s\n", nerr ? "FAIL" : "PASS (none failed)");
        print(ch, m);
        return nerr ? -1 : 0;
    }
    /* tone / scales: mode 2 returns at once (the hardware tone runs), mode 3 plays until the deadline */
    while (timers_jiffies() < g1t_end) {
        /* wait: the sound plays until the deadline */
    }
    {   /* everything off */
        volatile u32 *P = (volatile u32 *)0xFEC22000UL;
        short v;
        for (v = 0; v < 16; v++) P[(0x400 + 32 * v) / 4] = 2;
        P[0] = 0;
    }
    print(ch, "TEST DAC: done (sound off)\n");
    return 0;
}

/* ---- TEST GDMA [mode] [seconds]: 0 = random transfers over all four banks (checked), 4 = diagnostics,
 *      speed (MB/s): 1 SDRAM copy, 2 SDRAM fill, 3 SDRAM odd-byte copy, 5 SDRAM -> SRAM, 6 SRAM -> SDRAM,
 *      7 SRAM -> SRAM, 8 SDRAM -> VRAM A, 9 VRAM A -> SDRAM, 10 VRAM B -> VRAM B, 11 SRAM fill, 12 VRAM A fill,
 *      13 SRAM -> SDRAM tenure 255 ---- */
static const char * const bank_nm[4] = { "SDRAM", "SRAM", "VRAM A", "VRAM B" };

static void print_pairs(short ch, const u32 *w)
{
    char m[100];
    short k;
    print(ch, "  bank pairs:");
    for (k = 0; k < 16; k++) {
        unsigned long n = w[k];
        sprintf(m, "%s %s>%s %lu", k ? "," : "", bank_nm[k / 4], bank_nm[k % 4], n);
        print(ch, m);
    }
    print(ch, "\n");
}

static short g1t_gdma_cmd(short ch, int argc, const char * argv[])
{
    static const char * const sp[14] = { "", "SDRAM copy", "SDRAM fill", "SDRAM odd-byte copy", "", "SDRAM -> SRAM",
        "SRAM -> SDRAM", "SRAM -> SRAM", "SDRAM -> VRAM A", "VRAM A -> SDRAM", "VRAM B -> VRAM B", "SRAM fill",
        "VRAM A fill", "SRAM -> SDRAM, tenure 255" };
    char m[200];
    long mode = g1t_arg(argc, argv, 1, 0), secs = g1t_arg(argc, argv, 2, 10);
    u32 *w = g1t_r;
    if (!g1t_from_flash(ch)) return -1;
    if (mode > 13) { print(ch, "TEST GDMA [mode 0-13] [seconds]\n"); return -1; }
    g1t_run(g1t_gdma_main, (u32)mode, secs);
    {
        unsigned long passes = w[1], dir = w[15];
        int md = (int)mode, s = (int)secs;
        sprintf(m, "mode %d: passes %lu in %d s, directed %lX (1 ERROR ok, 2 ABORT ok, 4 COUNT moved)\n", md, passes, s, dir);
        print(ch, m);
    }
    if (mode == 0) {
        unsigned long bad = w[3], d2 = w[16], fi = w[17], mi = w[18], ov = w[19];
        sprintf(m, "  transfers with a wrong byte: %lu (2D %lu, fill %lu, mirror %lu, overlap %lu)\n", bad, d2, fi, mi, ov);
        print(ch, m);
        if (bad) {
            unsigned long a = w[5], b = w[6], c = w[7], d = w[8], e = w[9], f = w[10], g = w[11], h = w[12], i = w[13], j = w[14];
            sprintf(m, "  first: transfer %lu byte %08lX got %02lX expected %02lX CTRL %lX SRC %08lX DST %08lX X %lu Y %lu STRIDE %08lX\n",
                    a, b, c, d, e, f, g, h, i, j);
            print(ch, m);
        }
        print_pairs(ch, &w[21]);
    } else if (mode == 4) {
        short k;
        for (k = 0; k < 45; k += 4) {
            unsigned long a = g1t_d[k], b = g1t_d[k + 1], c = g1t_d[k + 2], d = g1t_d[k + 3];
            int kk = k;
            sprintf(m, "  D(%2d..%2d): %08lX %08lX %08lX %08lX\n", kk, kk + 3, a, b, c, d);
            print(ch, m);
        }
    } else {
        unsigned long mb = w[1], mbs10 = secs ? (w[1] * 10) / (unsigned long)secs : 0;
        sprintf(m, "  %s: %lu MB in %d s = %lu.%lu MB/s\n", sp[mode], mb, (int)secs, mbs10 / 10, mbs10 % 10);
        print(ch, m);
    }
    {
        unsigned long late = w[20];
        sprintf(m, "  GUI late lines: %lu\n", late);
        print(ch, m);
    }
    if (mode == 0) {
        sprintf(m, "TEST GDMA: %s\n", (w[3] || (w[15] & 3) != 3) ? "FAIL" : "PASS");
        print(ch, m);
        return (w[3] || (w[15] & 3) != 3) ? -1 : 0;
    }
    return 0;
}

/* ---- TEST BLIT [mode] [seconds]: 0 = random blits + lists over all four banks (checked, GDMA beside), 4 = diagnostics,
 *      speed modes 1-3, 5-22 as blitrun.sh ---- */
static short g1t_blit_cmd(short ch, int argc, const char * argv[])
{
    static const char * const sp[23] = { "", "SDRAM fill 16 bpp", "SDRAM copy 16 bpp", "SDRAM scroll 16 bpp", "",
        "SDRAM XOR fill 16 bpp", "SDRAM keyed copy 16 bpp", "VRAM B fill 8 bpp", "SDRAM -> VRAM A copy 16 bpp",
        "SRAM fill 8 bpp", "mono (memory) -> SDRAM 16 bpp", "mono (DATA port, CPU fed) -> SDRAM 16 bpp transparent",
        "pattern fill SDRAM 16 bpp", "pattern fill SDRAM 8 bpp", "pattern fill SDRAM 32 bpp", "pattern XOR SDRAM 16 bpp",
        "pattern fill SDRAM 16 bpp 320 wide", "alpha copy SDRAM 16 bpp", "pixel-alpha copy SDRAM 32 bpp",
        "flat lines 16 bpp", "vertical XOR lines 16 bpp", "256 keyed 32x32 sprites 16 bpp, one list",
        "256 keyed 32x32 sprites 16 bpp, through the registers" };
    char m[200];
    long mode = g1t_arg(argc, argv, 1, 0), secs = g1t_arg(argc, argv, 2, 10);
    u32 *w = g1t_r;
    if (!g1t_from_flash(ch)) return -1;
    if (mode > 22) { print(ch, "TEST BLIT [mode 0-22] [seconds]\n"); return -1; }
    g1t_run(g1t_blit_main, (u32)mode, secs);
    {
        unsigned long passes = w[1];
        sprintf(m, "mode %d: passes %lu in %d s\n", (int)mode, passes, (int)secs);
        print(ch, m);
    }
    if (mode == 0) {
        unsigned long bad = w[3], fi = w[17], ov = w[18], ky = w[19], gd = w[16], gb = w[15];
        sprintf(m, "  blits with a wrong byte: %lu (fills %lu, overlaps %lu, keyed %lu); GDMA beside: %lu done, %lu wrong\n",
                bad, fi, ov, ky, gd, gb);
        print(ch, m);
        if (bad) {
            unsigned long a = w[5], b = w[6], c = w[7], d = w[8], e = w[9], f = w[10], g = w[11], h = w[12], i = w[13], j = w[14];
            sprintf(m, "  first: blit %lu byte %08lX got %02lX expected %02lX CTRL %lX SRC %08lX DST %08lX SIZE %08lX STRIDE %08lX PM %08lX\n",
                    a, b, c, d, e, f, g, h, i, j);
            print(ch, m);
        }
        print_pairs(ch, &w[21]);
        {
            unsigned long mo = w[37], pw = w[38], tr = w[39], pa = w[40], al = w[41], li = w[42], lp = w[43];
            unsigned long ls = w[44], le = w[45], lb = w[46], lf = w[47];
            sprintf(m, "  B2: mono blits %lu (transparent %lu), DATA port words %lu; B3: pattern blits %lu\n", mo, tr, pw, pa);
            print(ch, m);
            sprintf(m, "  B4: alpha blits %lu, lines %lu (%lu pixels); B5: lists %lu (%lu entries), wrong STATUS / LIST_ADDR %lu (first %lX)\n",
                    al, li, lp, ls, le, lb, lf);
            print(ch, m);
        }
    } else if (mode == 4) {
        short k;
        for (k = 0; k < 64; k += 4) {
            unsigned long a = g1t_d[k], b = g1t_d[k + 1], c = g1t_d[k + 2], d = g1t_d[k + 3];
            int kk = k;
            sprintf(m, "  D(%2d..%2d): %08lX %08lX %08lX %08lX\n", kk, kk + 3, a, b, c, d);
            print(ch, m);
        }
    } else {
        unsigned long kb = w[1], mbs10 = secs ? (w[1] * 10) / 1024 / (unsigned long)secs : 0;
        sprintf(m, "  %s: %lu KB in %d s = %lu.%lu MB/s written\n", sp[mode], kb, (int)secs, mbs10 / 10, mbs10 % 10);
        print(ch, m);
        if (mode == 21 || mode == 22) {
            unsigned long sps = secs ? (w[1] / 2 / (unsigned long)secs) : 0;	/* KB / 512 x 256 = KB / 2 */
            sprintf(m, "  = %lu sprites/s\n", sps);
            print(ch, m);
        }
    }
    {
        unsigned long late = w[20];
        sprintf(m, "  GUI late lines: %lu\n", late);
        print(ch, m);
    }
    if (mode == 0) {
        short bad = (w[3] || w[15] || w[46]) ? 1 : 0;
        sprintf(m, "TEST BLIT: %s\n", bad ? "FAIL" : "PASS");
        print(ch, m);
        return bad ? -1 : 0;
    }
    return 0;
}

/*
 * 2.2.0: the TEST DAC / GDMA / BLIT commands. Each takes its SDRAM areas from the allocator first
 * (mem_test_claim), runs the test, and gives them back. A test whose area is in use does not run.
 *
 * g1t_sdram_test - claim the tests' SDRAM, run one test, free the SDRAM
 * Input:  ch, argc, argv = the command line; test = the test to run; name = for the refusal and MEM;
 *         last_page = 1 for TEST BLIT, which also blits at the very end of the SDRAM ($05FF_FFF0)
 * Output: the test's result, or -1 if the SDRAM was in use or reserved memory reaches into the tests' SRAM
 */
#define G1T_SDRAM       0x03000000UL    /* $0300_0000 - $04FF_FFFF: every SDRAM area of the three tests */
#define G1T_SDRAM_SIZE  0x02000000UL
#define G1T_LAST_PAGE   0x05FFF000UL    /* TEST BLIT's "leaves the SDRAM" check */
#define G1T_SRAM_END    0x00341000UL    /* one past the tests' SRAM ($0010_0000-$0034_0FFF) */

static short g1t_sdram_test(short ch, int argc, const char * argv[],
                            short (*test)(short, int, const char * []), const char * name, short last_page) {
    char m[120];
    short result;

    /* Memory taken with sys_mem_reserve lies below the top of RAM: the tests' SRAM must stay under it */
    if (mem_get_ramtop() < G1T_SRAM_END) {
        sprintf(m, "%s needs SRAM up to $%08lX, but memory from $%08lX is reserved (see MEM).\n",
                name, (unsigned long)(G1T_SRAM_END - 1), (unsigned long)mem_get_ramtop());
        print(ch, m);
        return -1;
    }
    if (!mem_test_claim(ch, G1T_SDRAM, G1T_SDRAM_SIZE, name)) {
        return -1;
    }
    if (last_page && !mem_test_claim(ch, G1T_LAST_PAGE, MEM_PAGE_SIZE, name)) {
        mem_sdram_free(G1T_SDRAM);
        return -1;
    }
    result = test(ch, argc, argv);
    mem_sdram_free(G1T_SDRAM);
    if (last_page) {
        mem_sdram_free(G1T_LAST_PAGE);
    }
    return result;
}

/* TEST DAC [mode] [seconds]. Mode 2 leaves the tone playing from SDRAM after the command ends;
   once the area is free again, a program that writes there only changes the sound. */
short cli_test_dac(short ch, int argc, const char * argv[]) {
    return g1t_sdram_test(ch, argc, argv, g1t_dac_cmd, "TEST DAC", 0);
}

/* TEST GDMA [mode] [seconds] */
short cli_test_gdma(short ch, int argc, const char * argv[]) {
    return g1t_sdram_test(ch, argc, argv, g1t_gdma_cmd, "TEST GDMA", 0);
}

/* TEST BLIT [mode] [seconds] */
short cli_test_blit(short ch, int argc, const char * argv[]) {
    return g1t_sdram_test(ch, argc, argv, g1t_blit_cmd, "TEST BLIT", 1);
}
