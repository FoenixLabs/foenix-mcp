/*
 * vge_test.c - Gen1 VGE tests for the MCP (2026-11-05): TEST VGE (the board checks of host/vgetest, stages V1 - V5)
 * and TEST VGEDEMO (256 bouncing sprites with collision until a key is pressed).
 *
 * The VGE registers are at $FD00_0000 (see the K25 hardware reference, VGE chapter). The tests use VRAM A
 * ($0040_0000-$007F_FFFF) and VRAM B ($0080_0000-$00BF_FFFF) freely: the 108 MHz screen (text / MemText / TOSGraph)
 * is off while the VGE is on and its VRAM contents are overwritten - clear the screen afterwards if needed.
 */

#include <stdio.h>
#include <string.h>
#include "syscalls.h"
#include "dev/channel.h"
#include "cli/vge_test.h"

typedef unsigned long u32;
typedef unsigned char u8;
typedef unsigned short u16;

#define V(o)        (*(volatile u32 *)(0xFD000000UL + (o)))
#define IRQ_PEND0   (*(volatile u16 *)0xFEC00100UL)        /* GAVIN pending 15..0: 8 = SOF B, 9 = line B */

#define CTRL        0x0000
#define STATUS      0x0004
#define BG          0x0008
#define FADE        0x000C
#define LIRQ        0x0010
#define FRAME       0x0014
#define ORDER0      0x0018
#define ORDER1      0x001C
#define SP_COMMIT   0x0020
#define SP_STAT     0x0024
#define OPAC        0x0100
#define BM(n)       (0x1000 + 0x40 * (n))   /* +0 CTRL, +4 BASE, +8 STRIDE, +C SIZE, +10 SCROLL */
#define TL(n)       (0x2000 + 0x40 * (n))   /* +0 CTRL, +4 MAP, +8 MAP_SIZE, +C SET, +10 SCROLL, +14 LINE_TABLE */
#define COLL_EN     0x3000
#define COLL_ANY    0x3004
#define COLL_W(s)   (0x3010 + 4 * (s))
#define SP_HIT(w)   (0x3040 + 4 * (w))
#define GAM_R       0x5000
#define GAM_G       0x5400
#define GAM_B       0x5800
#define SPR(n)      (0x20000UL + 16 * (n))
#define PAL         0x40000UL

#define C_ON        1UL
#define C_400       2UL
#define C_DBL       4UL
#define C_GAM       8UL

static short vt_chan;
static int vt_fail;
static char vt_msg[96];

static void vt_out(const char *s) { sys_chan_write(vt_chan, (unsigned char *)s, strlen(s)); }

static void vt_check(int k, const char *name, u32 got, u32 want)
{
    sprintf(vt_msg, "%2d %-44s %08lX %s\n", k, name, got, (got == want) ? "ok" : "FAIL");
    if (got != want) vt_fail++;
    vt_out(vt_msg);
}

static u32 vt_frame(void) { return V(FRAME) & 0xFFFF; }

static int vt_wait(u32 n)
{
    u32 f0 = vt_frame(), k;
    for (k = 0; k < 4000000UL; k++)
        if (((vt_frame() - f0) & 0xFFFF) >= n) return 1;
    return 0;
}

static u32 vt_rnd_s = 12345;
static u32 vt_rnd(void) { vt_rnd_s = vt_rnd_s * 1103515245UL + 12345UL; return vt_rnd_s >> 8; }

/* the test picture: colour bars, a grid every 32 pixels, a grey ramp, a 1-pixel frame */
static void vt_picture(volatile u8 *a, int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        u8 c;
        if (x == 0 || y == 0 || x == w - 1 || y == h - 1) c = 255;
        else if ((x & 31) == 0 || (y & 31) == 0) c = 15;
        else if (y < h / 2) c = 1 + (x * 8) / w;
        else c = 16 + (x * 224) / w;
        a[y * w + x] = c;
    }
}

static void vt_palette(int p)
{
    static const u32 bars[9] = { 0, 0xFFFF0000UL, 0xFF00FF00UL, 0xFF0000FFUL, 0xFFFFFF00UL, 0xFF00FFFFUL, 0xFFFF00FFUL,
                                 0xFFFFFFFFUL, 0xFF808080UL };
    int i;
    for (i = 0; i < 256; i++) {
        u32 v;
        if (i < 9) v = bars[i];
        else if (i == 15) v = 0xFF404040UL;
        else if (i == 255) v = 0xFFFFFFFFUL;
        else if (i >= 16) { u32 g = ((u32)(i - 16) * 255) / 223; v = 0xFF000000UL | (g << 16) | (g << 8) | g; }
        else v = 0xFF000000UL | ((u32)i * 0x111111UL);
        V(PAL + 0x400 * p + 4 * i) = v;
    }
}

