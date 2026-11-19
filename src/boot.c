/**
 * @file boot.c
 *
 * Routines to support the boot process
 */

#include "log_level.h"
#include "cache.h"
#include "version.h"
#ifndef DEFAULT_LOG_LEVEL
    #define DEFAULT_LOG_LEVEL LOG_TRACE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "sys_general.h"
#include "boot.h"
#include "constants.h"
#include "errors.h"
#include "gabe_reg.h"
#include "log.h"
#include "simpleio.h"
#include "syscalls.h"
#include "vicky_general.h"
#include "cli/cli.h"
#include "dev/kbd_mo.h"
#include "dev/txt_screen.h"

#include "rsrc/font/quadrotextFONT.h"
#include "rsrc/bitmaps/image.h" /* Splashscreen */
#include "rsrc/new_bitmaps/Logo_1bpp.h"


extern const unsigned char BM437_IBM_Model3_Alt4[];

#if MODEL == MODEL_FOENIX_A2560U || MODEL == MODEL_FOENIX_A2560U_PLUS
#elif MODEL == MODEL_FOENIX_A2560K
//#include "rsrc/bitmaps/splash_a2560k.h"
#elif MODEL == MODEL_FOENIX_A2560X || MODEL == MODEL_FOENIX_GENX
//#include "rsrc/bitmaps/splash_a2560x.h"
#elif MODEL == MODEL_FOENIX_C256U || MODEL == MODEL_FOENIX_C256U_PLUS || MODEL == MODEL_FOENIX_FMX
#endif

#if MODEL == MODEL_FOENIX_A2560K
    #define SPLASH_WAIT_SEC     10      /* How many seconds to wait on the splash screen */
#else
    #define SPLASH_WAIT_SEC     4      /* How many seconds to wait on the splash screen */
#endif    
/*
 * Important scan codes
 */
#define SC_F1       0x3B
#define SC_F2       0x3C
#define SC_F3       0x3D
#define SC_SPACE    0x39
#define SC_RETURN   0x1C

/* TODO: move this to constants.h */


#define BOOT_SECTOR_BUFFER          ((unsigned char *)0x00004000)
#define BOOT_CODE_MBR_OFF           0x000   /* Offset to the code in the MBR */
#define BOOT_CPUID_MBR_OFF          0x004   /* Offset to the CPUID in the MBR */
#define BOOT_SIG_MBR_OFF            0x006   /* Offset to the boot signature in the MBR */
#define BOOT_SIG                    0xF0E1  /* Foenix/MCP boot signature expected */

#define FDC_VBR_JUMP                0x000   // Intel 80x86 machine language jump... 3 bytes
#define FDC_VBR_OEMNAME             0x003   // OEM Name... 8 bytes
#define FDC_VBR_BPB                 0x00B   // BIOS Parameter Block
#define FDC_VBR_BYTES_PER_SECTOR    0x00B   // Number of bytes per sector... 2 bytes
#define FDC_VBR_SECTORS_PER_CLUSTER 0x00D   // Number of sectors per cluster... 1 byte
#define FDC_VBR_RESERVED_SECTORS    0x00E   // Number of reserved sectors... 2 bytes
#define FDC_VBR_FAT_COUNT           0x010   // Number of file allocation tables... 1 byte
#define FDC_VBR_MAX_ROOT_ENTRIES    0x011   // Maximum number of root directory entries... 2 bytes
#define FDC_VBR_SECTORS             0x013   // Number of logical sectors in FAT12/16... 2 bytes
#define FDC_VBR_MEDIA_TYPE          0x015   // Media type code... 1 byte
#define FDC_VBR_SECTORS_PER_FAT     0x016   // Number of sectors per file allocation table... 2 bytes
#define FDC_VBR_SECTORS_PER_TRACK   0x018   // Number of sectors per track... 2 bytes
#define FDC_VBR_HEADS               0x01A   // Number of heads... 2 bytes
#define FDC_VBR_HIDDEN_SECTORS      0x01C   // Number of hidden sectors... 2 bytes
#define FDC_VBR_TOTAL_SECTORS       0x01E   // Total number of sectors... 2 bytes
#define FDC_VBR_BOOT_CODE           0x060   // Start of boot sector code

