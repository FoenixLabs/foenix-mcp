/*
 * mcp/types.h - the types a program needs to call the Foenix/MCP (MCP 2.1.0, A2560K)
 *
 * Part of the MCP's Calypsi SDK (sdk/calypsi in the foenix-mcp repository). It replaces the
 * mcp/types.h that comes with Calypsi (contrib/A2560K-SDK), which is older than MCP 2.1.0: its
 * t_sys_info has a different layout and its model and CPU numbers are not the ones the MCP gives.
 *
 * Every structure here must be laid out exactly as the MCP's own (src/include/types.h,
 * src/sys_general.h and the headers in src/dev): the MCP fills them in. tools/check_sdk.py compares them when
 * the MCP is built, so a change on one side without the other stops the build.
 */

#ifndef __MCP_TYPES_H
#define __MCP_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/*=========================================
 * Screen geometry and colours (src/include/types.h)
 *========================================*/

/* The size of a rectangular area */
typedef struct s_extent {
    short width;
    short height;
} t_extent, *p_extent;

/* A point on the screen */
typedef struct s_point {
    short x;
    short y;
} t_point, *p_point;

/* A rectangle on the screen */
typedef struct s_rect {
    t_point origin;
    t_extent size;
} t_rect, *p_rect;

/* A colour (BGR) */
typedef struct s_color3 {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
} t_color3;

/* A colour lookup table entry (BGRA) */
typedef struct s_color4 {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t alpha;
} t_color4;

/*
 * Function types (for device drivers a program registers with sys_chan_register and
 * sys_bdev_register)
 */
typedef void  (*FUNC_V_2_V)();
typedef short (*FUNC_V_2_S)();
typedef short (*FUNC_S_2_S)(char *);
typedef short (*FUNC_BS_2_S)(unsigned char *, short);
typedef short (*FUNC_cBS_2_S)(const unsigned char *, short);
typedef short (*FUNC_B_2_S)(const unsigned short);
typedef short (*FUNC_LBS_2_S)(long, unsigned char *, short);
typedef short (*FUNC_LcBS_2_S)(long, const unsigned char *, short);
typedef short (*FUNC_SBS_2_S)(short, unsigned char *, short);
typedef short (*FUNC_LB_2_S)(long, short);

/*=========================================
 * Channels (src/dev/channel.h)
 *========================================*/

#define CDEV_DEVICES_MAX    8       /* The maximum number of channel devices */
#define CHAN_MAX            16      /* The maximum number of open channels */
#define CHAN_DATA_SIZE      32      /* The number of bytes in a channel's data area */

/* Channel device numbers (for sys_chan_open) */
#define CDEV_CONSOLE        0
#define CDEV_EVID           1
#define CDEV_COM1           2
#define CDEV_COM2           3
#define CDEV_LPT            4
#define CDEV_MIDI           5
#define CDEV_FILE           6

/* Channel status bits (sys_chan_status) */
#define CDEV_STAT_EOF       0x01    /* The channel has reached the end of the data */
#define CDEV_STAT_ERROR     0x02    /* The channel has encountered an error */
#define CDEV_STAT_READABLE  0x04    /* There is data to read (a read will not wait) */
#define CDEV_STAT_WRITABLE  0x08    /* The channel can take data (a write will not wait) */

/* The base of sys_chan_seek */
#define CDEV_SEEK_START     0       /* From the start of the file */
#define CDEV_SEEK_RELATIVE  1       /* From the current position */
#define CDEV_SEEK_END       2       /* From the end of the file */
#define CDEV_SEEK_ABSOLUTE  CDEV_SEEK_START     /* The name older SDKs used */

/* A channel */
typedef struct s_channel {
    short number;
    short dev;
    uint8_t data[CHAN_DATA_SIZE];
} t_channel, *p_channel;

typedef short (*FUNC_CBS_2_S)(p_channel, uint8_t *, short);
typedef short (*FUNC_C_2_S)(p_channel);
typedef short (*FUNC_CcBS_2_S)(p_channel, const uint8_t *, short);
typedef short (*FUNC_CB_2_S)(p_channel, uint8_t);
typedef short (*FUNC_CLS_2_S)(p_channel, long, short);
typedef short (*FUNC_CSBS_2_S)(p_channel, short, uint8_t *, short);

