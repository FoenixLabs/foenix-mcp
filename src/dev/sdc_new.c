/*------------------------------------------------------------------------/
/  Foolproof MMCv3/SDv1/SDv2 (in SPI mode) control module
/-------------------------------------------------------------------------/
/
/  Copyright (C) 2019, ChaN, all right reserved.
/
/ * This software is a free software and there is NO WARRANTY.
/ * No restriction on use. You can use, modify and redistribute it for
/   personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
/ * Redistributions of source code must retain the above copyright notice.
/
/-------------------------------------------------------------------------/
  Features and Limitations:

  * Easy to Port Bit-banging SPI
    It uses only four GPIO pins. No complex peripheral needs to be used.

  * Platform Independent
    You need to modify only a few macros to control the GPIO port.

  * Low Speed
    The data transfer rate will be several times slower than hardware SPI.

  * No Media Change Detection
    Application program needs to perform a f_mount() after media change.

/-------------------------------------------------------------------------*/

#include <stdio.h>
//#include "ff.h"		/* Obtains integer types for FatFs */
#include "log_level.h"
#ifndef DEFAULT_LOG_LEVEL
    #define DEFAULT_LOG_LEVEL LOG_INFO
#endif

#include "log.h"
#include "constants.h"
#include "errors.h"
#include "gabe_reg.h"
#include "indicators.h"
#include "interrupt.h"
#include "dev/block.h"
#include "sdc_reg.h"
#include "dev/rtc.h"
#include "dev/sdc_new.h"	/* Common include file for FatFs and disk I/O layer */
//#include "dev/sdc.h"
// Constant (4 the GenX/A2560K/A2560X)
#define SDC_TIMEOUT_JF 20           /* Timeout in jiffies (1/60 second) */

/*-------------------------------------------------------------------------*/
/* Platform dependent macros and functions needed to be modified           */
/*-------------------------------------------------------------------------*/
// A2560K  $FEC00518
#if MODEL == MODEL_FOENIX_A2560K
#define SD0_STAT         ((volatile unsigned int *)0xFEC00518)
#define SD0_STAT_CD      0x01000000      /* Is an SD card present? --- 0:Yes, 1:No */
#define SD0_STAT_WP      0x02000000  /* Is the SD card write protected? --- 0:Yes, 1:No */
// Gen X
#elif MODEL == MODEL_FOENIX_A2560X || MODEL == MODEL_FOENIX_GENX
#define SD0_STAT         ((volatile unsigned short *)0xFEC0051A)
#define SD0_STAT_CD      0x0100      /* Is an SD card present? --- 0:Yes, 1:No */
#define SD0_STAT_WP      0x0200      /* Is the SD card write protected? --- 0:Yes, 1:No */
#endif 
/* SPI Controler 0 Registers - External Access (Front of Unit)*/
#define SD0_CTRL    ((volatile unsigned char *)0xFEC00300)
#define SD0_DATA	((volatile unsigned char *)0xFEC00301)
// Flags
#define SDx_CS				0x01		// 1 = Enable 
#define SDx_SLOW 			0x02		// 1 = Slow 400Khz, 0 = 25Mhz
#define SDx_BUSY			0x80		// 1 = Busy

/* 2.0.15: K25 build 1107 G1_SDC block engine (long-word registers next to the byte ones; see G1_SDC.v) */
#define SD0_CLKDIV  ((volatile unsigned long *)0xFEC00304)	/* SCLK half period - 1: 1 = 6.25 MHz, 0 = 12.5 MHz */
#define SD0_BLKCMD  ((volatile unsigned long *)0xFEC00308)	/* N << 16 | token << 8 | op (1 read, 2 write, 3 clear) */
#define SD0_BLKSTAT ((volatile unsigned long *)0xFEC0030C)	/* 0 busy, 1 CRC error, 2 time-out, 3 write rejected */
#define SD0_FIFOCNT ((volatile unsigned long *)0xFEC00310)	/* long words in the FIFO */
#define SD0_FIFO    ((volatile unsigned long *)0xFEC00320)	/* FIFO data port */
static unsigned char g_sdc_blk = 0;		/* 1 = the block engine is there (found by sdc init) */
// 
#define DEV_SD0		0	/* Frontal SDCard - Removable Media 0 - SDCARD */
//
/* MMC card type flags (MMC_GET_TYPE) */
#define CT_MMC3		0x01		/* MMC ver 3 */
#define CT_MMC4		0x02		/* MMC ver 4+ */
#define CT_MMC		0x03		/* MMC */
#define CT_SDC1		0x04		/* SD ver 1 */
#define CT_SDC2		0x08		/* SD ver 2+ */
#define CT_SDC		0x0C		/* SD */
#define CT_BLOCK	0x10		/* Block addressing */

/* Delay n microseconds.
 * 2.0.25 (K60): this was a 10-NOP loop, only right for a 68040 at 25..33 MHz running from flash with the cache off.
 * On the 68LC060 (66 MHz core, RAM, caches) it ran many times too fast, so every SD time-out (card power-up up to 1 s,
 * busy, data token) became a few ms and the card could not be read. Now each step is 5 reads of an uncached GAVIN
 * register: a register read takes a fixed ~7 bus clocks whatever the CPU is (~210 ns at 33 MHz, ~280 ns at 25 MHz),
 * so 5 reads >= 1 us on every Gen1 board, with or without cache. */