#define BOOT_CODE_VBR_OFF   FDC_VBR_BOOT_CODE       /* Offset to the code in the VBR for floppy drives */
#define BOOT_CPUID_VBR_OFF  BOOT_CODE_VBR_OFF+4     /* Offset to the CPUID in the VBR for floppy drives */
#define BOOT_SIG_VBR_OFF    BOOT_CODE_VBR_OFF+6     /* Offset to the boot signature in the VBR for floppy drives */

const char * MCP_INIT_SDC = "/sd/system/mcp.init";  /**< Path to config file on the SD card */
const char * MCP_INIT_FDC = "/fd/system/mcp.init";  /**< Path to config file on the floppy drive */
const char * MCP_INIT_HDC = "/hd/system/mcp.init";  /**< Path to config file on the IDE drive */

// Colors for the A2560K keyboard LED matrix
const unsigned short kbd_colors[] = {0x000F, 0x0FF, 0x00F0, 0x0FF0, 0x0F70, 0x0F00};

/**
 * On the A2560K, animate the LEDs based on the current time while we're waiting for a key press
 *
 * @param max_ticks the value of the jiffy counter when the boot screen will end
 * @param ticks the current value of the jiffy counter
 * @param min_ticks the starting value of the jiffy counter
 */
void boot_animate_keyboard(unsigned long max_ticks, unsigned long ticks, unsigned long min_ticks) {
#if MODEL == MODEL_FOENIX_A2560K
    const int animation_steps = 7;
    int current_step = (int)(((ticks - min_ticks) * animation_steps) / (max_ticks - min_ticks));
    int i;

    for (i = 0; i < current_step; i++) {
        kbdmo_set_led_matrix_row(current_step - i - 1, kbd_colors[5 - i]);
    }
#endif
}

/**
 * Determine if a sector loaded from a device is bootable
 *
 * @param sector pointer to where the sector is stored in memory
 * @param device the number of the block device
 *
 * @return 0 if not bootable, non-zero if bootable
 */
bool is_bootable(unsigned char * sector, short device) {
    switch(device) {
        case BDEV_FDC:
            // The SDC and HDC boot off the MBR...
            // Check for the CPUID and boot signature
            if ((sector[BOOT_CPUID_VBR_OFF] == CPU_M68000) ||
                (sector[BOOT_CPUID_VBR_OFF] == CPU_M68040)) {
                if ((sector[BOOT_SIG_VBR_OFF] == ((BOOT_SIG >> 8) & 0x00FF)) &&
                    (sector[BOOT_SIG_VBR_OFF+1] == (BOOT_SIG & 0x00FF))) {
                    // The CPU is supported, and the boot signature is correct
                    return true;
                }
            }
            break;

        case BDEV_SDC:
        case BDEV_HDC:
            // The SDC and HDC boot off the MBR...
            // Check for the CPUID and boot signature
            if ((sector[BOOT_CPUID_MBR_OFF] == CPU_M68000) ||
                (sector[BOOT_CPUID_MBR_OFF] == CPU_M68040)) {
                if ((sector[BOOT_SIG_MBR_OFF] == ((BOOT_SIG >> 8) & 0x00FF)) &&
                    (sector[BOOT_SIG_MBR_OFF+1] == (BOOT_SIG & 0x00FF))) {
                    // The CPU is supported, and the boot signature is correct
                    return true;
                }
            }
            break;

        default:
            // Otherwise: we're not bootable
            break;
    }

    // If we have reached this point, the sector is not bootable
    return false;
}

/**
 * Run the code in the boot sector
 *
 * @param device the number of the block device for the sector
 */