/* A channel device driver (sys_chan_register) */
typedef struct s_dev_chan {
    short number;
    char * name;
    FUNC_V_2_S init;
    FUNC_CcBS_2_S open;
    FUNC_V_2_S close;
    FUNC_CBS_2_S read;
    FUNC_CBS_2_S readline;
    FUNC_C_2_S read_b;
    FUNC_CcBS_2_S write;
    FUNC_CB_2_S write_b;
    FUNC_C_2_S status;
    FUNC_C_2_S flush;
    FUNC_CLS_2_S seek;
    FUNC_CSBS_2_S ioctrl;
} t_dev_chan, *p_dev_chan;

/*=========================================
 * Block devices (src/dev/block.h)
 *========================================*/

#define BDEV_DEVICES_MAX    8

#define BDEV_SDC            0       /* The SD card */
#define BDEV_FDC            1       /* The floppy drive */
#define BDEV_HDC            2       /* The IDE (PATA) drive */

/* A block device driver (sys_bdev_register) */
typedef struct s_dev_block {
    short number;
    char * name;
    FUNC_V_2_S init;
    FUNC_LBS_2_S read;
    FUNC_LcBS_2_S write;
    FUNC_V_2_S status;
    FUNC_V_2_S flush;
    FUNC_SBS_2_S ioctrl;
} t_dev_block, *p_dev_block;

/*=========================================
 * Files (src/dev/fsys.h, src/include/constants.h)
 *========================================*/

#define MAX_PATH_LEN        256
#define DEFAULT_CHUNK_SIZE  256

/* The mode of sys_fsys_open (FatFS's FA_ values; combine with |) */
#ifndef FA_READ
#define FA_READ             0x01    /* Read */
#define FA_WRITE            0x02    /* Write */
#define FA_OPEN_EXISTING    0x00    /* The file must exist */
#define FA_CREATE_NEW       0x04    /* Create it; fails if it exists */
#define FA_CREATE_ALWAYS    0x08    /* Create it; an existing file is emptied */
#define FA_OPEN_ALWAYS      0x10    /* Open it, or create it if it does not exist */
#define FA_OPEN_APPEND      0x30    /* As FA_OPEN_ALWAYS, at the end of the file */
#endif

/* The bits of t_file_info.attributes (FatFS's AM_ values) */
#ifndef AM_RDO
#define AM_RDO              0x01    /* Read only */
#define AM_HID              0x02    /* Hidden */
#define AM_SYS              0x04    /* System */
#define AM_DIR              0x10    /* A folder */
#define AM_ARC              0x20    /* Archive */
#endif

/*
 * A file or folder, as sys_fsys_stat, sys_fsys_readdir, sys_fsys_findfirst and sys_fsys_findnext
 * give it. date and time are in FAT format: date = (year - 1980) << 9 | month << 5 | day,
 * time = hour << 11 | minute << 5 | second / 2.
 */
typedef struct s_file_info {
    long size;
    unsigned short date;
    unsigned short time;
    unsigned char attributes;
    char name[MAX_PATH_LEN];
} t_file_info, *p_file_info;

/* A file loader (sys_fsys_register_loader): short loader(short chan, long destination, long * start) */
typedef short (*p_file_loader)(short chan, long destination, long * start);

/*=========================================
 * The real-time clock (src/dev/rtc.h)
 *========================================*/

typedef struct s_time {
    short year;
    short month;
    short day;
    short hour;
    short minute;
    short second;
    short is_pm;
    short is_24hours;
} t_time, *p_time;

/*=========================================
 * System information (src/sys_general.h)
 *========================================*/

/* t_sys_info.model */
#define MODEL_FOENIX_FMX            0
#define MODEL_FOENIX_C256U          1
#define MODEL_FOENIX_GENX           4
#define MODEL_FOENIX_C256U_PLUS     5
#define MODEL_FOENIX_A2560U_PLUS    6
#define MODEL_FOENIX_A2560X         8
#define MODEL_FOENIX_A2560U         9
#define MODEL_FOENIX_A2560K         11
#define MODEL_FOENIX_A2560M         12

/* t_sys_info.cpu */
#define CPU_WDC65816                0xFF
#define CPU_M68000                  0x00
#define CPU_M68020                  0x01
#define CPU_M68EC020                0x02
#define CPU_M68030                  0x03
#define CPU_M680EC30                0x04
#define CPU_M68040                  0x05
#define CPU_M68040V                 0x06
#define CPU_M680EC40                0x07
#define CPU_486DX2_50               0x08
#define CPU_486DX2_60               0x09
#define CPU_486DX4                  0x0A
#define CPU_M68LC060                0x0B
#define CPU_MC68LC060               0x0B
#define CPU_MC68EC060               0x0C
#define CPU_MC68060                 0x0D