static void dly_us (unsigned int n)	{
	volatile unsigned char x;
	do {
		x = *SD0_CTRL; x = *SD0_CTRL; x = *SD0_CTRL; x = *SD0_CTRL; x = *SD0_CTRL;
	} while (--n);
}
/*--------------------------------------------------------------------------

   Module Private Functions

---------------------------------------------------------------------------*/

/* MMC/SD command (SPI mode) */
#define CMD0	(0)			/* GO_IDLE_STATE */
#define CMD1	(1)			/* SEND_OP_COND */
#define	ACMD41	(0x80+41)	/* SEND_OP_COND (SDC) */
#define CMD8	(8)			/* SEND_IF_COND */
#define CMD9	(9)			/* SEND_CSD */
#define CMD10	(10)		/* SEND_CID */
#define CMD12	(12)		/* STOP_TRANSMISSION */
#define CMD13	(13)		/* SEND_STATUS */
#define ACMD13	(0x80+13)	/* SD_STATUS (SDC) */
#define CMD16	(16)		/* SET_BLOCKLEN */
#define CMD17	(17)		/* READ_SINGLE_BLOCK */
#define CMD18	(18)		/* READ_MULTIPLE_BLOCK */
#define CMD23	(23)		/* SET_BLOCK_COUNT */
#define	ACMD23	(0x80+23)	/* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24	(24)		/* WRITE_BLOCK */
#define CMD25	(25)		/* WRITE_MULTIPLE_BLOCK */
#define CMD32	(32)		/* ERASE_ER_BLK_START */
#define CMD33	(33)		/* ERASE_ER_BLK_END */
#define CMD38	(38)		/* ERASE */
#define CMD55	(55)		/* APP_CMD */
#define CMD58	(58)		/* READ_OCR */


static unsigned char SD0_Stat = STA_NOINIT;	/* Disk status */
static unsigned char SD0_CardType;			/* b0:MMC, b1:SDv1, b2:SDv2, b3:Block addressing */

unsigned char g_sdc_status = SDC_STAT_NOINIT;
unsigned char g_sdc_error = 0;



////////////////////////////////////////////////////////////////////////////
/// STEFANY's CODE STARTS HERE
/*-----------------------------------------------------------------------*/
/* Transmit Busy Flag Check - SPI Controler 0                          */
/*-----------------------------------------------------------------------*/
static void SD0_Wait_SDx_Busy( void ) {
	unsigned char i;
	i = (*SD0_CTRL & SDx_BUSY);
	do {
		i = (*SD0_CTRL & SDx_BUSY);
	} while (i == SDx_BUSY);
}

/*-----------------------------------------------------------------------*/
/* Transmit bytes to the card - SPI Controler 0                          */
/*-----------------------------------------------------------------------*/
static void SD0_Tx ( const unsigned char* buff,	unsigned int bc ) {
	unsigned char d;
	do {
		d = *buff++;	/* Get a byte to be sent */
    *SD0_DATA = d; // Set the Data in the Transmit Register
    SD0_Wait_SDx_Busy(); // Wait for the transmit to be over with 
	} while (--bc);
}

/*-----------------------------------------------------------------------*/
/* Receive bytes from the card - SPI Controler 0                         */
/*-----------------------------------------------------------------------*/
static void SD0_Rx ( unsigned char *buff, unsigned int bc	) {
	unsigned char r;
	do {
    	*SD0_DATA = 0xff; // Set the Data in the Transmit Register
    	SD0_Wait_SDx_Busy(); // Wait for the transmit to be over with 
			*buff++ = *SD0_DATA;			/* Store a received byte */
	} while (--bc);
}

/*-----------------------------------------------------------------------*/
/* Wait for card ready Using - SPI Controler 0                           */
/*-----------------------------------------------------------------------*/
static int SD0_wait_ready (void) {
	unsigned char d;
	unsigned int tmr;
	for (tmr = 5000; tmr; tmr--) {	/* Wait for ready in timeout of 500ms */
		SD0_Rx(&d, 1);
		if (d == 0xFF) break;
		dly_us(100);              // 100us
	}
	return tmr ? 1 : 0;
}

/*-----------------------------------------------------------------------*/
/* Deselect the card and release SPI bus - SPI Controler 0               */
/*-----------------------------------------------------------------------*/
static void SD0_deselect ( void ) {
	unsigned char d;
  *SD0_CTRL = *SD0_CTRL & ~ SDx_CS; // SDx_CS = 0 ( Disabled ), SDx = 1 (Active)
	SD0_Rx(&d, 1);	/* Dummy clock (force DO hi-z for multiple slave SPI) */
}

/*-----------------------------------------------------------------------*/
/* Select the card and wait for ready - SPI Controler 0                  */
/*-----------------------------------------------------------------------*/
/* 1:OK, 0:Timeout */
static int SD0_select (void) {
	unsigned char d;

  *SD0_CTRL = *SD0_CTRL | SDx_CS; // SDx_CS = 0 ( Disabled ), SDx = 1 (Active)
	SD0_Rx(&d, 1);	/* Dummy clock (force DO enabled) */
	
  if (SD0_wait_ready()) 
    return 1;	/* Wait for card ready */

	SD0_deselect();
	return 0;			/* Failed */
}