void boot_sector_run(short device) {
    FUNC_V_2_V boot_sector = 0;

    /* 2.0.29: the sector was read into memory with data writes; empty the instruction cache so the CPU
       runs what is there now, not old cached code */
    cache_flush();

    switch(device) {
        case BDEV_FDC:
            // The FDC boots off the Volume Boot Record (offset 0x060)
            boot_sector = (FUNC_V_2_V)(BOOT_SECTOR_BUFFER + BOOT_CODE_VBR_OFF);
            boot_sector();
            break;

        case BDEV_SDC:
        case BDEV_HDC:
            // The SDC and HDC both boot off the MBR
            boot_sector = (FUNC_V_2_V)(BOOT_SECTOR_BUFFER);
            boot_sector();
            break;

        default:
            break;
    }
}

void make_key_name(const char * original, char * buffer) {
    int x;
    for (x = 0; x < strlen(original); x++) {
        //buffer[x] = 0x80 | original[x];
        buffer[x] = original[x];        // The 8x16 doesn't have inversion
    }
    buffer[strlen(original)] = 0;
}

// New Code to decompress the 1bpp Graphic for the boot Moniker
void decompress_rle_16bit(const uint8_t *source, uint16_t *destination, int32_t data_len) {
    const uint8_t *src_end = source + data_len;
    
    while (source < src_end) {
        // Read the control byte (signed char)
        int8_t control = (int8_t)(*source++);

        if (control > 0) {
            /* POSITIVE: Literal run */
            /* Copy 'control' number of 16-bit words directly */
            int count = (int)control;
            while (count--) {
                // Read 16-bit word (Big-Endian) and move source pointer
                uint16_t word = (*source++) << 8;
                word |= (*source++);
                *destination++ = word;
            }
        } 
        else if (control < 0) {
            /* NEGATIVE: Repeat run */
            /* The next 16-bit word is repeated abs(control) times */
            int count = -((int)control); 
            
            uint16_t word = (*source++) << 8;
            word |= (*source++);
            
            while (count--) {
                *destination++ = word;
            }
        }
        // Note: control == 0 is ignored or could be used as an EOF marker
    }
}

/*
 * Load and display the boot splash screen on the graphics screen
 *
 * @return boot device selected by user
 */