/* The machine, as sys_get_info describes it */
typedef struct s_sys_info {
    uint16_t mcp_version;           /* The MCP's version: 2 for 2.1.0 */
    uint16_t mcp_rev;               /* ... its minor number: 1 for 2.1.0 */
    uint16_t mcp_build;             /* ... its build number: 0 for 2.1.0 */
    uint16_t model;                 /* MODEL_FOENIX_... */
    uint16_t sub_model;             /* 0 = PB, 1 = LB, 2 = CUBE */
    const char * model_name;        /* The model's name, for people */
    uint16_t cpu;                   /* CPU_... */
    const char * cpu_name;          /* The CPU's name, for people */
    uint32_t cpu_clock_khz;         /* The CPU clock in kHz */
    uint32_t fpga_date;             /* The FPGA's date: YYYYMMDD */
    uint32_t fpga_model;            /* The FPGA's model number */
    uint16_t fpga_version;          /* The FPGA's version */
    uint16_t fpga_subver;           /* ... and sub-version */
    uint32_t system_ram_size;       /* Bytes of system RAM */
    bool has_floppy;                /* A floppy drive is there */
    bool has_hard_drive;            /* An IDE (PATA) drive is there */
    bool has_expansion_card;        /* An expansion card is there */
    bool has_ethernet;              /* An Ethernet port is there */
    uint16_t screens;               /* The number of screens */
    uint32_t bus_clock_khz;         /* The bus clock in kHz (the GAVIN timers count it) */
} t_sys_info, *p_sys_info;

/*=========================================
 * SDRAM blocks (src/memory.h), 2.2.0
 *========================================*/

#define MEM_PAGE_SIZE       4096    /* sys_mem_alloc hands out whole pages of this size */
#define MEM_KEEP            0x0001  /* sys_mem_alloc: the block stays after the program ends */
#define MEM_CLEAR           0x0002  /* sys_mem_alloc: fill the block with zeroes */

/* The answer of sys_mem_info */
typedef struct s_mem_info {
    uint32_t sdram_base;            /* First byte of the SDRAM; 0: this machine has none */
    uint32_t sdram_size;            /* Bytes of SDRAM (0: none) */
    uint32_t sdram_free;            /* Bytes of SDRAM not in a block */
    uint32_t sdram_largest;         /* The largest block sys_mem_alloc can give now */
    uint32_t ram_top;               /* The top of the system RAM programs may use (sys_mem_get_ramtop) */
    uint16_t blocks;                /* SDRAM blocks in use */
    uint16_t max_blocks;            /* SDRAM blocks the MCP can keep track of */
} t_mem_info, *p_mem_info;

/*=========================================
 * Text screens (src/dev/txt_screen.h)
 *========================================*/

/* The mode bits of sys_txt_set_mode */
#define TXT_MODE_TEXT       0x0001  /* Text */
#define TXT_MODE_BITMAP     0x0002  /* Bitmap graphics */
#define TXT_MODE_SPRITE     0x0004  /* Sprites */
#define TXT_MODE_TILE       0x0008  /* Tiles */
#define TXT_MODE_SLEEP      0x0010  /* The monitor sleeps (no sync) */

/* What a screen can do (sys_txt_get_capabilities) */
typedef struct s_txt_capabilities {
    short number;                   /* The screen's number */
    short supported_modes;          /* The TXT_MODE_ bits it supports */
    short font_size_count;          /* How many entries font_sizes has */
    p_extent font_sizes;            /* The font sizes it supports (in pixels) */
    short resolution_count;         /* How many entries resolutions has */
    p_extent resolutions;           /* The resolutions it supports (in pixels) */
} t_txt_capabilities, *p_txt_capabilities;

/*=========================================
 * System calls
 *========================================*/

/*
 * The arguments of a system call, as syscall() (in Calypsi's A2560K library) loads them into
 * d1-d6 before its TRAP #15; the function number goes in d0.
 */
typedef struct {
    int32_t arg1;
    int32_t arg2;
    int32_t arg3;
    int32_t arg4;
    int32_t arg5;
    int32_t arg6;
} t_syscall_params, *p_syscall_params;

#endif