static void vt_gamma_identity(void)
{
    int i;
    for (i = 0; i < 256; i++) { V(GAM_R + 4 * i) = i; V(GAM_G + 4 * i) = i; V(GAM_B + 4 * i) = i; }
}

static void vt_bm(int n, u32 base, u32 stride, u32 w, u32 h, u32 sx, u32 sy, int pal)
{
    V(BM(n) + 0x04) = base;
    V(BM(n) + 0x08) = stride;
    V(BM(n) + 0x0C) = (h << 16) | w;
    V(BM(n) + 0x10) = (sy << 16) | sx;
    V(BM(n) + 0x00) = 1 | (pal << 1);
}

/* STATUS.LATE after n frames with the CPU reading / writing both banks */
static u32 vt_late(u32 n)
{
    volatile u32 *a = (volatile u32 *)0x00700000UL, *b = (volatile u32 *)0x00B00000UL;
    u32 f0, k = 0, v = 0;
    V(STATUS) = 4;
    f0 = vt_frame();
    while (((vt_frame() - f0) & 0xFFFF) < n) {
        a[k & 4095] = k; b[k & 4095] = ~k;
        v += a[(k + 7) & 4095] + b[(k + 3) & 4095];
        k++;
    }
    return (V(STATUS) & 4) | (v & 0);
}

/* a ball of size z (8 / 16 / 32): colour c inside, 2 on the rim, 0 outside */
static void vt_ball(volatile u8 *a, int z, u8 c)
{
    int x, y;
    for (y = 0; y < z; y++) for (x = 0; x < z; x++) {
        int dx = 2 * x - z + 1, dy = 2 * y - z + 1, r2 = dx * dx + dy * dy, R = z * z;
        a[y * z + x] = (r2 < R - 4 * z) ? c : (r2 < R) ? 2 : 0;
    }
}

static void vt_all_off(void)
{
    int i;
    V(CTRL) = 0;
    V(BM(0)) = 0; V(BM(1)) = 0;
    for (i = 0; i < 4; i++) V(TL(i)) = 0;
    for (i = 0; i < 256; i++) V(SPR(i) + 8) = 0;
    V(SP_COMMIT) = 0xF;
    V(COLL_EN) = 0;
    V(ORDER0) = 0x76543210UL; V(ORDER1) = 0x98;
    for (i = 0; i < 10; i++) V(OPAC + 4 * i) = 255;
}

/*
 * TEST VGE: the VGE board checks (V1 - V5). Takes about 15 s; the screen shows the VGE pictures meanwhile.
 */