short boot_screen(void) {
    t_rect region;
    short device = BOOT_DEFAULT;
    short screen;
    char buffer[256];
    char entry[50];
    unsigned long target_jiffies = 0;
    unsigned long min_jiffies = 0;
    unsigned long current_jiffies = 0;
    int i = 0, j = 0;
    const unsigned char * pixels;
    volatile unsigned short * vram = (volatile unsigned short *)0x00800000;   // VSRAM BANK B
    unsigned int * VKYII_LGCY_CFG           = (void *) 0xFEC80000;   // Base Address 
    unsigned int * VKYIII_TOSGRAPH_CTRL     = (void *) 0xFEC97000;   // Base Address For Atari Style Bitmap Control
    unsigned int * VSRAM_B                  = (void *) 0x00800000;   // Base Address For Atari Style Bitmap Control   
    t_sys_info info;
    char f1[3], f2[3], f3[3];
    char space[10], cr_text[10];

    TRACE("boot_screen");

    // We'll display boot information on the common screen
    screen = 0;


    /* Set a background color for the bitmap mode */
#if HAS_DUAL_SCREEN
    *BackGroundControlReg_B = 0x00000000;    /* black, like the text background */
    screen = 0;
#else
    *BackGroundControlReg_A = 0x00402040;
    screen = 0;
#endif

    if ( *VKYII_LGCY_CFG & 0x40000000 ) { // Check the HiRes-DipSwitch
            *VKYII_LGCY_CFG = *VKYII_LGCY_CFG | 0x00000100;
    }
    else {
            *VKYII_LGCY_CFG = *VKYII_LGCY_CFG & 0xFFFFFEFF;
    }
    
    /* Set the resolution */   
    txt_set_resolution(screen, 0, 0);                 // The chosen resolution is set by the dipswitch
    /* Display the splashscreen at 640x480 without a border */

    txt_set_border(screen, 0, 0);
    txt_set_font(screen, 8, 16, BM437_IBM_Model3_Alt4);

    region.origin.x = 0;
    region.origin.y = 0;
    region.size.width = 0;
    region.size.height = 0;
    txt_set_region(screen, &region);
    txt_setsizes(screen);
    //txt_set_mode(screen, TXT_MODE_TEXT | TXT_MODE_BITMAP | TXT_MODE_SPRITE);
    //txt_set_mode(screen, TXT_MODE_TEXT );
    VKYIII_TOSGRAPH_CTRL[4] = 0x00000000; // Color 0 
    VKYIII_TOSGRAPH_CTRL[5] = 0x00521A44; // Color 1 

    // So decompress the data stored in Flash and go write it in its proper Buffer
    decompress_rle_16bit(FNXL_Logo1bpp, (uint16_t *)VSRAM_B, FNXL_Logo1bpp_len);

    if ( *VKYII_LGCY_CFG & 0x40000000 ) {
        for (j = 38400; j < 40960; ) {
            for (i = 0; i < 80; i++) {
                VSRAM_B[j + i ] = 0xCCCCCCCC;
            }
            j = j + 80;
            for (i = 0; i < 80; i++) {
                VSRAM_B[j + i] = 0x33333333;
            } 
            j = j + 80;
        }
    }
    else {
        for (j = 35840; j < 38400; ) {
            for (i = 0; i < 80; i++) {
                VSRAM_B[j + i ] = 0xCCCCCCCC;
            }
            j = j + 80;
            for (i = 0; i < 80; i++) {
                VSRAM_B[j + i] = 0x33333333;
            } 
            j = j + 80;
        }
    }

    VKYII_LGCY_CFG[0] = VKYII_LGCY_CFG[0] | 0x00100003; // Turn on TOSGraph and Overlay
    VKYIII_TOSGRAPH_CTRL[0] = 0x01; // 1280xXXX 1bpp    
    /* Disable the cursor, set the color, clear the text screen, and display the text message */
    txt_set_cursor(screen, 0, 0, 0);    // Disable the cursor
    txt_set_color(screen, 15, 0);       // White on transparent
    txt_fill(screen, ' ');              // Clear the screen

    make_key_name("F1", f1);
    make_key_name("F2", f2);
    make_key_name("F3", f3);
    make_key_name("SPACE", space);
    make_key_name("RETURN", cr_text);

#if HAS_FLOPPY
    sprintf(buffer, "BOOT: %s=SD CARD, %s=HARD DRIVE, %s=FLOPPY, %s=DEFAULT, %s=SAFE", f1, f2, f3, space, cr_text);
#else
    sprintf(buffer, "BOOT: %s=SD CARD, %s=HARD DRIVE, %s=DEFAULT, %s=SAFE", f1, f2, space, cr_text);
#endif
    if ( *VKYII_LGCY_CFG & 0x40000000 )
        txt_set_xy(screen, (160 - strlen(buffer)) / 2, 62);
    else 
        txt_set_xy(screen, (160 - strlen(buffer)) / 2, 58);
    sys_chan_write(screen, buffer, strlen(buffer));

    // Get the information about the system
    sys_get_info(&info);

    region.origin.x = 129;
    region.origin.y = 1;
    region.size.width = 40;
    region.size.height = 20;
    txt_set_region(screen, &region);

    sprintf(buffer, "\x1b[HFOENIX/MCP V: %02u.%04u.%04u%s\n", info.mcp_version, info.mcp_rev, info.mcp_build, VER_SUFFIX);
    print(screen, buffer);
    str_upcase(info.model_name, entry);
    sprintf(buffer, "       MODEL: %s\n", entry);
    print(screen, buffer);
    str_upcase(info.cpu_name, entry);
    sprintf(buffer, "         CPU: %s\n", entry);
    print(screen, buffer);
    sprintf(buffer, " CLOCK (KHZ): %lu\n", info.cpu_clock_khz);
    print(screen, buffer);
    sprintf(buffer, "      FPGA V: %x.%02x.%04x\n", (unsigned int)info.fpga_model, info.fpga_version, info.fpga_subver);
    print(screen, buffer);
    /* 2.0.30: what the CPU's cache register really says, not what the MCP set */
    strcpy(entry, cache_state_text());
    str_upcase(entry, entry);
    sprintf(buffer, "   CPU CACHE: %s\n", entry);
    print(screen, buffer);

    /* Wait until the target duration has been reached _or_ the user presses a key */
    sprintf(buffer, "Booting from default device...\n");
    min_jiffies = sys_time_jiffies();
#if MODEL == MODEL_FOENIX_A2560K    
    target_jiffies = min_jiffies + SPLASH_WAIT_SEC * 30;        // <<<<<
#else 
    target_jiffies = min_jiffies + SPLASH_WAIT_SEC * 60;        // <<<<<
#endif
    while (target_jiffies > (current_jiffies = sys_time_jiffies())) {
        boot_animate_keyboard(target_jiffies, current_jiffies, min_jiffies);

        unsigned short scan_code = sys_kbd_scancode();
        if (scan_code == 0)
            continue;

        switch (scan_code) {
            case SC_F1:
                device = BDEV_SDC;
                strcpy(buffer, "Booting from SD card.\n");
                break;

            case SC_F2:
                device = BDEV_HDC;
                strcpy(buffer, "Booting from hard drive.\n");
                break;

#if HAS_FLOPPY
            case SC_F3:
                device = BDEV_FDC;
                strcpy(buffer, "Booting from floppy drive.\n");
                break;
#endif

            case SC_RETURN:
                device = BOOT_SAFE;
                strcpy(buffer, "Booting directly to the command line.\n");
                break;

            default:
                device = BOOT_DEFAULT;
                break;
        }
        break;
    }
    
    /* Initialise all screens */
    txt_init_screen(screen); /* This is the one used for the boot message */
    /* No need to txt_set_resolution(screen, 0, 0) because during screen_init, the defaults are applied */
#if MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_A2560X || MODEL == MODEL_FOENIX_GENX
    txt_set_resolution(0, 0, 0);    // Set the resolution based on the DIP switch
    txt_set_resolution(1, 0, 0);    // Set the resolution based on the DIP switch
#endif
    print(screen, buffer);

#if MODEL == MODEL_FOENIX_A2560K
    // Turn off the keyboard LEDs
    kbdmo_set_led_matrix_fill(0);
#endif
    VKYII_LGCY_CFG[0] = VKYII_LGCY_CFG[0] & 0xFFEFFFFF; // Turn off TOSGraph and Overlay
    VKYIII_TOSGRAPH_CTRL[0] = 0x00; // 1280xXXX 1bpp
    return device;
}

