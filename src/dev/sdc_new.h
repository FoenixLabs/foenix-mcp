/*-----------------------------------------------------------------------/
/  Low level disk interface modlue include file   (C)ChaN, 2019          /
/-----------------------------------------------------------------------*/

#ifndef _DISKIO_DEFINED
#define _DISKIO_DEFINED

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"

/* Status of Disk Functions */
//typedef unsigned char	DSTATUS;

/* Results of Disk Functions */

typedef enum {
	RES_OK = 0,		/* 0: Successful */
	RES_ERROR,		/* 1: R/W Error */
	RES_WRPRT,		/* 2: Write Protected */
	RES_NOTRDY,		/* 3: Not Ready */
	RES_PARERR		/* 4: Invalid Parameter */
} DRESULT;


/*---------------------------------------*/
/* Prototypes for disk control functions */


//unsigned char disk_initialize (unsigned char pdrv);
//unsigned char disk_status (unsigned char pdrv);
//unsigned char disk_read (unsigned char pdrv, unsigned char *buff, LBA_t sector, unsigned int count);
//unsigned char disk_write (unsigned char pdrv, const unsigned char* buff, LBA_t sector, unsigned int count);
//unsigned char disk_ioctl (unsigned char pdrv, unsigned char cmd, void* buff);



//
// Install the SDC driver
//
extern short sdc_install_n();

//
// Initialize the SDC
//
// Returns:
//  0 on success, any negative number is an error code
//
extern short sdc_init_n();

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
extern short sdc_read_n(long lba, unsigned char * buffer, short size);

//
// Write a block to the SDC
//
// Inputs:
//  lba = the logical block address of the block to write
//  buffer = the buffer containing the data to write
//  size = the size of the buffer.
//
// Returns:
//  number of bytes written, any negative number is an error code
//
extern short sdc_write_n(long lba, const unsigned char * buffer, short size);

//
// Return the status of the SDC
//
// Returns:
//  the status of the device
//
extern short sdc_status_n();

//
// Return any error code of the SDC
//
// Returns:
//  the error code of the device
//
extern short sdc_error_n();

//
// Ensure that any pending writes to teh device have been completed
//
// Returns:
//  0 on success, any negative number is an error code
//
extern short sdc_flush_n();

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
extern short sdc_ioctrl_n(short command, unsigned char * buffer, short size);

/* Disk Status Bits (DSTATUS) */

// FatFS original Naming
#define STA_NOINIT		0x01	/* Drive not initialized */
#define STA_NODISK		0x02	/* No medium in the drive */
#define STA_PROTECT		0x04	/* Write protected */

// Gen X Implementation
#define SDC_SECTOR_SIZE         512         // Size of a block on the SDC
#define SDC_STAT_NOINIT         0x01        // SD has not been initialized
#define SDC_STAT_PRESENT        0x02        // SD is present
#define SDC_STAT_PROTECTED      0x04        // SD is write-protected


/* Command code for disk_ioctrl fucntion */

/* Generic command (Used by FatFs) */
#define CTRL_SYNC			0	/* Complete pending write process (needed at FF_FS_READONLY == 0) */
#define GET_SECTOR_COUNT	1	/* Get media size (needed at FF_USE_MKFS == 1) */
#define GET_SECTOR_SIZE		2	/* Get sector size (needed at FF_MAX_SS != FF_MIN_SS) */
#define GET_BLOCK_SIZE		3	/* Get erase block size (needed at FF_USE_MKFS == 1) */
#define CTRL_TRIM			4	/* Inform device that the data on the block of sectors is no longer used (needed at FF_USE_TRIM == 1) */

/* Generic command (Not used by FatFs) */
#define CTRL_POWER			5	/* Get/Set power status */
#define CTRL_LOCK			6	/* Lock/Unlock media removal */
#define CTRL_EJECT			7	/* Eject media */
#define CTRL_FORMAT			8	/* Create physical format on the media */

/* MMC/SDC specific ioctl command */
#define MMC_GET_TYPE		10	/* Get card type */
#define MMC_GET_CSD			11	/* Get CSD */
#define MMC_GET_CID			12	/* Get CID */
#define MMC_GET_OCR			13	/* Get OCR */
#define MMC_GET_SDSTAT		14	/* Get SD status */
#define ISDIO_READ			55	/* Read data form SD iSDIO register */
#define ISDIO_WRITE			56	/* Write data to SD iSDIO register */
#define ISDIO_MRITE			57	/* Masked write data to SD iSDIO register */

/* ATA/CF specific ioctl command */
#define ATA_GET_REV			20	/* Get F/W revision */
#define ATA_GET_MODEL		21	/* Get model name */
#define ATA_GET_SN			22	/* Get serial number */

#ifdef __cplusplus
}
#endif

#endif