short cli_test_vge(short channel, int argc, const char * argv[])
{
    u32 v, bad;
    int i;

    vt_chan = channel;
    vt_fail = 0;
    vt_out("VGE test (stages V1 - V5); the screen switches to the VGE and back\n");
    vt_all_off();
    vt_gamma_identity();

    /* ---- V1 */
    bad = 0;
    V(BG) = 0x00123456;  if (V(BG) != 0x00123456UL) bad |= 1;
    V(FADE) = 0x5A;      if (V(FADE) != 0x5A) bad |= 2;
    V(LIRQ) = 0x80000064UL; if (V(LIRQ) != 0x80000064UL) bad |= 4;
    vt_bm(0, 0x00412340UL, 1284, 1280, 600, 17, 33, 1);
    if (V(BM(0) + 4) != 0x00412340UL) bad |= 8;
    if (V(BM(0) + 0xC) != ((600UL << 16) | 1280)) bad |= 32;
    vt_check(0, "registers read back", bad, 0);
    bad = 0;
    for (i = 0; i < 512; i++) V(PAL + 4 * i) = 0x01010101UL * i ^ 0xA5C3E10FUL;
    for (i = 0; i < 512; i++) if (V(PAL + 4 * i) != (0x01010101UL * i ^ 0xA5C3E10FUL)) bad++;
    vt_check(1, "palettes read back (wrong entries)", bad, 0);
    bad = 0;
    for (i = 0; i < 256; i++) { V(GAM_R + 4 * i) = 255 - i; V(GAM_G + 4 * i) = i ^ 0x55; V(GAM_B + 4 * i) = (i * 7) & 255; }
    for (i = 0; i < 256; i++) {
        if ((V(GAM_R + 4 * i) & 255) != (u32)(255 - i)) bad++;
        if ((V(GAM_G + 4 * i) & 255) != (u32)(i ^ 0x55)) bad++;
        if ((V(GAM_B + 4 * i) & 255) != (u32)((i * 7) & 255)) bad++;
    }
    vt_gamma_identity();
    vt_check(2, "gamma read back (wrong entries)", bad, 0);
    vt_check(3, "unused addresses read 0", V(0x4000) | V(0x0FFFFCUL) | V(0x1080), 0);

    vt_picture((volatile u8 *)0x00400000UL, 640, 480);
    vt_palette(0);
    V(BG) = 0x00200040; V(FADE) = 0; V(LIRQ) = 0;
    vt_bm(0, 0x00400000UL, 640, 640, 480, 0, 0, 0);
    V(CTRL) = C_ON;
    vt_check(4, "VGE runs (frames, STATUS.ON)", vt_wait(10) ? (V(STATUS) & 1) : 2, 1);
    vt_check(5, "no LATE line, CPU on both banks", vt_late(30), 0);
    IRQ_PEND0 = 0x0300;
    vt_wait(2);
    vt_check(6, "SOF interrupt pending (GAVIN bit 8)", IRQ_PEND0 & 0x0100, 0x0100);
    V(LIRQ) = 0x80000000UL | 100;
    vt_wait(2); IRQ_PEND0 = 0x0200; vt_wait(2);
    v = IRQ_PEND0 & 0x0200;
    V(LIRQ) = 0;
    vt_wait(2); IRQ_PEND0 = 0x0200; vt_wait(3);
    vt_check(7, "line interrupt (GAVIN bit 9) on / off", v | ((IRQ_PEND0 & 0x0200) >> 8), 0x0200);
    {
        u32 a = (V(STATUS) >> 16) & 0x3FF, b = a, k;
        for (k = 0; k < 2000; k++) { b = (V(STATUS) >> 16) & 0x3FF; if (b != a) break; }
        vt_check(8, "STATUS line field moves", b != a, 1);
    }
    V(CTRL) = C_ON | C_400;
    vt_check(9, "640x400: frames, ON", vt_wait(10) ? (V(STATUS) & 1) : 2, 1);
    vt_picture((volatile u8 *)0x00800000UL, 320, 240);
    vt_bm(0, 0x00800000UL, 320, 320, 240, 5, 7, 0);
    V(FADE) = 0x40;
    V(CTRL) = C_ON | C_DBL | C_GAM;
    vt_wait(3);
    vt_check(10, "VRAM B, doubled, gamma + fade: no LATE", vt_late(20), 0);
    V(CTRL) = 0;
    for (v = 0; v < 200000UL && (V(STATUS) & 1); v++) ;
    vt_check(11, "off: STATUS.ON clears", V(STATUS) & 1, 0);

    /* ---- V2 */
    bad = 0;
    vt_bm(1, 0x00856780UL, 2052, 2048, 300, 41, 9, 1);
    if (V(BM(1) + 4) != 0x00856780UL) bad |= 1;
    if (V(BM(1) + 0x10) != ((9UL << 16) | 41)) bad |= 8;
    V(ORDER0) = 0x01234567UL; V(ORDER1) = 0xFF89;
    if (V(ORDER0) != 0x01234567UL) bad |= 64;
    if (V(ORDER1) != 0x89) bad |= 128;
    for (i = 0; i < 10; i++) V(OPAC + 4 * i) = 0x10 * i + 3;
    for (i = 0; i < 10; i++) if (V(OPAC + 4 * i) != (u32)(0x10 * i + 3)) bad |= 256;
    V(ORDER0) = 0x76543210UL; V(ORDER1) = 0x98;
    for (i = 0; i < 10; i++) V(OPAC + 4 * i) = 255;
    vt_check(12, "V2 registers (BM1, order, opacity)", bad, 0);
    bad = 0;
    for (i = 0; i < 512; i++) { V(PAL + 4 * i) = 0x11111111UL * (i & 15) ^ i; V(PAL + 0x800 + 4 * i) = ~(0x01010101UL * i); }
    for (i = 0; i < 512; i++) {
        if (V(PAL + 4 * i) != (0x11111111UL * (i & 15) ^ i)) bad++;
        if (V(PAL + 0x800 + 4 * i) != ~(0x01010101UL * i)) bad++;
    }
    vt_check(13, "V2 BM1 palettes (wrong entries)", bad, 0);
    vt_palette(0);
    vt_picture((volatile u8 *)0x00400000UL, 640, 480);
    vt_bm(0, 0x00400000UL, 640, 640, 480, 0, 0, 0);
    vt_bm(1, 0x00400000UL, 640, 640, 480, 3, 5, 0);
    V(FADE) = 0;
    V(CTRL) = C_ON;
    vt_wait(3);
    vt_check(14, "V2 both layers on VRAM A: no LATE", vt_late(30), 0);
    vt_picture((volatile u8 *)0x00800000UL, 640, 480);
    vt_bm(1, 0x00800000UL, 640, 640, 480, 7, 1, 0);
    vt_wait(3);
    v = vt_late(30);
    V(CTRL) = C_ON | C_DBL;
    vt_wait(3);
    vt_check(15, "V2 A + B at once, doubled: no LATE", v | (vt_late(30) << 1), 0);
    V(CTRL) = 0;
    V(BM(1)) = 0;

    /* ---- V3 */
    bad = 0;
    for (i = 0; i < 4; i++) {
        V(TL(i) + 0x04) = 0x00410000UL + 0x1000 * i;
        V(TL(i) + 0x08) = (i << 4) | (3 - i);
        V(TL(i) + 0x0C) = 0x00840000UL + 0x2000 * i;
        V(TL(i) + 0x10) = ((100UL + i) << 16) | (2000 + i);
        V(TL(i) + 0x14) = 0x00460000UL + 0x100 * i;
        V(TL(i) + 0x00) = 6 - (i & 1);
    }
    for (i = 0; i < 4; i++) {
        if (V(TL(i) + 0x00) != (u32)(6 - (i & 1))) bad |= 1;
        if (V(TL(i) + 0x04) != 0x00410000UL + 0x1000 * i) bad |= 2;
        if (V(TL(i) + 0x08) != (u32)((i << 4) | (3 - i))) bad |= 4;
        if (V(TL(i) + 0x0C) != 0x00840000UL + 0x2000 * i) bad |= 8;
        if (V(TL(i) + 0x10) != (((100UL + i) << 16) | (2000 + i))) bad |= 16;
        if (V(TL(i) + 0x14) != 0x00460000UL + 0x100 * i) bad |= 32;
        if (V(TL(i) + 0x18) != 0) bad |= 64;
        V(TL(i)) = 0;
    }
    vt_check(16, "V3 tile layer registers", bad, 0);
    bad = 0;
    for (i = 0; i < 2048; i++) V(PAL + 0x1000 + 4 * i) = 0x9E3779B9UL * (i + 1);
    for (i = 0; i < 2048; i++) if (V(PAL + 0x1000 + 4 * i) != 0x9E3779B9UL * (i + 1)) bad++;
    vt_check(17, "V3 tile palettes (wrong entries)", bad, 0);
    vt_bm(0, 0x00400000UL, 640, 640, 480, 0, 0, 0);
    vt_bm(1, 0x00800000UL, 640, 640, 480, 0, 0, 0);
    for (i = 0; i < 4; i++) {
        V(TL(i) + 0x04) = (i & 1) ? 0x00880000UL : 0x00480000UL;
        V(TL(i) + 0x0C) = (i == 0 || i == 3) ? 0x00900000UL : 0x00500000UL;
        V(TL(i) + 0x08) = 0x11;
        V(TL(i) + 0x10) = ((u32)(17 * i) << 16) | (29 * i);
        V(TL(i) + 0x14) = 0x00A00000UL;
        V(TL(i) + 0x00) = 1 | (i >= 2 ? 2 : 0) | (i == 3 ? 4 : 0);
    }
    V(CTRL) = C_ON;
    vt_wait(3);
    v = vt_late(30);
    V(CTRL) = C_ON | C_400;
    vt_wait(3);
    vt_check(18, "V3 six layers, both banks: no LATE", v | (vt_late(30) << 1), 0);
    vt_all_off();

    /* ---- V4 */
    bad = 0;
    for (i = 0; i < 256; i++) {
        V(SPR(i) + 0) = ((u32)((i * 3) & 0x7FF) << 16) | ((i * 5) & 0x7FF);
        V(SPR(i) + 4) = 0x00500000UL + 0x100 * i;
        V(SPR(i) + 8) = 0x7E & (i * 2);
    }
    for (i = 0; i < 256; i++) {
        if (V(SPR(i) + 0) != (((u32)((i * 3) & 0x7FF) << 16) | ((i * 5) & 0x7FF))) bad |= 1;
        if (V(SPR(i) + 4) != 0x00500000UL + 0x100 * i) bad |= 2;
        if (V(SPR(i) + 8) != (u32)(0x7E & (i * 2))) bad |= 4;
        if (V(SPR(i) + 12) != 0) bad |= 8;
    }
    V(CTRL) = C_ON;
    V(SP_COMMIT) = 0xF;
    if ((V(SP_COMMIT) & 0xF) != 0xF) bad |= 16;
    /* done at the next frame start + the copy (about 1.3 us): a COMMIT still waiting from vt_all_off (VGE off) is
       copied right at the enable, this one then at the following frame start - so poll, up to 3 frames */
    v = vt_frame();
    while ((V(SP_COMMIT) & 0xF) != 0 && ((vt_frame() - v) & 0xFFFF) < 3) ;
    if ((V(SP_COMMIT) & 0xF) != 0) bad |= 32;
    vt_check(19, "V4 sprite registers + COMMIT", bad, 0);
    vt_ball((volatile u8 *)0x00500000UL, 16, 3);
    vt_ball((volatile u8 *)0x00900000UL, 16, 4);
    for (i = 0; i < 256; i++) {
        V(SPR(i) + 0) = ((u32)((i * 7) % 480) << 16) | ((i * 37) % 640);
        V(SPR(i) + 4) = (i < 128) ? 0x00500000UL : 0x00900000UL;
        V(SPR(i) + 8) = 1 | 2;
    }
    vt_bm(0, 0x00400000UL, 640, 640, 480, 0, 0, 0);
    V(SP_COMMIT) = 0xF;
    vt_wait(3);
    vt_check(20, "V4 256 sprites + bitmap: no LATE", vt_late(30), 0);
    V(SP_STAT) = 0xF;
    for (i = 0; i < 40; i++) V(SPR(i) + 0) = (100UL << 16) | (i * 15);
    V(SP_COMMIT) = 1;
    vt_wait(3);
    vt_check(21, "V4 40 sprites on a line: OVERFLOW 0", V(SP_STAT) & 0xF, 1);
    vt_all_off();

    /* ---- V5 */
    V(COLL_EN) = 0x2A5;
    vt_check(22, "V5 COLL_ENABLE read back", V(COLL_EN), 0x2A5);
    vt_ball((volatile u8 *)0x00500000UL, 16, 3);
    V(SPR(0) + 0) = (100UL << 16) | 200;  V(SPR(0) + 4) = 0x00500000UL;  V(SPR(0) + 8) = 1 | 2 | 64;
    V(SPR(64) + 0) = (104UL << 16) | 206; V(SPR(64) + 4) = 0x00500000UL; V(SPR(64) + 8) = 1 | 2 | 64;
    V(SPR(1) + 0) = (300UL << 16) | 400;  V(SPR(1) + 4) = 0x00500000UL;  V(SPR(1) + 8) = 1 | 2 | 64;
    V(COLL_EN) = 0x3C0;
    V(CTRL) = C_ON;
    V(SP_COMMIT) = 0xF;
    vt_wait(3);
    bad = 0;
    if (V(SP_HIT(0)) != 1) bad |= 1;
    if (V(SP_HIT(1)) != 0) bad |= 2;
    if (V(SP_HIT(2)) != 1) bad |= 4;
    if (V(SP_HIT(3)) | V(SP_HIT(4)) | V(SP_HIT(5)) | V(SP_HIT(6)) | V(SP_HIT(7))) bad |= 8;
    if (V(COLL_W(6)) != 0x080) bad |= 16;
    if (V(COLL_W(7)) != 0x040) bad |= 32;
    if (V(COLL_ANY) != 0x0C0) bad |= 64;
    vt_check(23, "V5 two sprites touch: SP_HIT / WITH / ANY", bad, 0);
    V(COLL_EN) = 0;
    vt_wait(3);
    vt_check(24, "V5 COLL_ENABLE 0: nothing reported", V(COLL_ANY) | V(SP_HIT(0)) | V(COLL_W(6)), 0);
    vt_all_off();

    sprintf(vt_msg, "VGE test: %d check(s) failed\n", vt_fail);
    vt_out(vt_msg);
    return vt_fail ? -1 : 0;
}