/**
 * Start the boot process after initializing the MCP
 *
 * @param device the number of the block device to use for booting (-1 to go straight to CLI)
 */
void boot_from_bdev(short device) {
    char initial_path[10];
#if MODEL == MODEL_FOENIX_A2560K
    unsigned int boot_dip = 0;        // The setting on the user and boot mode DIP switches
#elif MODEL == MODEL_FOENIX_GENX || MODEL == MODEL_FOENIX_A2560X || MODEL_FOENIX_A2560U_PLUS
    unsigned short boot_dip = 0;        // The setting on the user and boot mode DIP switches
#endif 
    short bootable = 0;                 // Is the boot sector of the selected device bootable?

    TRACE1("boot_from_bdev(%d)", device);

    initial_path[0] = '\0';

    // Get the boot device
    switch (device) {
        case BOOT_DEFAULT:
            // User chose the default. Look at the DIP switches to determine the boot source
            boot_dip = *GABE_DIP_REG & GABE_DIP_BOOT_MASK;
            switch (boot_dip) {
                case 0x0000:
                    // Boot from IDE
                    device = BDEV_HDC;
                    INFO("Boot DIP set for IDE");
                    strcpy(initial_path, "/hd");
                    break;

                case 0x0001:
                    // Boot from SDC
                    device = BDEV_SDC;
                    INFO("Boot DIP set for SDC");
                    strcpy(initial_path, "/sd");
                    break;

#if HAS_FLOPPY
                case 0x0002:
                    // Boot from Floppy
                    device = BDEV_FDC;
                    INFO("Boot DIP set for FDC");
                    strcpy(initial_path, "/fd");
                    break;
#endif
                default:
                    // Boot straight to REPL
                    INFO("Boot DIP set for REPL");
                    strcpy(initial_path, "/sd");
                    device = -1;
                    break;
            }
            break;

        default:
            break;
    }

    if (device >= 0) {
        int i;

        for (i = 0; i < 512; i++) {
            // Zero out the buffer
            BOOT_SECTOR_BUFFER[i] = 0;
        }

        // Try to load the boot sector
        DEBUG("boot_from_bdev: trying to read boot sector");
        short result = bdev_read(device, 0, BOOT_SECTOR_BUFFER, 512);
        if (result > 0) {
            // Check to see if it's bootable
            bootable = is_bootable(BOOT_SECTOR_BUFFER, device);
        }
    }

    if (bootable) {
        DEBUG("boot_from_bdev: boot sector is bootable, trying to run");
        // If bootable, run it
        boot_sector_run(device);

    } else {
        DEBUG("boot_from_bdev: boot sector not bootable");
        // If not bootable...

        // Get the screen for the CLI
        short cli_screen = cli_txt_screen_get();

        if (device >= 0) {
            switch (device) {
                case BDEV_SDC:  strcpy(initial_path, "/sd"); break;
                case BDEV_FDC:  strcpy(initial_path, "/fd"); break;
                case BDEV_HDC:  strcpy(initial_path, "/hd"); break;
                default:        break;
            }

            /* 2.0.33: set up the command line's screen (title bar, banner, colours, folder) BEFORE the
             * start-up file. A program started from mcp.init does not come back here when it ends: the
             * MCP restarts the command line with cli_rerepl(), and the set-up in cli_start_repl() below
             * was skipped (no banner, white text). See cli_prepare_screen(). */
            cli_prepare_screen(cli_screen, initial_path);

            DEBUG("Execute startup file on boot device (if present)");
            switch (device) {
                case BDEV_SDC:
                    if (cli_exec_batch(cli_screen, MCP_INIT_SDC) != 0) {
                        cli_exec_batch(cli_screen, MCP_INIT_HDC);
                    }
                    break;

                case BDEV_FDC:
                    if (cli_exec_batch(cli_screen, MCP_INIT_FDC) != 0) {
                        cli_exec_batch(cli_screen, MCP_INIT_HDC);
                    }
                    break;

                case BDEV_HDC:
                    cli_exec_batch(cli_screen, MCP_INIT_HDC);
                    break;

                default:
                    break;
            }
        }
    INFO("REACHED  cli_start_repl(cli_screen, initial_path) <<<<<<<<<<<<<<<<<<<<<");    
        // Start up the command shell
        cli_start_repl(cli_screen, initial_path);
    }
}

