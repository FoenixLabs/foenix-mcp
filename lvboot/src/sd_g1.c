/*
 * sd_g1.c - Gen1 LVGL boot loader: the front SD card (/sd in the MCP) through G1_SDC, for FatFs (2026-11-06; from the
 * A2560Me loader's sd2.c and the Gen1 MCP's sdc_new.c).
 *
 * Byte interface: a write to SD_DATA shifts one byte out and one in (BUSY = SD_CTRL bit 7); bit 0 = chip select, bit 1 =
 * slow clock for the initialisation. Commands, answers and writes go through it (standard SD SPI: CMD0, CMD8, ACMD41,
 * CMD58; CMD17/18, CMD24/25). Reads use the block engine when it is there (K25 build 1107+, found by writing CLKDIV and
 * reading it back): BLK_CMD = N << 16 | 1 takes N data packets (token, 512 bytes, CRC16 checked) into the FIFO.
 * The engine is not reset by a CPU-only reset, and a card left inside CMD25 needs the STOP_TRAN token: both are
 * cleared first (as MCP 2.0.19).
 *
 * 1.5 (2026-11-25): nothing on the board ever resets the SD card - not the reset button, not REBOOT, not a debug-port
 * release - so after a warm reset the card is still in whatever state the last program left it. Booting then took a
 * very long time: the CMD12 that should stop a card still inside a multi-block read went out with the card NOT
 * selected (the card ignores it), and every CMD0 first waited 0.5 s for a "ready" byte that a card still sending
 * data never gives. Now CMD12 goes out with CS on, CMD0 is sent without the ready wait, an empty slot is seen at once
 * (card-detect pin) and one disk_initialize() takes a few seconds at most.
 */
#include "hw.h"
#include "board.h"
#include "ff.h"
#include "diskio.h"

#define CMD0    0
#define CMD1    1
#define CMD8    8
#define CMD12   12
#define CMD16   16
#define CMD17   17
#define CMD18   18
#define CMD24   24
#define CMD25   25
#define CMD55   55
#define CMD58   58
#define ACMD23  (0x80 + 23)
#define ACMD41  (0x80 + 41)

#define CT_SDC2  0x01
#define CT_BLOCK 0x02

static uint8_t card_type;
static DSTATUS stat = STA_NOINIT;
static int engine;                  /* 1 = G1_SDC block engine present */
uint32_t sd_retries;

static uint8_t xchg(uint8_t b) {
    uint32_t n = 100000;
    SD_DATA = b;
    (void)SD_CTRL;                  /* K60 (11-07): one read first, as the MCP does - the 66 MHz 060 can poll before BUSY is up */
    while ((SD_CTRL & SDX_BUSY) && --n) ;
    return SD_DATA;
}

static int wait_ready(uint32_t ms) {
    uint32_t t0 = board_ms();
    do {
        if (xchg(0xFF) == 0xFF) return 1;
    } while (board_ms() - t0 < ms);
    return 0;
}

static void deselect(void) {
    SD_CTRL &= (uint8_t)~SDX_CS;
    xchg(0xFF);                     /* one more clock: the card releases its data output */
}

static int select_card(void) {
    SD_CTRL |= SDX_CS;
    xchg(0xFF);
    if (wait_ready(500)) return 1;
    deselect();
    return 0;
}

/*
 * card_present - is there a card in the slot?
 *
 * Input:  none
 * Output: 1 = the card-detect pin says a card is in the slot, 0 = the slot is empty
 *
 * Why: without a card every command times out; checking the pin first makes an empty slot cost nothing.
 */
static int card_present(void) {
    return (SD_CD_REG & SD_CD_EMPTY) ? 0 : 1;
}

/*
 * send_cmd - send one command to the card and read its first answer byte
 *
 * Input:  cmd = command number (ACMDn = $80 + n: CMD55 is sent first), arg = the 32-bit argument
 * Output: the R1 response (bit 7 set = no answer)
 *
 * CMD12 is sent on the card as it is selected now (it must reach a card that is in the middle of sending data).
 * CMD0 selects the card without waiting for it to be ready: a card left in the middle of a read never sends the
 * $FF "ready" byte, and CMD0 is the command that brings it back.
 */
