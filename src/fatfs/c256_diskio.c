/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "log.h"
#include "dev/block.h"
#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */
#include "simpleio.h"

/* Definitions of physical drive number for each drive */
#define DEV_SDC		0	/* Example: Map Ramdisk to physical drive 0 */
#define DEV_FDC		1
#define DEV_HDC 	2


/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	DSTATUS stat;
	int result;

	TRACE("disk_status");

	stat = bdev_status(pdrv);
	return stat;
}



/*-----------------------------------------------------------------------*/
/* Initialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	DSTATUS stat;
	int result;

	TRACE("disk_initialize");

	return bdev_init(pdrv);
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	DRESULT res;
	int result;
	int i;

	TRACE("disk_read");

	/* 2.0.15: the SD driver reads several sectors per call (one CMD18 + the block engine): up to 32 at a time */
	if (pdrv == BDEV_SDC) {
		while (count) {
			UINT n = (count > 32) ? 32 : count;
			result = bdev_read(pdrv, sector, buff, (short)(n * 512));
			if (result < 0) {
				log_num(LOG_ERROR, "disk_read error: ", result);
				return (result == ERR_MEDIA_CHANGE) ? RES_NOTRDY : RES_PARERR;
			}
			buff += n * 512; sector += n; count -= n;
		}
		return RES_OK;
	}

	for (i = 0; i < count; i++) {
		result = bdev_read(pdrv, sector, buff + 512 * i, 512);	/* 2.0.14: next sector into the next 512 bytes (as A2560Me MCP 163) */
		if (result < 0) {
			log_num(LOG_ERROR, "disk_read error: ", result);
			if (result == ERR_MEDIA_CHANGE) {
				logmsg(LOG_ERROR, "disk changed.");
				return RES_NOTRDY;
			} else {
				logmsg(LOG_ERROR, "General error");
				return RES_PARERR;
			}
		} else {
			sector++;
		}
	}

	return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	DRESULT res;
	int i;
	int result;

	TRACE("disk_write");

	/* 2.0.15: SD writes of several sectors per call (CMD25 + the block engine) */
	if (pdrv == BDEV_SDC) {
		while (count) {
			UINT n = (count > 32) ? 32 : count;
			result = bdev_write(pdrv, sector, buff, (short)(n * 512));
			if (result < 0) return RES_PARERR;
			buff += n * 512; sector += n; count -= n;
		}
		return RES_OK;
	}

	for (i = 0; i < count; i++) {
		result = bdev_write(pdrv, sector, buff + 512 * i, 512);	/* 2.0.14: next 512 bytes */
		if (result < 0) {
			return RES_PARERR;
		} else {
			sector++;			/* 2.0.14: was "sector += result" (the drivers return 512: wrote sector S, S+512, ...) */
		}
	}

	return RES_OK;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	DRESULT res;
	int result;

	TRACE("disk_ioctl");

	result = bdev_ioctrl(pdrv, cmd, buff, 0);
	if (result < 0) {
		return RES_PARERR;
	} else {
		return RES_OK;
	}
}