const unsigned char boot_from_file_sector[] = {
    0x60, 0x00, 0x00, 0x06,         //          bra.w boot
    CPU_M68000, 0x00, 0xf0, 0xe1,   //          dc.b CPU_M68000, 0, 0xf0, 0xe1
    0x30, 0x3c, 0x00, 0x40,         // boot:    move.w #$40,d0
    0x43, 0xfa, 0x00, 0x0e,         //          lea (path,pc),a1
    0x22, 0x09,                     //          move.l a1,d1
    0x42, 0x82,                     //          clr.l d2
    0x42, 0x83,                     //          clr.l d3
    0x4e, 0x4f,                     //          trap #15
    0x4e, 0x71,                     // bootloop nop
    0x60, 0xfc                      //          bra bootloop
};

/**
 * Make the indicated drive non booting by erasing the boot information
 *
 * @param device the number of the block device to use for booting (-1 to go straight to CLI)
 * @return 0 on success, any other number is an error
 */
short boot_non_booting(short device) {
    unsigned char * buffer;
    short result = 0;

    buffer = (unsigned char *)malloc(FSYS_SECTOR_SZ);
    if (buffer != 0) {
        // Try to read the current sector
        short n = sys_bdev_read(device, 0, buffer, FSYS_SECTOR_SZ);
        if (n == FSYS_SECTOR_SZ) {
            short sector_offset = 0;

            if (device == BDEV_FDC) {
                // Point to the beginning of the boot code for the FDC (VBR)
                sector_offset = BOOT_CODE_VBR_OFF;

            } else {
                // Point to the beginning of the boot code for the SDC/HDD (MBR)
                sector_offset = BOOT_CODE_MBR_OFF;
            }

            // Boot record read... clear out the boot code
            for (int i = 0; i < sizeof(boot_from_file_sector); i++) {
                buffer[sector_offset + i] = 0;
            }

            // Try to write it back
            n = sys_bdev_write(device, 0, buffer, FSYS_SECTOR_SZ);
            if (n == FSYS_SECTOR_SZ) {
                // Success!
                result = 0;
            } else {
                result = DEV_CANNOT_WRITE;
            }

        } else {
            result = DEV_CANNOT_READ;
        }

    } else {
        result = ERR_OUT_OF_MEMORY;
    }

    // Clear up the memory we grabbed...
    if (buffer) {
        free(buffer);
    }

    return result;
}