static uint8_t send_cmd(uint8_t cmd, uint32_t arg) {
    uint8_t r, n;
    if (cmd & 0x80) {               /* ACMDn = CMD55 + CMDn */
        cmd &= 0x7F;
        r = send_cmd(CMD55, 0);
        if (r > 1) return r;
    }
    if (cmd == CMD0) {
        deselect();
        SD_CTRL |= SDX_CS;
        xchg(0xFF);
    } else if (cmd != CMD12) {
        deselect();
        if (!select_card()) return 0xFF;
    }
    xchg((uint8_t)(0x40 | cmd));
    xchg((uint8_t)(arg >> 24));
    xchg((uint8_t)(arg >> 16));
    xchg((uint8_t)(arg >> 8));
    xchg((uint8_t)arg);
    xchg(cmd == CMD0 ? 0x95 : cmd == CMD8 ? 0x87 : 0x01);      /* the two CRCs the card checks before init */
    if (cmd == CMD12) xchg(0xFF);
    n = 10;
    do r = xchg(0xFF); while ((r & 0x80) && --n);
    return r;
}

static int rx_block(uint8_t * buf, uint32_t len) {
    uint8_t t;
    uint32_t t0 = board_ms();
    do t = xchg(0xFF); while (t == 0xFF && board_ms() - t0 < 200);
    if (t != 0xFE) return 0;
    while (len--) *buf++ = xchg(0xFF);
    xchg(0xFF); xchg(0xFF);         /* CRC */
    return 1;
}

/* the block engine: count packets into buf (long-word aligned); 0 = all there, CRC right */
static int engine_read(uint8_t * buf, uint32_t count) {
    uint32_t * lp = (uint32_t *)buf;
    uint32_t left = count, st;
    int i;
    SD_BLKCMD = (count << 16) | 1;
    while (left) {
        while (SD_FIFOCNT < 128) {
            st = SD_BLKSTAT;
            if (!(st & 1) && SD_FIFOCNT < 128) break;      /* engine stopped: time-out */
        }
        if (SD_FIFOCNT < 128) break;
        for (i = 0; i < 128; i++) *lp++ = SD_FIFO;
        left--;
    }
    while (SD_BLKSTAT & 1) ;
    st = SD_BLKSTAT;
    SD_BLKCMD = 3;
    return (left || (st & 0x06)) ? 0 : 1;
}

static int tx_block(const uint8_t * buf, uint8_t token) {
    uint32_t i;
    if (!wait_ready(500)) return 0;
    xchg(token);
    if (token == 0xFD) return 1;    /* stop token */
    for (i = 0; i < 512; i++) xchg(buf[i]);
    xchg(0xFF); xchg(0xFF);         /* dummy CRC */
    return (xchg(0xFF) & 0x1F) == 0x05;
}

/* 11-07: volume "1:" = the IDE drive (ide_g1.c) */
DSTATUS ide_disk_initialize(void);
DSTATUS ide_disk_status(void);
DRESULT ide_disk_read(BYTE * buff, LBA_t sector, UINT count);
DRESULT ide_disk_write(const BYTE * buff, LBA_t sector, UINT count);
DRESULT ide_disk_ioctl(BYTE cmd, void * buff);

/*
 * disk_initialize - FatFs: start the SD card (volume 0) or the IDE drive (volume 1)
 *
 * Input:  pdrv = 0 for the SD card, 1 for the IDE drive
 * Output: 0 = ready, STA_NOINIT = no card / the card does not answer
 *
 * The card may still be in the middle of something from before a warm reset (see the top of the file), so first
 * everything it could be busy with is ended: a multi-block write (STOP_TRAN token), a multi-block read (CMD12, card
 * selected), then CMD0 puts it back into the idle state and the normal SD start-up follows. Three tries in all.
 */
DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv == 1) return ide_disk_initialize();
    uint8_t ocr[4], ty = 0, cmd, r0 = 0xFF;
    int i, tries;
    uint32_t t0;

    if (pdrv) return STA_NOINIT;
    SD_BLKCMD = 3;                                      /* FIRST: abort a block command left running (it blocks BUSY) */
    SD_CLKDIV = 0x0000000A;                             /* engine there? (older FPGAs: CLKDIV does not read back) */
    engine = (SD_CLKDIV == 0x0000000AUL);
    if (engine) { SD_CLKDIV = 1; SD_BLKCMD = 3; }       /* K60 (11-07): the reset divider during the card start-up, as the
                                                           MCP does (CLKDIV 0 before it: CMD0 answered $4F on the K60) */
    if (!card_present()) {
        SD_CTRL = 0;
        if (engine) SD_CLKDIV = 0;
        card_type = 0;
        stat = STA_NOINIT;
        dbg_printf("sd: slot empty\n");
        return stat;
    }
    for (tries = 0; tries < 3 && !ty; tries++) {        /* the first try after a warm reset can fail */
        SD_CTRL = SDX_SLOW | SDX_CS;                    /* a card left inside CMD25: STOP_TRAN, then let it finish */
        xchg(0xFD);
        wait_ready(250);
        send_cmd(CMD12, 0);                             /* 1.5: still selected - a card left inside a multi-block read
                                                           only hears CMD12 with CS on (1.4 sent it with CS off) */
        wait_ready(250);
        SD_CTRL = SDX_SLOW;
        for (i = 0; i < 10; i++) xchg(0xFF);            /* 80 clocks with CS high */
#ifdef SDTRACE
        {   uint8_t rb[12]; int k;
            deselect(); select_card();
            dbg_printf("sd: CTRL %02X CLKDIV %08lX\n", SD_CTRL, (unsigned long)SD_CLKDIV);
            xchg(0x40); xchg(0); xchg(0); xchg(0); xchg(0); xchg(0x95);
            for (k = 0; k < 12; k++) rb[k] = xchg(0xFF);
            dbg_printf("sd: CMD0 raw %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
                       rb[0], rb[1], rb[2], rb[3], rb[4], rb[5], rb[6], rb[7], rb[8], rb[9], rb[10], rb[11]);
            deselect();
        }
#endif
        /* K60 (11-07): after the STOP_TRAN / CMD12 recovery the card's first CMD0 answer can come 4 bits out of step
           ("F0 4F" instead of "FF 01"); the next CMD0 is clean. Retry CMD0 alone, as the MCP does (5 x, idle clocks). */
        for (i = 0; i < 5; i++) {
            r0 = send_cmd(CMD0, 0);
            if (r0 == 1) break;
            deselect();
            { int k; for (k = 0; k < 64; k++) xchg(0xFF); }
        }
#ifdef SDTRACE
        dbg_printf("sd: try %d CMD0 -> %02X\n", tries, r0);
#endif
        if (r0 == 1) {
            t0 = board_ms();
            r0 = send_cmd(CMD8, 0x1AA);
#ifdef SDTRACE
            dbg_printf("sd: CMD8 -> %02X\n", r0);
#endif
            if (r0 == 1) {                              /* SD v2 */
                for (i = 0; i < 4; i++) ocr[i] = xchg(0xFF);
#ifdef SDTRACE
                dbg_printf("sd: R7 %02X %02X %02X %02X\n", ocr[0], ocr[1], ocr[2], ocr[3]);
#endif
                if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
                    while (board_ms() - t0 < 1000 && (r0 = send_cmd(ACMD41, 1UL << 30))) ;
#ifdef SDTRACE
                    dbg_printf("sd: ACMD41 -> %02X after %lu ms\n", r0, (unsigned long)(board_ms() - t0));
#endif
                    if (board_ms() - t0 < 1000 && send_cmd(CMD58, 0) == 0) {
                        for (i = 0; i < 4; i++) ocr[i] = xchg(0xFF);
                        ty = (ocr[0] & 0x40) ? (CT_SDC2 | CT_BLOCK) : CT_SDC2;
                    }
                }
            } else {                                    /* SD v1 or MMC */
                cmd = (send_cmd(ACMD41, 0) <= 1) ? ACMD41 : CMD1;
                while (board_ms() - t0 < 1000 && send_cmd(cmd, 0)) ;
                if (board_ms() - t0 < 1000 && send_cmd(CMD16, 512) == 0) ty = CT_SDC2;
            }
        }
        deselect();
        if (!ty) delay_ms(100);
    }
    SD_CTRL = 0;                    /* fast clock */
    if (engine) SD_CLKDIV = 0;      /* 12.5 MHz (K25) / 16.7 MHz (K60) after the start-up */
    card_type = ty;
    stat = ty ? 0 : STA_NOINIT;
    dbg_printf("sd: %s after %d tr%s, %s, engine %s\n", ty ? "card ready" : "card does not answer", tries,
               tries == 1 ? "y" : "ies", (ty & CT_BLOCK) ? "block addressing" : "byte addressing",
               engine ? "yes" : "no");
    return stat;
}