/*-----------------------------------------------------------------------*/
/* Receive a data packet from the card  - SPI Controler 0                */
/*-----------------------------------------------------------------------*/
/* 1:OK, 0:Failed */
static int SD0_Rx_datablock ( unsigned char *buff, unsigned int btr ) {
	unsigned char d[2];
	unsigned int tmr;

	for (tmr = 1000; tmr; tmr--) {	/* Wait for data packet in timeout of 100ms */
		SD0_Rx(d, 1);
		if (d[0] != 0xFF) break;
		dly_us(100);    // 100us
	}

	if (d[0] != 0xFE) 
    return 0;		/* If not valid data token, return with error */

	SD0_Rx(buff, btr);			/* Receive the data block into buffer */
	SD0_Rx(d, 2);					/* Discard CRC */

	return 1;						/* Return with success */
}

/*-----------------------------------------------------------------------*/
/* Send a data packet to the card  - SPI Controler 0                     */
/*-----------------------------------------------------------------------*/
/* 1:OK, 0:Failed */
static int SD0_Tx_datablock (	const unsigned char *buff,	unsigned char token ) {
	unsigned char d[2];

	if (!SD0_wait_ready()) 
    return 0;

	d[0] = token;
	SD0_Tx(d, 1);				/* Xmit a token */

	if (token != 0xFD) {		/* Is it data token? */
		SD0_Tx(buff, 512);	/* Xmit the 512 byte data block to MMC */
		SD0_Rx(d, 2);			/* Xmit dummy CRC (0xFF,0xFF) */
		SD0_Rx(d, 1);			/* Receive data response */
		if ((d[0] & 0x1F) != 0x05)	/* If not accepted, return with error */
			return 0;
	}

	return 1;
}

/*-----------------------------------------------------------------------*/
/* 2.0.15: send n data packets through the G1_SDC block engine            */
/* (token, 512 bytes, CRC16, data response, busy wait - all in hardware). */
/* Returns the number of packets NOT written (0 = all accepted).         */
/*-----------------------------------------------------------------------*/
static unsigned short SD0_blk_write ( const unsigned char *buff, unsigned short n, unsigned char token ) {
	const unsigned long *lp = (const unsigned long *)buff;
	unsigned long pushed = 0, total = (unsigned long)n * 128, st;
	short i;

	if (!SD0_wait_ready())
		return n;
	*SD0_BLKCMD = 3;										/* empty FIFO */
	for (i = 0; i < 128; i++) *SD0_FIFO = *lp++;			/* first packet in, then start */
	pushed = 128;
	*SD0_BLKCMD = ((unsigned long)n << 16) | ((unsigned long)token << 8) | 2;
	while (pushed < total) {
		if (*SD0_FIFOCNT <= 128) {							/* room for the next packet */
			for (i = 0; i < 128; i++) *SD0_FIFO = *lp++;
			pushed += 128;
		} else if (!(*SD0_BLKSTAT & 1)) break;				/* engine stopped (reject / time-out) */
	}
	while (*SD0_BLKSTAT & 1) ;
	st = *SD0_BLKSTAT;
	*SD0_BLKCMD = 3;
	if (st & 0x0C) return n - (unsigned short)(st >> 16) ? n - (unsigned short)(st >> 16) : 1;
	return n - (unsigned short)(st >> 16);
}

/*-----------------------------------------------------------------------*/
/* Send a command packet to the card                                     */
/*-----------------------------------------------------------------------*/
/* Returns command response (bit7==1:Send failed)*/
static unsigned char SD0_Tx_cmd ( unsigned char cmd, unsigned long arg ) {
	unsigned char n, d, buf[6];
	//printf("Processing Command: %d\r", cmd);
	if (cmd & 0x80) {	/* ACMD<n> is the command sequense of CMD55-CMD<n> */
		cmd &= 0x7F;
		n = SD0_Tx_cmd(CMD55, 0);
		if (n > 1)
      return n;
	}

	/* Select the card and wait for ready except to stop multiple block read */
	if (cmd != CMD12) {
		//printf("Processing Command: %d\r", cmd);		
		SD0_deselect();
		if (!SD0_select()) 
      return 0xFF;
	}

	/* Send a command packet */
	buf[0] = 0x40 | cmd;			/* Start + Command index */
	buf[1] = (unsigned char)(arg >> 24);		/* Argument[31..24] */
	buf[2] = (unsigned char)(arg >> 16);		/* Argument[23..16] */
	buf[3] = (unsigned char)(arg >> 8);		/* Argument[15..8] */
	buf[4] = (unsigned char)arg;				/* Argument[7..0] */
	n = 0x01;						/* Dummy CRC + Stop */

	if (cmd == CMD0) 
    n = 0x95;		/* (valid CRC for CMD0(0)) */
	if (cmd == CMD8) 
    n = 0x87;		/* (valid CRC for CMD8(0x1AA)) */
	
  buf[5] = n;
	SD0_Tx(buf, 6);

	/* Receive command response */
	if (cmd == CMD12) 
    SD0_Rx(&d, 1);	/* Skip a stuff byte when stop reading */
	
  n = 10;								/* Wait for a valid response in timeout of 10 attempts */
	do
		SD0_Rx(&d, 1);
	while ((d & 0x80) && --n);

	return d;			/* Return with the response value */
}

