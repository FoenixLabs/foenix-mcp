/*
 * ide_g1.c - Gen1 LVGL boot loader: the IDE drive (FatFs volume "1:"), 2026-11-07.
 *
 * Owner 11-07: BOOT.CFG is looked for on the fixed drive first (IDE), then on the SD card, then the flash blocks are
 * listed. This is a small polled PIO driver: LBA28, one sector per command, drive 0 (master).
 *
 * Gen1 facts (same as the MCP's dev/pata.c and EmuTOS):
 *   - registers at $FEC0_0400 (CS0, one register every 2 bytes); device control at $FEC0_041C (CS1 register 6).
 *   - no INTRQ on any Gen1 board (the translator points the wrong way): the drive is told not to drive it (nIEN = 1)
 *     and everything is polled.
 *   - the 16-bit data register puts the drive's first byte at the even address: right for sector data as it is
 *     (FatFs reads the sectors directly), only the 16-bit values of IDENTIFY would be swapped (not used here).
 */
#include <stdint.h>
#include "ff.h"
#include "diskio.h"
#include "hw.h"
#include "board.h"

#define IDE_DATA        REG16(0xFEC00400)
#define IDE_ERROR       REG8(0xFEC00402)
#define IDE_COUNT       REG8(0xFEC00404)
#define IDE_LBA0        REG8(0xFEC00406)
#define IDE_LBA1        REG8(0xFEC00408)
#define IDE_LBA2        REG8(0xFEC0040A)
#define IDE_HEAD        REG8(0xFEC0040C)
#define IDE_CMD         REG8(0xFEC0040E)    /* write: command, read: status */
#define IDE_DEVCTRL     REG8(0xFEC0041C)

#define ST_BSY          0x80
#define ST_DRDY         0x40
#define ST_DF           0x20
#define ST_DRQ          0x08
#define ST_ERR          0x01

#define CMD_READ        0x20
#define CMD_WRITE       0x30
#define CMD_FLUSH       0xE7

static DSTATUS ide_stat = STA_NOINIT;

/* wait until BSY is clear; 0 = time-out (or no drive: the bus floats to $FF / $7F) */
static int wait_not_busy(uint32_t ms) {
    uint32_t t0 = board_ms();
    uint8_t s;
    do {
        s = IDE_CMD;
        if (s == 0xFF || s == 0x7F) return 0;
        if (!(s & ST_BSY)) return 1;
    } while (board_ms() - t0 < ms);
    return 0;
}

/* after a command: BSY clear, then DRQ (data) or ERR / DF; 1 = DRQ */
static int wait_drq(uint32_t ms) {
    uint32_t t0 = board_ms();
    uint8_t s;
    (void)IDE_CMD;                          /* the first status read after a command is the ATA 400 ns gap */
    do {
        s = IDE_CMD;
        if (!(s & ST_BSY)) {
            if (s & (ST_ERR | ST_DF)) return 0;
            if (s & ST_DRQ) return 1;
        }
    } while (board_ms() - t0 < ms);
    return 0;
}

static int set_lba(uint32_t lba) {
    if (!wait_not_busy(1000)) return 0;
    IDE_HEAD = (uint8_t)(0xE0 | ((lba >> 24) & 0x0F));     /* LBA mode, drive 0 */
    if (!wait_not_busy(1000)) return 0;
    IDE_COUNT = 1;
    IDE_LBA0 = (uint8_t)lba;
    IDE_LBA1 = (uint8_t)(lba >> 8);
    IDE_LBA2 = (uint8_t)(lba >> 16);
    return 1;
}

DSTATUS ide_disk_initialize(void) {
    uint8_t buf[512];
    int i;
    uint16_t * w = (uint16_t *)buf;

    ide_stat = STA_NOINIT;
    IDE_DEVCTRL = 0x02;                     /* nIEN: the drive leaves INTRQ alone (Gen1 has no INTRQ input) */
    if (!wait_not_busy(3000)) {             /* a drive that is still spinning up gets 3 s */
        dbg_printf("ide: no drive (status $%02X)\n", IDE_CMD);
        return ide_stat;
    }
    /* proof that a drive answers: read sector 0 (the MBR) */
    if (!set_lba(0)) return ide_stat;
    IDE_CMD = CMD_READ;
    if (!wait_drq(2000)) {
        dbg_printf("ide: sector 0 not readable (status $%02X, error $%02X)\n", IDE_CMD, IDE_ERROR);
        return ide_stat;
    }
    for (i = 0; i < 256; i++) w[i] = IDE_DATA;
    ide_stat = 0;
    dbg_printf("ide: drive ready, MBR signature %02X %02X, partition 1 type $%02X\n", buf[510], buf[511], buf[0x1C2]);
    return ide_stat;
}

DSTATUS ide_disk_status(void) {
    return ide_stat;
}

DRESULT ide_disk_read(BYTE * buff, LBA_t sector, UINT count) {
    uint16_t * w = (uint16_t *)buff;        /* FatFs buffers are word aligned (FF_FS_TINY 0: in FATFS / FIL) */
    int i;
    if (ide_stat & STA_NOINIT) return RES_NOTRDY;
    while (count--) {
        if (!set_lba((uint32_t)sector)) return RES_ERROR;
        IDE_CMD = CMD_READ;
        if (!wait_drq(2000)) return RES_ERROR;
        for (i = 0; i < 256; i++) *w++ = IDE_DATA;
        sector++;
    }
    return RES_OK;
}

DRESULT ide_disk_write(const BYTE * buff, LBA_t sector, UINT count) {
    const uint16_t * w = (const uint16_t *)buff;
    int i;
    if (ide_stat & STA_NOINIT) return RES_NOTRDY;
    while (count--) {
        if (!set_lba((uint32_t)sector)) return RES_ERROR;
        IDE_CMD = CMD_WRITE;
        if (!wait_drq(2000)) return RES_ERROR;
        for (i = 0; i < 256; i++) IDE_DATA = *w++;
        if (!wait_not_busy(2000)) return RES_ERROR;
        if (IDE_CMD & (ST_ERR | ST_DF)) return RES_ERROR;
        sector++;
    }
    return RES_OK;
}

DRESULT ide_disk_ioctl(BYTE cmd, void * buff) {
    if (ide_stat & STA_NOINIT) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC:                         /* the drive's write cache out to the flash / platters */
        if (!wait_not_busy(1000)) return RES_ERROR;
        IDE_CMD = CMD_FLUSH;
        (void)IDE_CMD;
        return wait_not_busy(5000) ? RES_OK : RES_ERROR;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