DSTATUS disk_status(BYTE pdrv) {
    return pdrv == 1 ? ide_disk_status() : pdrv ? STA_NOINIT : stat;
}

static DRESULT read_once(BYTE * buff, LBA_t sector, UINT count);
static DRESULT write_once(const BYTE * buff, LBA_t sector, UINT count);

/* a failed transfer: re-initialise and try again */
DRESULT disk_read(BYTE pdrv, BYTE * buff, LBA_t sector, UINT count) {
    if (pdrv == 1) return count ? ide_disk_read(buff, sector, count) : RES_PARERR;
    int t;
    DRESULT r = RES_PARERR;
    if (pdrv || !count) return RES_PARERR;
    for (t = 0; t < 3; t++) {
        if (t) { sd_retries++; disk_initialize(0); }
        if ((r = read_once(buff, sector, count)) == RES_OK) break;
    }
    return r;
}

DRESULT disk_write(BYTE pdrv, const BYTE * buff, LBA_t sector, UINT count) {
    if (pdrv == 1) return count ? ide_disk_write(buff, sector, count) : RES_PARERR;
    int t;
    DRESULT r = RES_PARERR;
    if (pdrv || !count) return RES_PARERR;
    for (t = 0; t < 3; t++) {
        if (t) { sd_retries++; disk_initialize(0); }
        if ((r = write_once(buff, sector, count)) == RES_OK) break;
    }
    return r;
}

static DRESULT read_once(BYTE * buff, LBA_t sector, UINT count) {
    uint32_t a = (uint32_t)sector;
    int use_engine = engine && !((uint32_t)buff & 3);
    if (stat & STA_NOINIT) return RES_NOTRDY;
    if (!(card_type & CT_BLOCK)) a *= 512;
    if (count == 1) {
        if (send_cmd(CMD17, a) == 0 && (use_engine ? engine_read(buff, 1) : rx_block(buff, 512))) count = 0;
    } else if (send_cmd(CMD18, a) == 0) {
        if (use_engine) {
            if (engine_read(buff, count)) count = 0;
        } else {
            do {
                if (!rx_block(buff, 512)) break;
                buff += 512;
            } while (--count);
        }
        send_cmd(CMD12, 0);
    }
    deselect();
    return count ? RES_ERROR : RES_OK;
}

static DRESULT write_once(const BYTE * buff, LBA_t sector, UINT count) {
    uint32_t a = (uint32_t)sector;
    if (stat & STA_NOINIT) return RES_NOTRDY;
    if (!(card_type & CT_BLOCK)) a *= 512;
    if (count == 1) {
        if (send_cmd(CMD24, a) == 0 && tx_block(buff, 0xFE)) count = 0;
    } else {
        send_cmd(ACMD23, count);
        if (send_cmd(CMD25, a) == 0) {
            do {
                if (!tx_block(buff, 0xFC)) break;
                buff += 512;
            } while (--count);
            if (!tx_block(0, 0xFD)) count = 1;
        }
    }
    deselect();
    return count ? RES_ERROR : RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void * buff) {
    if (pdrv == 1) return ide_disk_ioctl(cmd, buff);
    if (buff) { }
    if (pdrv) return RES_PARERR;
    if (stat & STA_NOINIT) return RES_NOTRDY;
    if (cmd == CTRL_SYNC) return select_card() ? (deselect(), RES_OK) : RES_ERROR;
    return RES_PARERR;
}