/*--------------------------------------------------------------------------

   Public Functions

---------------------------------------------------------------------------*/

//#define SD0_STAT      (*(volatile __far uint8_t *)0xF016A0)
//#define SD0_STAT_CD   0x40  // When 1 = No Card, 0 = Card is Present
//#define SD0_STAT_WP   0x80  // When 1 = Writeable, 0 = Card is Protected
/*-----------------------------------------------------------------------*/
/* Get Disk Status                                                       */
/*-----------------------------------------------------------------------*/
/* Drive number (always 0) */
/*
unsigned char disk_status ( BYTE drv ) {

  if ( drv == DEV_SD0) {
  // CHeck for Card Present
    if ( *SD0_STAT & SD0_STAT_CD )
      g_sdc_status = g_sdc_status | STA_NODISK;
    else 
      g_sdc_status = g_sdc_status & ~STA_NODISK;

    if ( *SD0_STAT & SD0_STAT_WP )
      g_sdc_status = g_sdc_status | STA_PROTECT;
    else 
      g_sdc_status = g_sdc_status & ~STA_PROTECT;
    return g_sdc_status;
  }

  return STA_NOINIT;
}
*/


/*-----------------------------------------------------------------------*/
/* Initialize Disk Drive                                                 */
/*-----------------------------------------------------------------------*/

