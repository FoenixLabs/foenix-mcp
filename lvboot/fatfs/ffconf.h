/*---------------------------------------------------------------------------/
/  FatFs configuration for the A2560Me LVGL boot loader (FatFs R0.14b)
/  One volume (the internal SD card), short file names, read and write, no RTC.
/---------------------------------------------------------------------------*/

#define FFCONF_DEF	86631

#define FF_FS_READONLY	0
#define FF_FS_MINIMIZE	1       /* no f_stat, f_getfree, f_unlink, f_mkdir, f_truncate, f_rename */
#define FF_USE_STRFUNC	0
#define FF_USE_FIND		0
#define FF_USE_MKFS		0
#define FF_USE_FASTSEEK	0
#define FF_USE_EXPAND	0
#define FF_USE_CHMOD	0
#define FF_USE_LABEL	0
#define FF_USE_FORWARD	0
#define FF_PRINT_LLI	0
#define FF_PRINT_FLOAT	0
#define FF_STRF_ENCODE	0

#define FF_CODE_PAGE	437
#define FF_USE_LFN		0       /* 8.3 names: BOOT.CFG */
#define FF_MAX_LFN		255
#define FF_LFN_UNICODE	0
#define FF_LFN_BUF		255
#define FF_SFN_BUF		12
#define FF_FS_RPATH		0

#define FF_VOLUMES		2	/* 11-07: "0:" = SD card, "1:" = IDE drive (boot loader) */
#define FF_STR_VOLUME_ID	0
#define FF_VOLUME_STRS		"SD2"
#define FF_MULTI_PARTITION	0

#define FF_MIN_SS		512
#define FF_MAX_SS		512
#define FF_LBA64		0
#define FF_MIN_GPT		0x10000000
#define FF_USE_TRIM		0

#define FF_FS_TINY		0
#define FF_FS_EXFAT		0
#define FF_FS_NORTC		1
#define FF_NORTC_MON	10
#define FF_NORTC_MDAY	14
#define FF_NORTC_YEAR	2026
#define FF_FS_NOFSINFO	0
#define FF_FS_LOCK		0
#define FF_FS_REENTRANT	0
#define FF_FS_TIMEOUT	1000
#define FF_SYNC_t		HANDLE