/**
 * Make the indicated drive booting from a file
 *
 * @param device the number of the block device to use for booting (-1 to go straight to CLI)
 * @param path the path to the file to boot from
 * @return 0 on success, any other number is an error
 */
short boot_set_file(short device, const char * path) {
    unsigned char * buffer, x;
    short result = 0, i = 0;

    buffer = (unsigned char *)malloc(FSYS_SECTOR_SZ);
    if (buffer != 0) {
        // Try to read the current sector
        bdev_init(device);
        short n = sys_bdev_read(device, 0, buffer, FSYS_SECTOR_SZ);
        if (n == FSYS_SECTOR_SZ) {
            int sector_len = sizeof(boot_from_file_sector);
            int sector_offset = 0;
            int path_len = strlen(path);

            if (device == BDEV_FDC) {
                // Set up the floppy disk boot record
                sector_offset = BOOT_CODE_VBR_OFF;

                // Write 80x86 code to infinite loop at the start of the boot sector
                // This will help maintain compatibility with MS-DOS and Windows machines
                buffer[0] = 0xEB;
                buffer[1] = 0xFF;
                buffer[2] = 0x90;

            } else {
                // Set up the SDC or HDC master boot record
                sector_offset = BOOT_CODE_MBR_OFF;
            }

            // Copy the boot code over
            for (i = 0; i < sector_len; i++) {
                buffer[sector_offset + i] = boot_from_file_sector[i];
            }

            // Insert the path
            for (i = 0; i < path_len; i++) {
                buffer[sector_offset + i + sector_len] = path[i];
            }
            buffer[sector_offset + sector_len + path_len] = 0;

            // Try to write it back
            n = sys_bdev_write(device, 0, buffer, FSYS_SECTOR_SZ);
            if (n == FSYS_SECTOR_SZ) {
                // Success!
                result = 0;
            } else {
                result = DEV_CANNOT_WRITE;
            }

        } else {
            result = DEV_CANNOT_READ;
        }

    } else {
        result = ERR_OUT_OF_MEMORY;
    }

    // Clear up the memory we grabbed...
    if (buffer) {
        free(buffer);
    }

    return result;
}