unsigned char new_disk_initialize (
	unsigned char drv		/* Physical drive nmuber (0) */
)
{
	unsigned char n, ty, cmd, buf[4];
	unsigned int tmr;
	unsigned char s;

	//printf("Init Drive %d\r", drv);	
	*SD0_BLKCMD = 3;		/* 2.0.19: FIRST: abort a G1_SDC block command left running (its BUSY blocks every byte
	                		   transfer, including the dummy clocks below); the old controller ignores this write */
	dly_us(10000);			/* 10ms */
    *SD0_CTRL = *SD0_CTRL | SDx_SLOW;   // Set the SPI in Slow Mode
	  for (n = 10; n; n--) {
	  	SD0_Rx(buf, 1);	/* Apply 80 dummy clocks and the card gets ready to receive command */
	  }
	  ty = 0;
	  /* 2.0.14: a card left in the middle of a multi-block read (CPU restarted by the debug port or a reset button
	     while reading) does not answer CMD0: STOP_TRANSMISSION first (with CS on), let it finish, then CMD0 retries */
	  /* 2.0.17: G1_SDC block engine left running by a restarted program: abort it (the old controller ignores this
	     long write); a card left inside a multi-block WRITE needs the STOP_TRAN token ($FD), then its busy time */
	  *SD0_CTRL = *SD0_CTRL | SDx_CS;
	  buf[0] = 0xFD; SD0_Tx(buf, 1);
	  SD0_wait_ready();
	  SD0_Tx_cmd(CMD12, 0);
	  SD0_wait_ready();
	  SD0_deselect();
	  for (n = 5; n; n--) {
	  	if (SD0_Tx_cmd(CMD0, 0) == 1) break;
	  	SD0_deselect();
	  	for (tmr = 600; tmr; tmr--) SD0_Rx(buf, 1);
	  }
	  if (n) {			/* Enter Idle state (CMD0 answered 1) */

	  	if (SD0_Tx_cmd(CMD8, 0x1AA) == 1) {	/* SDv2? */
	  		SD0_Rx(buf, 4);							/* Get trailing return value of R7 resp */
	  		if (buf[2] == 0x01 && buf[3] == 0xAA) {		/* The card can work at vdd range of 2.7-3.6V */
	  			for (tmr = 1000; tmr; tmr--) {			/* Wait for leaving idle state (ACMD41 with HCS bit) */
	  				if (SD0_Tx_cmd(ACMD41, 1UL << 30) == 0) break;
	  				dly_us(1000);
	  			}
	  			if (tmr && SD0_Tx_cmd(CMD58, 0) == 0) {	/* Check CCS bit in the OCR */
	  				SD0_Rx(buf, 4);
	  				ty = (buf[0] & 0x40) ? CT_SDC2 | CT_BLOCK : CT_SDC2;	/* SDv2+ */
	  			}
	  		}
	  	} else {							/* SDv1 or MMCv3 */
	  		if (SD0_Tx_cmd(ACMD41, 0) <= 1) 	{
	  			ty = CT_SDC2; cmd = ACMD41;	/* SDv1 */
	  		} else {
	  			ty = CT_MMC3; cmd = CMD1;	/* MMCv3 */
	  		}
	  		for (tmr = 1000; tmr; tmr--) {			/* Wait for leaving idle state */
	  			if (SD0_Tx_cmd(cmd, 0) == 0) 
					break;
	  			dly_us(1000);
	  		}
	  		if (!tmr || SD0_Tx_cmd(CMD16, 512) != 0)	/* Set R/W block length to 512 */
	  			ty = 0;
	  	}
	  }
      *SD0_CTRL = *SD0_CTRL & ~SDx_SLOW;   // Bring back the Fast Mode - 25Mhz

	  /* 2.0.15: the G1_SDC block engine? Its CLKDIV register keeps what is written; the old controller answers
	     a long read at +4 with its control byte on all four lanes. Then 12.5 MHz (half period 1 bus clock). */
	  *SD0_CLKDIV = 0x0000000A;
	  g_sdc_blk = (*SD0_CLKDIV == 0x0000000A) ? 1 : 0;
	  if (g_sdc_blk) {
	  	*SD0_CLKDIV = 0;
	  	*SD0_BLKCMD = 3;		/* empty the FIFO, clear the status */
	  }

	  SD0_CardType = ty;
	  s = ty ? 0 : STA_NOINIT;
	  g_sdc_status = s;
	  SD0_deselect();
	
	//DEBUG1("sdc_init: sdtype=0x%x", (unsigned char)ty);
    //log3(LOG_INFO, "cli_exec: '", argv[0], "'");
    //log_num(LOG_INFO, "argc = ", argc);	
		if (s) {
	  	  logmsg(LOG_ERROR, "SDC_INIT <--- S > 0");
		}
		else {
			logmsg(LOG_DEBUG, "SDC_INIT <--- S == 0");
			log_num(LOG_INFO, "SDC_INIT <--- SDCard Type:", (unsigned char)ty);
		}

	  return s;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

unsigned char new_disk_read (unsigned char drv,	unsigned char *buff, unsigned long sector, unsigned short count ) {
	unsigned char cmd;
	unsigned long sect = (unsigned long)sector;

	logmsg(LOG_INFO, "...Entering SDC_DISK_READ");

//	if (disk_status(drv) & STA_NOINIT) 
//    	return RES_NOTRDY;

	if (!(SD0_CardType & CT_BLOCK))
    	sect *= 512;	/* Convert LBA to byte address if needed */

	log_num(LOG_INFO, "SDC_DISK_READ <--- Count = ", count);
	cmd = count > 1 ? CMD18 : CMD17;			/*  READ_MULTIPLE_BLOCK : READ_SINGLE_BLOCK */
	log_num(LOG_INFO, "SDC_DISK_READ <--- Command:", (unsigned char)cmd);

	if (SD0_Tx_cmd(cmd, sect) == 0) {
		do {
	  		if (!SD0_Rx_datablock(buff, 512)) 
          		break;
	  		buff += 512;
	  	} while (--count);
	  	
		if (cmd == CMD18) 
    	SD0_Tx_cmd(CMD12, 0);	/* STOP_TRANSMISSION */
	}
	SD0_deselect();
	log_num(LOG_INFO, "SDC_DISK_READ <--- End Count = ", count);
  return count ? RES_ERROR : RES_OK;      
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/
//DRESULT disk_write (
//	BYTE drv,			/* Physical drive nmuber (0) */
//	const BYTE *buff,	/* Pointer to the data to be written */
//	LBA_t sector,		/* Start sector number (LBA) */
//	UINT count			/* Sector count (1..128) */
//)
unsigned char new_disk_write ( unsigned char drv, const unsigned char *buff, unsigned long sector, unsigned int count ) {
	unsigned long sect = (unsigned long)sector;

	//if (disk_status(drv) & STA_NOINIT) 
	//return RES_NOTRDY;	

	if (!(SD0_CardType & CT_BLOCK)) sect *= 512;	/* Convert LBA to byte address if needed */	
	if (count == 1) {	/* Single block write */
		if ((SD0_Tx_cmd(CMD24, sect) == 0) && SD0_Tx_datablock(buff, 0xFE))
			count = 0;
	}
	else {				/* Multiple block write */
		if (SD0_CardType & CT_SDC) 
	  SD0_Tx_cmd(ACMD23, count);	
		if (SD0_Tx_cmd(CMD25, sect) == 0) {	/* WRITE_MULTIPLE_BLOCK */
			do {
				if (!SD0_Tx_datablock(buff, 0xFC)) 
	      break;
				buff += 512;
			} while (--count);	
			if (!SD0_Tx_datablock(0, 0xFD))	/* STOP_TRAN token */
				count = 1;
		}
	}
	SD0_deselect();	
  
  return count ? RES_ERROR : RES_OK;
}


// Will implement later
/*
DWORD get_fattime (void)
{
    time_t t;
    struct tm *stm;


    t = time(0);
    stm = localtime(&t);

    return (DWORD)(stm->tm_year - 80) << 25 |
           (DWORD)(stm->tm_mon + 1) << 21 |
           (DWORD)stm->tm_mday << 16 |
           (DWORD)stm->tm_hour << 11 |
           (DWORD)stm->tm_min << 5 |
           (DWORD)stm->tm_sec >> 1;
}
*/

/*
 * Handle insertion of an SD card
 */
void sdc_handler() {
    /* Very simple... just flag it as uninitialized */
    g_sdc_status = SDC_STAT_NOINIT;
}
//
// Attempt to reset the SD controller
// Stefany Note, March 18th, 2024:
// Now that it is completely Controled in Software through SPI there is no core to reset
void sdc_reset() {
    short delay;
    TRACE("sdc_reset");
}
//
// Return true if there is an SD card in the slot
//
// When no Card Inserted, the bit ought to be 1
// When a card is present the bit ought to be 0
short sdc_detected() {
    TRACE("sdc_detected()");
	if (( *GABE_SDC_REG & GABE_SDC_PRESENT ) == 0 )
		return (1);
	else 
		return (0);

    //return (*GABE_SDC_REG & GABE_SDC_PRESENT) != GABE_SDC_PRESENT;
}
//
// Return true if there is an SD card is protected
//
short sdc_protected() {
    TRACE("sdc_protected()");
    return (*GABE_SDC_REG & GABE_SDC_WPROT) == GABE_SDC_WPROT;
}
//
// Turn the SDC LED on or off
//
// Inputs:
//  is_on = if 0, turn the LED off, otherwise turn the LED on
//
void sdc_set_led(short is_on) {
	ind_set(IND_SDC, is_on);
}

short sdc_init_n() {
    TRACE("sdc_init");

    // Check for presence of the card
    if (!sdc_detected()) {
        // SDC_DETECTED is active 0... 1 means there is no card
        g_sdc_status = SDC_STAT_NOINIT;
        return DEV_NOMEDIA;
    }
	new_disk_initialize( 0x00 ); // One SDCard in the system, so Device is 0
							// The disk init directly sets the g_sdc_error

    if ( g_sdc_error ) { // if g_sdc_error == 1 - STA_NOINIT
        logmsg(LOG_ERROR, "sdc_init: DEV_CANNOT_INIT");
        return DEV_CANNOT_INIT;
    } 
	else {	// if g_sdc_error == 0 == Success
	    logmsg(LOG_INFO, "sdc_init: SUCCESS");
    	return 0;		
    }
}


//
// Read a block from the SDC
//
// Inputs:
//  lba = the logical block address of the block to read
//  buffer = the buffer into which to copy the block data
//  size = the size of the buffer.
//
// Returns:
//  number of bytes read, any negative number is an error code
//
short sdc_read_n(long lba, unsigned char * buffer, short size) {
	unsigned char status;
	unsigned char cmd;
	unsigned long sect = (unsigned long)lba;
	unsigned short Count = size >> 9;	// Count is the number of packet of 512bytes
	
	/* 2.0.15: without the block engine, one sector per command (CMD18 was slower here: 96 vs 128 KB/s) */
	if (!g_sdc_blk && Count > 1) {
		unsigned short k;
		for (k = 0; k < Count; k++) {
			short r = sdc_read_n(lba + k, buffer + 512 * k, 512);
			if (r < 0) return r;
		}
		return size;
	}

    TRACE3("sdc_read(%ld,%p,%d)", sect, buffer, (int)size);

    // Check for presence of the card
    if (!sdc_detected()) {
        // SDC_DETECTED is active 0... 1 means there is no card
        g_sdc_status = SDC_STAT_NOINIT;
        TRACE("sdc_read: DEV_NOMEDIA");
        return DEV_NOMEDIA;
    }

	logmsg(LOG_DEBUG, "SDC_READ <--- About to Go Read A sector");
    /* Turn on the SDC LED */
    ind_set(IND_SDC, IND_ON);


//unsigned char new_disk_read (unsigned char drv,	unsigned char *buff, unsigned long sector, unsigned int count ) 
	//status = new_disk_read ( 0x00,	buffer, lba, size);
	logmsg(LOG_INFO, "...Entering SDC_DISK_READ");

//	if (disk_status(drv) & STA_NOINIT) 
//    	return RES_NOTRDY;

	if (!(SD0_CardType & CT_BLOCK))
    	sect *= 512;	/* Convert LBA to byte address if needed */

	log_num(LOG_INFO, "SDC_READ <--- Size Before = ", size);
	log_num(LOG_INFO, "SDC_READ <--- Sector Before = ", sect);

	//cmd = size > 1 ? CMD18 : CMD17;			/*  READ_MULTIPLE_BLOCK : READ_SINGLE_BLOCK */
	cmd = Count > 1 ? CMD18 : CMD17;			/*  READ_MULTIPLE_BLOCK : READ_SINGLE_BLOCK */

	log_num(LOG_INFO, "SDC_DISK_READ <--- Command:", (unsigned char)cmd);

	if (SD0_Tx_cmd(cmd, sect) == 0) {
		logmsg(LOG_INFO, "SDC_READ <--- Pass the SD0_Tx_cmd");
		if (g_sdc_blk) {
			/* 2.0.15: the block engine takes the Count data packets (token, 512 bytes, CRC check) into its FIFO;
			   we take 128 long words per sector out of it as soon as they are there */
			unsigned long *lp = (unsigned long *)buffer;
			unsigned long st;
			short i;
			*SD0_BLKCMD = ((unsigned long)Count << 16) | 1;
			while (Count) {
				while (*SD0_FIFOCNT < 128) {
					st = *SD0_BLKSTAT;
					if (!(st & 1) && *SD0_FIFOCNT < 128) break;		/* engine stopped: time-out */
				}
				if (*SD0_FIFOCNT < 128) break;
				for (i = 0; i < 128; i++) *lp++ = *SD0_FIFO;
				Count--;
			}
			while (*SD0_BLKSTAT & 1) ;
			if (*SD0_BLKSTAT & 0x06) Count = 1;				/* CRC error or time-out: report a read error */
			*SD0_BLKCMD = 3;
		} else
		do {
	  		if (!SD0_Rx_datablock(buffer, 512)) 
          		break;
	  		buffer += 512;
	  	} while (--Count);
	  	
		if (cmd == CMD18) 
    	SD0_Tx_cmd(CMD12, 0);	/* STOP_TRANSMISSION */
	}
	SD0_deselect();
	log_num(LOG_INFO, "SDC_DISK_READ <--- End Count = ", size);
  	status = Count ? RES_ERROR : RES_OK;    

	ind_set(IND_SDC, IND_OFF);
// By this point, the indeed has been executed, so we can turn off LED
// possible values for Status
/* 	RES_OK = 0,		0: Successful */
/* 	RES_ERROR,		1: R/W Error */
/* 	RES_WRPRT,		2: Write Protected */
/* 	RES_NOTRDY		3: Not Ready */
/* 	RES_PARERR		4: Invalid Parameter */
	log_num(LOG_INFO, "SDC_READ <--- Status = ", status);
	switch ( status ) {
		case RES_OK:  
        	// Success: return the byte count
			// With the old controller the Count was sent back because it was starting from 0
			// in the new code the Count is actually decremented in the read routine, so I will just report the original Size
            TRACE1("sdc_read: returning %d", size);
            return size;		
		break;

		case RES_ERROR:
            TRACE("sdc_read: DEV_CANNOT_READ");
            return DEV_CANNOT_READ;
		break;

		case RES_NOTRDY:
        	TRACE("sdc_read: DEV_NOMEDIA");		
			return DEV_NOMEDIA; // No specific error for not readyu
		break;

		// Honestly the code ought to never reach that point
		default: 
			return ERR_GENERAL;
		break;
	}
}
//
// Write a block to the SDC
//
// Inputs:
//  lba = the TRACEical block address of the block to write
//  buffer = the buffer containing the data to write
//  size = the size of the buffer.
//
// Returns:
//  number of bytes written, any negative number is an error code
//
short sdc_write_n(long lba, const unsigned char * buffer, short size) {
	unsigned char status;
	unsigned long sect = (unsigned long)lba;
	unsigned short count = size >> 9;	// Count is the number of packet of 512bytes
	/* 2.0.15: without the block engine, one sector per command (as before) */
	if (!g_sdc_blk && count > 1) {
		unsigned short k;
		for (k = 0; k < count; k++) {
			short r = sdc_write_n(lba + k, buffer + 512 * k, 512);
			if (r < 0) return r;
		}
		return size;
	}

    TRACE("sdc_write");

    // Check for presence of the card

    if (!sdc_detected()) {
        // SDC_DETECTED is active 0... 1 means there is no card
        g_sdc_status = SDC_STAT_NOINIT;
        return DEV_NOMEDIA;
    }
	logmsg(LOG_DEBUG, "SDC_WRITE <--- About to Go Write A sector");
    /* Turn on the SDC LED */
    ind_set(IND_SDC, IND_ON);

//	status = new_disk_write ( 0x00, buffer, lba, size );

	//if (disk_status(drv) & STA_NOINIT) 
	//return RES_NOTRDY;	

	if (!(SD0_CardType & CT_BLOCK)) 
		sect *= 512;	/* Convert LBA to byte address if needed */	
	
	if (g_sdc_blk) {	/* 2.0.15: block engine */
		if (count == 1) {
			if (SD0_Tx_cmd(CMD24, sect) == 0) count = SD0_blk_write(buffer, 1, 0xFE);
			if (!SD0_wait_ready()) count = 1;					/* card busy after the last packet */
		} else {
			if (SD0_CardType & CT_SDC) SD0_Tx_cmd(ACMD23, count);
			if (SD0_Tx_cmd(CMD25, sect) == 0) {
				count = SD0_blk_write(buffer, count, 0xFC);
				if (!SD0_Tx_datablock(0, 0xFD)) count = 1;		/* STOP_TRAN token */
			}
		}
	}
	else if (count == 1) {	/* Single block write */
		if ((SD0_Tx_cmd(CMD24, sect) == 0) && SD0_Tx_datablock(buffer, 0xFE))
			count = 0;
	}
	else {				/* Multiple block write */
		if (SD0_CardType & CT_SDC) 
	  SD0_Tx_cmd(ACMD23, count);	
		if (SD0_Tx_cmd(CMD25, sect) == 0) {	/* WRITE_MULTIPLE_BLOCK */
			do {
				if (!SD0_Tx_datablock(buffer, 0xFC)) 
	      break;
				buffer += 512;
			} while (--count);	
			if (!SD0_Tx_datablock(0, 0xFD))	/* STOP_TRAN token */
				count = 1;
		}
	}
	SD0_deselect();	
  
  status = count ? RES_ERROR : RES_OK;


	ind_set(IND_SDC, IND_OFF);

// By this point, the indeed has been executed, so we can turn off LED
// possible values for Status
/* 	RES_OK = 0,		0: Successful */
/* 	RES_ERROR,		1: R/W Error */
/* 	RES_WRPRT,		2: Write Protected */
/* 	RES_NOTRDY		3: Not Ready */
/* 	RES_PARERR		4: Invalid Parameter */

	switch ( status ) {
		case RES_OK:  
        	// Success: return the byte count
			// With the old controller the Count was sent back because it was starting from 0
			// in the new code the Count is actually decremented in the read routine, so I will just report the original Size
            TRACE1("sdc_write: returning %d", size);
            return size;		
		break;

		case RES_ERROR:
            TRACE("sdc_write: DEV_CANNOT_WRITE");
            return DEV_CANNOT_WRITE;
		break;

		case RES_NOTRDY:
        	TRACE("sdc_write: DEV_NOMEDIA");		
			return DEV_NOMEDIA; // No specific error for not readyu
		break;

		// Honestly the code ought to never reach that point
		default: 
			return ERR_GENERAL;
		break;
	}
}
//
// Return the status of the SDC
//
// Returns:
//  the status of the device
//
short sdc_status_n() {
    short status = g_sdc_status;
	logmsg(LOG_DEBUG, "SDC_STATUS");
    TRACE1("sdc_status, status=0x%x",(int)status);

    if (sdc_detected()) {
        // Add the PRESENT flag, if the card is inserted
        status |= SDC_STAT_PRESENT;
    }

    if (sdc_protected()) {
        // Add the PROTECTED flag, if the card is write-protected
        status |= SDC_STAT_PROTECTED;
    }

    TRACE1("sdc_status: %x", (short)status);
    return status;
}
//
// Return any error code of the SDC
//
// Returns:
//  the error code of the device
//
short sdc_error_n() {
    return g_sdc_error;
}

//
// Ensure that any pending writes to teh device have been completed
//
// Returns:
//  0 on success, any negative number is an error code
//
short sdc_flush_n() {
    return 0;           // We don't buffer writes... always return success
}

//
// Return the count of sectors in this SD card
//
short sdc_sector_count() {
    // TODO: implement this!
    return 1000;
}

#define SDC_GET_SECTOR_COUNT 1
#define SDC_GET_SECTOR_SIZE 2
#define SDC_GET_BLOCK_SIZE 3


//
// Issue a control command to the device
//
// Inputs:
//  command = the number of the command to send
//  buffer = pointer to bytes of additional data for the command
//  size = the size of the buffer
//
// Returns:
//  0 on success, any negative number is an error code
//


short sdc_ioctrl_n(short command, unsigned char * buffer, short size) {
	unsigned char res;
	unsigned char n, csd[16];
	unsigned long cs;
    unsigned short *p_word;

//	if (disk_status(0) & STA_NOINIT) 
//		return RES_NOTRDY;	/* Check if card is in the socket */	
	logmsg(LOG_ERROR, "SDC_IOCONTROL <--- About to Pull Information");
	res = RES_ERROR;
	switch (command) {

		// 0
		case CTRL_SYNC :		/* Make sure that no pending write process */
			if (SD0_select()) 
			res = RES_OK;
			break;	

		// 1
		case SDC_GET_SECTOR_COUNT :	/* Get number of sectors on the disk (DWORD) */
			if ((SD0_Tx_cmd(CMD9, 0) == 0) && SD0_Rx_datablock(csd, 16)) {
				if ((csd[0] >> 6) == 1) {	/* SDC ver 2.00 */
					cs = csd[9] + ((unsigned short)csd[8] << 8) + ((unsigned long)(csd[7] & 63) << 16) + 1;
					*(unsigned long*)buffer = cs << 10;
				} 
	    else {					/* SDC ver 1.XX or MMC */
					n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
					cs = (csd[8] >> 6) + ((unsigned short)csd[7] << 2) + ((unsigned short)(csd[6] & 3) << 10) + 1;
					*(unsigned long*)buffer = cs << (n - 9);
				}
				res = RES_OK;
			}
		break;

		case SDC_GET_SECTOR_SIZE:
            // Return the size of a sector... always 512
            p_word = (unsigned short *)buffer;
            *p_word = SDC_SECTOR_SIZE;
		break; 

		// 	Block Size		
		case GET_BLOCK_SIZE :	/* Get erase block size in unit of sector (DWORD) */
			*(unsigned long*)buffer = 128;
			res = RES_OK;
			break;	
		default:
			res = RES_PARERR;
	}
	SD0_deselect();

	return res;
}

//
// Install the SDC driver
//
short sdc_install_n() {
    t_dev_block dev;                    // bdev_register copies the data, so we'll allocate this on the stack

    TRACE("sdc_install");

    /* Install an interrupt handler to catch insertion of a card */
    int_register(INT_SDC_INS, sdc_handler);
    int_enable(INT_SDC_INS);

    //sdc_reset(); - No Need for any reset here... Everything is now SPI and software control, no hardwire controller

    dev.number = BDEV_SDC;
    dev.name = "SDC";
    dev.init = sdc_init_n;
    dev.read = sdc_read_n;
    dev.write = sdc_write_n;
    dev.flush = sdc_flush_n;
    dev.status = sdc_status_n;
    dev.ioctrl = sdc_ioctrl_n;

    return bdev_register(&dev);
}