/*
 * TEST VGEDEMO: 256 balls (groups 0-1 16 px, group 2 8 px, group 3 32 px; images in VRAM A and B) bouncing on the
 * VGE screen; a ball that touched another one last frame is drawn with its second palette (grey / white).
 * Any key (keyboard or the serial console) stops it.
 */
short cli_test_vgedemo(short channel, int argc, const char * argv[])
{
    static short bx[256], by[256], vx[256], vy[256];
    int n, k;
    u32 f, frames = 0, f0;

    vt_chan = channel;
    vt_all_off();
    vt_gamma_identity();
    for (k = 0; k < 4; k++) {
        vt_ball((volatile u8 *)(0x00500000UL + 0x400 * k), 16, 3 + k);
        vt_ball((volatile u8 *)(0x00900000UL + 0x400 * k), 16, 3 + k);
        vt_ball((volatile u8 *)(0x00510000UL + 0x400 * k), 8, 3 + k);
        vt_ball((volatile u8 *)(0x00910000UL + 0x400 * k), 32, 3 + k);
    }
    for (n = 6; n <= 9; n++) {
        u32 p0 = PAL + 0x400 * (2 * n), p1 = PAL + 0x400 * (2 * n + 1);
        V(p0 + 8) = 0xFFFFFFFFUL; V(p0 + 12) = 0xFFFF4040UL; V(p0 + 16) = 0xFF40FF40UL;
        V(p0 + 20) = 0xFF4080FFUL; V(p0 + 24) = 0xC0FFFF40UL;
        V(p1 + 8) = 0xFF000000UL; V(p1 + 12) = 0xFF808080UL; V(p1 + 16) = 0xFFC0C0C0UL;
        V(p1 + 20) = 0xFFFF80FFUL; V(p1 + 24) = 0x80FFFFFFUL;
    }
    for (n = 0; n < 256; n++) {
        int g = n >> 6, sz = (g == 2) ? 0 : (g == 3) ? 2 : 1;
        u32 img = (g == 0) ? 0x00500000UL : (g == 1) ? 0x00900000UL : (g == 2) ? 0x00510000UL : 0x00910000UL;
        bx[n] = vt_rnd() % 600; by[n] = vt_rnd() % 440;
        vx[n] = 1 + vt_rnd() % 4; vy[n] = 1 + vt_rnd() % 3;
        if (vt_rnd() & 1) vx[n] = -vx[n];
        V(SPR(n) + 4) = img + 0x400 * (n & 3);
        V(SPR(n) + 8) = 1 | (sz << 1) | ((n & 4) ? 8 : 0) | 64;
    }
    V(BG) = 0x00101828;
    V(COLL_EN) = 0x3C0;
    V(CTRL) = C_ON;
    vt_out("VGE demo: 256 sprites with collision - press a key to stop\n");
    f0 = vt_frame();
    while (!(sys_chan_status(channel) & CDEV_STAT_READABLE)) {
        for (n = 0; n < 256; n++) {
            int lim = ((n >> 6) == 3) ? 32 : ((n >> 6) == 2) ? 8 : 16;
            bx[n] += vx[n]; by[n] += vy[n];
            if (bx[n] < 0 || bx[n] > 640 - lim) { vx[n] = -vx[n]; bx[n] += 2 * vx[n]; }
            if (by[n] < 0 || by[n] > 480 - lim) { vy[n] = -vy[n]; by[n] += 2 * vy[n]; }
            V(SPR(n)) = ((u32)(by[n] & 0x7FF) << 16) | (bx[n] & 0x7FF);
        }
        for (k = 0; k < 8; k++) {
            u32 h = V(SP_HIT(k));
            for (n = 0; n < 32; n++) {
                int m = 32 * k + n, g = m >> 6, sz = (g == 2) ? 0 : (g == 3) ? 2 : 1;
                V(SPR(m) + 8) = 1 | (sz << 1) | ((m & 4) ? 8 : 0) | 64 | (((h >> n) & 1) ? 32 : 0);
            }
        }
        f = vt_frame();
        V(SP_COMMIT) = 0xF;
        while (vt_frame() == f) ;
        /* FRAME counts at the start of blanking, the copy is at its end: rewriting NEXT before it would tear */
        while ((V(SP_COMMIT) & 0xF) != 0 && ((vt_frame() - f) & 0xFFFF) < 3) ;
        frames++;
    }
    sys_chan_read_b(channel);
    vt_all_off();
    sprintf(vt_msg, "VGE demo: %lu frames (VGE frame counter %lu)\n", frames, (vt_frame() - f0) & 0xFFFF);
    vt_out(vt_msg);
    return 0;
}
