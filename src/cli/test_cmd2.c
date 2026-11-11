/*
 * Test commands
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "features.h"
#include "cli.h"
#include "cli/test_cmds.h"
#include "cli/sound_cmds.h"
#include "cli/vge_test.h"
#include "dev/block.h"
#include "dev/channel.h"
#include "dev/fdc.h"
#include "dev/fsys.h"
#include "dev/lpt.h"
#include "dev/rtc.h"
#include "dev/txt_screen.h"
#include "dev/uart.h"
#include "fatfs/ff.h"
#include "interrupt.h"
#include "log.h"
#include "fdc_reg.h"
#include "lpt_reg.h"
#include "dev/ps2.h"
#include "rtc_reg.h"
#include "simpleio.h"
#include "syscalls.h"
#include "sys_general.h"
// TODO: the CLI should use system calls rather than depend on hardware directly
#include "uart_reg.h"
#include "gabe_reg.h"
#include "vicky_general.h"

#if MODEL == MODEL_FOENIX_A2560K
#include "dev/kbd_mo.h"
#endif

#if HAS_DUAL_SCREEN
#include "dev/txt_a2560k_a.h"
#include "dev/txt_a2560k_b.h"
#endif

typedef struct s_cli_test_feature {
    const char * name;
    const char * help;
    cli_cmd_handler handler;
} t_cli_test_feature, * p_cli_test_feature;

/*
 * Tests...
 */

#if MODEL == MODEL_FOENIX_A2560K
/*
 * Return true if the BREAK key has been pressed
 */
short kbd_break() {
    /* Channel 0, CON_IOCTRL_BREAK */
    return sys_chan_ioctrl(0, 5, 0, 0);
}
#endif

/*
 * Test the PS/2 keyboard
 */
short cli_test_ps2(short channel, int argc, const char * argv[]) {
    const char help[] = "Press keys on a PS/2 keyboard... CTRL-C to quit.\n";
    unsigned short scancode = 0;

    sys_chan_write(channel, help, strlen(help));

    do {
        if (scancode != 0) {
            char message[160];
            sprintf(message, "Scan code: %04x\n", scancode);
            sys_chan_write(channel, message, strlen(message));
        }
    } while (
#if MODEL == MODEL_FOENIX_A2560K
        // Not totally sure what this is supposed to test. Doesn't seem related to PS/2 anyway.
        sys_chan_ioctrl(channel, 0x05, 0, 0) == 0
#else
        (scancode = kbd_get_scancode()) || 1
#endif
    );

    return 0;
}

/*
 * Test the joystick ports
 */
short cli_test_joystick(short channel, int argc, const char * argv[]) {
#if MODEL == MODEL_FOENIX_A2560U
    volatile uint16_t * const atari_db9 = (uint16_t *)0xb00500;
    uint16_t current,previous;
    //const char msg[] = "Displays UPLR012 as Up/Down/Left/Right/Button0/Button1/Button2\nPress ESC to exit.\n";
    //sys_chan_write(channel, msg, strlen(msg));

    current = previous = ~*atari_db9;

    // CAVEAT: this tests the hardware, not the MCP Device driver.
    while (sys_kbd_scancode() != 0x01) {
        if ((current = ~*atari_db9) == previous)
            continue;
        else {
            char state[40];
            int i = 0;
            
            if ((current & 0x00ff) != (previous & 0x00ff)) {
                state[i] = '\0';
                strcat(state, "JOY1:");
                i = strlen(state);
                if (current & JOY1_UP) state[i++] = 'U';
                if (current & JOY1_DOWN) state[i++] = 'D';
                if (current & JOY1_LEFT) state[i++] = 'L';
                if (current & JOY1_RIGHT) state[i++] = 'R';
                if (current & JOY1_BTN0) state[i++] = '0';
                if (current & JOY1_BTN1) state[i++] = '1';
                if (current & JOY1_BTN2) state[i++] = '2';
                state[i++] = ' ';
            }

            if ((current & 0xff00) != (previous & 0xff00)) {
                state[i] = '\0';
                strcat(state, "JOY2:");
                i = strlen(state);
                if (current & JOY2_UP) state[i++] = 'U';
                if (current & JOY2_DOWN) state[i++] = 'D';
                if (current & JOY2_LEFT) state[i++] = 'L';
                if (current & JOY2_RIGHT) state[i++] = 'R';
                if (current & JOY2_BTN0) state[i++] = '0';
                if (current & JOY2_BTN1) state[i++] = '1';
                if (current & JOY2_BTN2) state[i++] = '2';
            }

            state[i++] = '\n';
            state[i] = '\0';
            sys_chan_write(channel, state, strlen(state));
            previous = current;
        }
    }
#else
    char message[160];
    volatile unsigned int * joystick_port = (volatile unsigned int *)0xFEC00500;
    volatile unsigned int * game_ctrl_port = (volatile unsigned int *)0xFEC00504;
    unsigned int joy_state = 0, old_joy_state = 0xffffffff;
    unsigned short scancode = 0;

    sprintf(message, "Plug a joystick into either port 0 or port 1... ESC to quit.\n");
    sys_chan_write(channel, message, strlen(message));

    /* Make sure we're in Atari joystick mode */
    *game_ctrl_port = 0;

    do {
        joy_state = *joystick_port;
        if (joy_state != old_joy_state) {
            old_joy_state = joy_state;
            sprintf(message, "Joystick: %08X\n", joy_state);
            sys_chan_write(channel, message, strlen(message));
        }

        scancode = sys_kbd_scancode();
    } while (sys_chan_ioctrl(channel, 5, 0, 0) == 0);
#endif
    return 0;
}

/*
 * Test SNES gamepads
 */
short cli_test_gamepad(short channel, int argc, const char * argv[]) {
    char message[160];
    volatile unsigned int * game_ctrl_port = (volatile unsigned int *)0xFEC00504;
    volatile unsigned int * game_0_0_port = (volatile unsigned int *)0xFEC00508;
    volatile unsigned int * game_0_1_port = (volatile unsigned int *)0xFEC0050C;
    volatile unsigned int * game_1_0_port = (volatile unsigned int *)0xFEC00510;
    volatile unsigned int * game_1_1_port = (volatile unsigned int *)0xFEC00514;
    unsigned int game_ctrl;
    unsigned int game_status;
    unsigned int game_state_0 = 0;
    unsigned int game_state_1 = 0;
    unsigned int old_game_state_0 = 0xffffffff;
    unsigned int old_game_state_1 = 0xffffffff;
    unsigned short scancode = 0;
    unsigned short port = 0;

    if (argc > 1) {
        port = (unsigned short)(cli_eval_number(argv[1]) & 0x0000ffff);
        if (port > 1) {
            port = 1;
        }
    }

    sprintf(message, "Testing SNES gamepad port %d... ESC to quit.\n", (short)port);
    sys_chan_write(channel, message, strlen(message));

    /* Make sure we're in SNES mode */
    if (port == 0) {
        /* Port #0 is SNES */
        game_ctrl = 0x00000005;
    } else {
        /* Port #1 is SNES */
        game_ctrl = 0x0000000A;
    }

    *game_ctrl_port = game_ctrl;

    while (scancode != 0x01) {
        /* Start transferring the data and wait for completion */
        *game_ctrl_port = game_ctrl | 0x00000080;
        do {
            game_status = *game_ctrl_port;
        } while ((game_status & 0x00000040) == 0) ;

        if (port == 0) {
            game_state_0 = *game_0_0_port;
            game_state_1 = *game_0_1_port;
        } else {
            game_state_0 = *game_1_0_port;
            game_state_1 = *game_1_1_port;
        }

        if ((game_state_0 != old_game_state_0) || (game_state_1 != old_game_state_1)) {
            old_game_state_0 = game_state_0;
            old_game_state_1 = game_state_1;
            sprintf(message, "Gamepads: %08X %08X\n", game_state_0, game_state_1);
            sys_chan_write(channel, message, strlen(message));
        }

        scancode = sys_kbd_scancode();
    }

    *game_ctrl_port = 0;
    return 0;
}

//assign Mstr_Ctrl_TOS_Graph_Enable_o				=  VICKY_MASTER_REG[0][20] & !VICKY_MASTER_REG[0][14];	
short tosgraph_enable_test(short channel, int argc, const char * argv[]) {
    unsigned int * VKYII_LGCY_CFG           = (void *) 0xFEC80000;   // Base Address 
    unsigned int * VKYIII_TOSGRAPH_CTRL     = (void *) 0xFEC97000;   // Base Address For Atari Style Bitmap Control
    unsigned int * BufferB                  = (void *) 0x00800000;   // Base Address 

    unsigned int i,j,k;

    VKYII_LGCY_CFG[0] = 0x00100103;     //  Set the Graphics mode in 1280x1024 
    VKYII_LGCY_CFG[1] = 0x00000000;     //  Turn off border
    VKYIII_TOSGRAPH_CTRL[0] = 0x00000001;   // Enable in 1bpp

    for (j = 0; j < 64; j++) {
        k = j * 640;
        for (i = 0; i < 320; i++ ) {    //8 lines
            BufferB[i + k] = 0xFF00FF00;
        }

        for (i = 320; i < 640; i++ ) {  //8 lines
            BufferB[i + k] = 0x00FF00FF;
        }
    }
    return(0);
}

//
short memtext_enable_test(short channel, int argc, const char * argv[]) {
    // New Memory Address for the Memtext system, moved from GAVIN's memory region to VICKY III's Channel B
    unsigned int * VKYII_LGCY_CFG       = (void *) 0xFEC80000;   // Base Address 
    unsigned int * VKYIII_MEMTXT_CTRL   = (void *) 0xFEC98000;   // Base Address For Memtext Control
    unsigned int * VKYIII_MEMTXT_LUT    = (void *) 0xFEC99000;   // Base Address For Memtext Lookup Table
    unsigned int * VKYIII_MEMTXT_FONT   = (void *) 0xFEC9A000;   // Base Address For Memtext FONT
    unsigned int * VKYIII_SDMA_CTRL     = (void *) 0xFEC9C000;   // Base Address For SDMA

    unsigned short * Memtext_Chr_Ptr = (void * ) 0x00400000;
    unsigned short * Memtext_Clr_Ptr = (void * ) 0x0040A000;

    unsigned int x;
    unsigned int y;
    unsigned int a = 0;

    *VKYII_LGCY_CFG = 0x00004003;   // Text Mode Enable but it ids 
    VKYIII_MEMTXT_CTRL[0] = 0x00004103;   // Enable Block 8x8 FONT
    VKYIII_MEMTXT_CTRL[1] = 0x00010001;   // Cursor X:Y
    VKYIII_MEMTXT_CTRL[2] = 0x00400000;   // text Starting Address (160x120x2 = 0x9600)
    VKYIII_MEMTXT_CTRL[3] = 0x0040A000;   // Color Starting Address
    VKYIII_MEMTXT_CTRL[4] = 0xFF404040;   // Cursor X:Y

    // Init the LUT wityh grey scale
    for (y = 0; y < 256; y++) {
        VKYIII_MEMTXT_LUT[y] = 0xFF000000 | ( y << 16)  | (y << 8 ) | (y);
    }
    
/*
    for (y = 0; y < 60; y++) {
        a = y * 160;
        for (x = 0; x < 160; x++) {
            Memtext_Chr_Ptr[x+a] = (x & 0xFFFF) | 0x0200;   // Set 8x16 FONT Set
            Memtext_Clr_Ptr[x+a] = 0x047F;
        }
    }
*/
    return(0);
}


short cli_test_bitmap(short channel, int argc, const char * argv[]) {
    int i,m,p;
    unsigned char j;
    unsigned short k;
    short scan_code;

    // Switch to bitmap mode
    sys_txt_set_mode(0, TXT_MODE_BITMAP);

    *BM0_Control_Reg = 0x00000001;      // Enable BM0
    *BM0_Addy_Pointer_Reg = 0x00000000; //Pointing to Starting of VRAM Bank A
    // *BorderControlReg_L = 0x00000800;   // Enable
    k = 0;
    for (i = 0; i< 256; i++) {
        LUT_0[4*i] = i;
        LUT_0[4*i+1] = i;
        LUT_0[4*i+2] = i;
        LUT_0[4*i+3] = 0;
    }

    for (i = 0; i< 640 * 480; i++) {
        VRAM_Bank0[i] = i & 0xff;
    }

    do {
        scan_code = sys_kbd_scancode();
    } while (scan_code != 0x01);

    // Go back to text mode
    sys_txt_set_mode(0, TXT_MODE_TEXT);

    return 0;
}

/**
 * Test for the serial ports
 *
 * TEST UART [1 | 2]
 */
short cli_test_uart(short channel, int argc, const char * argv[]) {
    char c, c_out;
    short scan_code;
    char buffer[160];
    short cdev = CDEV_COM1;
    short uart = -1;
    short uart_index = 0;
    unsigned long uart_address = 0;

    if (argc > 1) {
        // Get the COM port number
        short port = (short)cli_eval_number(argv[1]);
        if (port <= 1) cdev = CDEV_COM1;
        if (port >= 2) cdev = CDEV_COM2;
    }

    uart_index = cdev - CDEV_COM1;
    uart_address = (unsigned long)uart_get_base(uart_index);

    sprintf(buffer, "Serial port loopback test of COM%d at %p...\n", cdev - CDEV_COM1 + 1, (void*)uart_address);
    print(channel, buffer);

    uart = sys_chan_open(cdev, "9600,8,1,NONE", 0);
    if (uart >= 0) {
        sprintf(buffer, "COM%d: 9600, no parity, 1 stop bit, 8 data bits\nPress ESC to finish.\n", cdev - CDEV_COM1 + 1);
        print(channel, buffer);

        c_out = ' ';
        do {
            sys_chan_write_b(uart, c_out++);
            if (c_out > '}') {
                c_out = ' ';
                sys_chan_write_b(uart, '\r');
                sys_chan_write_b(uart, '\n');
            }

            if (sys_chan_status(uart) & CDEV_STAT_READABLE) {
                c = sys_chan_read_b(uart);
                if (c != 0) {
                    sys_chan_write_b(channel, c);
                }
            }

            scan_code = sys_kbd_scancode();
        } while (scan_code != 0x01);
    } else {
        sprintf(buffer, "Unable to open the serial port: %d\n", uart);
        print(channel, buffer);
    }

    return 0;
}

/**
 * Do a simple test of a kernel panic using division by zero
 */
short cli_test_panic(short channel, int argc, const char * argv[]) {
    volatile int x = 0;
    return argc / x;
}

/*
 * Try using the RTC periodic interrupt in polled mode
 */
short cli_test_rtc(short channel, int argc, const char * argv[]) {
    char buffer[160];
    char * spinner = "|/-\\";
    short count = 0;
    long ticks;

    *RTC_RATES = 0x0e;          /* Periodic interrupt rate: 250 ms */
    *RTC_ENABLES = RTC_PIE;     /* Turn on the periodic interrupt */
    int_enable(INT_RTC);

    ticks = sys_time_jiffies();

    sprintf(buffer, "Waiting for updated ticks starting from %ld\n", ticks);
    sys_chan_write(channel, buffer, strlen(buffer));

    while (sys_kbd_scancode() != 0x01) {
        if (ticks < sys_time_jiffies()) {
            /* We got the periodic interrupt */

            sprintf(buffer, "Tick! %ld\n", ticks);
            sys_chan_write(channel, buffer, strlen(buffer));

            ticks = sys_time_jiffies();
        }
    }

    return 0;
}

/*
 * Test the memory
 * MEM [SYS|MERA]
 */
short cli_mem_test(short channel, int argc, const char * argv[]) {
    volatile unsigned char * memory = 0x00000000;
    t_sys_info sys_info;
    unsigned long mem_start = 0x00010000;
    unsigned long mem_end = sys_mem_get_ramtop();
    char message[160];
    unsigned long i;

#if (MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_GENX || MODEL == MODEL_FOENIX_A2560X)
    if (argc > 1) {
        if ((strcmp(argv[1], "MERA") == 0) || (strcmp(argv[1], "mera") == 0)) {
            mem_start = 0x02000000;
            mem_end = 0x06000000;

            print(channel, "\x1B[H\x1B[2JTesting MERA memory...");
        }
    }
#endif

    sprintf(message, "\x1B[H\x1B[2JTesting memory from %p to %p\n", (void*)mem_start, (void*)mem_end);
    print(channel, message);

    for (i = mem_start; i < mem_end; i++) {
        memory[i] = 0x55; /* Every other bit starting with 1 */
        if (memory[i] != 0x55) {
            sprintf(message, "\x1B[1;2H\x1B[KFailed to write 0x55... read %02X at %p\n\n", memory[i], (void*)i);
            print(channel, message);
            return ERR_GENERAL;
        }

        memory[i] = 0xAA; /* Every other bit starting with 0 */
        if (memory[i] != 0xAA) {
            sprintf(message, "\x1B[1;2H\x1B[KFailed to write 0xAA... read %02X at %p\n\n", memory[i], (void*)i);
            print(channel, message);
            return ERR_GENERAL;
        }

        memory[i] = 0x00;
        if (memory[i] != 0x00) {
            sprintf(message, "\x1B[1;2H\x1B[KFailed to write 0x00... read %02X at %p\n\nX", memory[i], (void*)i);
            print(channel, message);
            return ERR_GENERAL;
        }

        if ((i % 1024) == 0) {
            sprintf(message, "\x1B[1;2H\x1B[0KMemory tested: %p", (void*)i);
            print(channel, message);
        }
    }

    print(channel, "\x1B[1;3H\x1B[0KMemory passed basic tests.\n\n");

    return 0;
}

#if HAS_FLOPPY
short cli_test_recalibrate(short screen, int argc, const char * argv[]) {
    unsigned char buffer[512];
    short i;
    short result;

    bdev_ioctrl(BDEV_FDC, FDC_CTRL_MOTOR_ON, 0, 0);

    sprintf(buffer, "Recalibrating the floppy drive\n");
    sys_chan_write(screen, buffer, strlen(buffer));

    if (fdc_recalibrate() == 0) {
        sprintf(buffer, "Success\n");
        sys_chan_write(screen, buffer, strlen(buffer));
    } else {
        sprintf(buffer, "Failed\n");
        sys_chan_write(screen, buffer, strlen(buffer));
    }

    bdev_ioctrl(BDEV_FDC, FDC_CTRL_MOTOR_OFF, 0, 0);
    return 0;
}

short cli_test_seek(short screen, int argc, const char * argv[]) {
    unsigned char buffer[512];
    short i;
    unsigned char cylinder;
    short result;

    bdev_ioctrl(BDEV_FDC, FDC_CTRL_MOTOR_ON, 0, 0);

    cylinder = (unsigned char)cli_eval_number(argv[1]);

    sprintf(buffer, "Seeking to %d\n", (short)cylinder);
    sys_chan_write(screen, buffer, strlen(buffer));

    if (fdc_seek(cylinder) == 0) {
        sprintf(buffer, "Success\n");
        sys_chan_write(screen, buffer, strlen(buffer));
    } else {
        sprintf(buffer, "Failed\n");
        sys_chan_write(screen, buffer, strlen(buffer));
    }

    bdev_ioctrl(BDEV_FDC, FDC_CTRL_MOTOR_OFF, 0, 0);
    return 0;
}

/*
 * Test the FDC interface by reading the MBR
 *
 * TEST FDC DSKCHG | [<lba> [WRITE <data>]]
 */
short cli_test_fdc(short screen, int argc, const char * argv[]) {
    unsigned char buffer[512];
    char message[160];
    unsigned long lba = 0;
    short i;
    short scancode;
    short n = 0;
    short result;
    short is_write = 0;
    short is_change_test = 0;
    unsigned char data = 0xAA;

    if (argc > 1) {
        if ((strcmp(argv[1], "DSKCHG") == 0) || (strcmp(argv[1], "dskchg") == 0)) {
            is_change_test = 1;
        } else {
            lba = (unsigned long)cli_eval_number(argv[1]);
            if (argc > 2) {
                print(screen, "Will attempt to write before reading...\n");
                is_write = 1;
                data = (unsigned long)cli_eval_number(argv[3]);
            }
        }
    }

    bdev_ioctrl(BDEV_FDC, FDC_CTRL_MOTOR_ON, 0, 0);

    if (is_change_test) {
        // long target_jiffies = sys_time_jiffies() + 240;
        // while (target_jiffies > sys_time_jiffies()) ;

        sprintf(message, "FDC_DIR: %02X\n", *FDC_DIR);
        print(screen, message);

        print(screen, "Recalibrating... ");
        fdc_recalibrate();
        fdc_seek(1);
        print(screen, "done\n");

        sprintf(message, "FDC_DIR: %02X\n", *FDC_DIR);
        print(screen, message);

    } else {
        result = bdev_init(BDEV_FDC);
        if (result != 0) {
            sprintf(buffer, "Could not initialize FDC: %s\n", sys_err_message(result));
            sys_chan_write(screen, buffer, strlen(buffer));
            return result;
        }

        for (i = 0; i < 512; i++) {
            buffer[i] = data;
        }

        if (is_write) {
            n = bdev_write(BDEV_FDC, lba, buffer, 512);
            if (n < 0) {
                dump_buffer(screen, buffer, 512, 1);
                sprintf(message, "Unable to write sector %d: %s\n", lba, err_message(n));
                print(screen, message);
                bdev_ioctrl(BDEV_FDC, FDC_CTRL_MOTOR_OFF, 0, 0);
                return n;
            }
        }

        for (i = 0; i < 512; i++) {
            buffer[i] = 0xAA;
        }

        n = bdev_read(BDEV_FDC, lba, buffer, 512);
        if (n < 0) {
            dump_buffer(screen, buffer, 512, 1);
            sprintf(message, "Unable to read sector %d: %s\n", lba, err_message(n));
            print(screen, message);
            bdev_ioctrl(BDEV_FDC, FDC_CTRL_MOTOR_OFF, 0, 0);
            return n;
        }

        dump_buffer(screen, buffer, 512, 1);

        print(screen, "\n\n");
    }

    bdev_ioctrl(BDEV_FDC, FDC_CTRL_MOTOR_OFF, 0, 0);
}
#endif

/*
 * Test the IDE interface by reading the MBR
 */
short cli_test_ide(short screen, int argc, const char * argv[]) {
    unsigned char buffer[512];
    short i;
    short scancode;
    short n = 0;

    for (;;) {
        n = bdev_read(BDEV_HDC, 0, buffer, 512);
        if (n <= 0) {
            err_print(screen, "Unable to read MBR", n);
            return n;
        }

        dump_buffer(screen, buffer, 512, 1);

        print(screen, "\n\n");

        return 0;

        scancode = sys_kbd_scancode();
        if (scancode == 0x01) {
            break;
        }
    }

    return 0;
}

/*
 * Test file creation
 */
/* 2.0.16 (2026-11-05): LAN9221 tests ported from the A2560Me MCP (same GAVIN byte order: BYTE_TEST long = $21436587) */
#define sys_scale_loops(n) (n)
/*
 * LAN [frames] [16|32]: LAN9221 FIFO test in MAC internal loopback (no cable or network needed).
 * Sends frames of random length (64..1514 bytes, broadcast) through the TX data FIFO and reads them back from
 * the RX FIFOs, checking every byte. 32 = longword accesses (FPGA build 25+, the FIFO-pop fix is build 29+),
 * 16 = word accesses (low half first). Default: 200 frames, both widths.
 * GAVIN gives FIFO bytes in memory order; numeric register values are byte-reversed for longwords (bswap32).
 */
#define LAN_BASE        0xFEC00600UL
#define LAN_L(off)      (*(volatile unsigned long  *)(LAN_BASE + (off)))
#define LAN_W(off)      (*(volatile unsigned short *)(LAN_BASE + (off)))
#define LAN_RX_DATA     0x00
#define LAN_TX_DATA     0x20
#define LAN_RX_STS      0x40
#define LAN_TX_STS      0x48
#define LAN_BYTE_TEST   0x64
#define LAN_RX_CFG      0x6C
#define LAN_TX_CFG      0x70
#define LAN_RX_FIFO_INF 0x7C
#define LAN_TX_FIFO_INF 0x80
#define LAN_PMT_CTRL    0x84
#define LAN_RX_DROP     0xA0
#define LAN_MAC_CMD     0xA4
#define LAN_MAC_DATA    0xA8

static unsigned long lan_bswap32(unsigned long v) {
    return (v >> 24) | ((v >> 8) & 0xFF00UL) | ((v << 8) & 0xFF0000UL) | (v << 24);
}
/* numeric register access (longword: the FPGA does the low half, then the high half) */
static unsigned long lan_rd(unsigned short off) { return lan_bswap32(LAN_L(off)); }
static void lan_wr(unsigned short off, unsigned long v) { LAN_L(off) = lan_bswap32(v); }
/* FIFO data in memory order, at the selected width */
static unsigned long lan_fifo_rd(unsigned short off, short w32) {
    unsigned long hi, lo;
    if (w32) return LAN_L(off);
    hi = LAN_W(off); lo = LAN_W(off + 2);
    return (hi << 16) | lo;
}
static void lan_fifo_wr(unsigned short off, unsigned long v, short w32) {
    if (w32) { LAN_L(off) = v; return; }
    LAN_W(off) = (unsigned short)(v >> 16); LAN_W(off + 2) = (unsigned short)v;
}
static short lan_mac_wait(void) {
    long t;
    for (t = 0; t < 100000; t++) if (!(lan_rd(LAN_MAC_CMD) & 0x80000000UL)) return 0;
    return -1;
}
static short lan_mac_wr(unsigned char idx, unsigned long v) {
    if (lan_mac_wait()) return -1;
    lan_wr(LAN_MAC_DATA, v);
    lan_wr(LAN_MAC_CMD, 0x80000000UL | idx);
    return lan_mac_wait();
}
static short lan_mac_rd(unsigned char idx, unsigned long *v) {
    if (lan_mac_wait()) return -1;
    lan_wr(LAN_MAC_CMD, 0xC0000000UL | idx);
    if (lan_mac_wait()) return -1;
    *v = lan_rd(LAN_MAC_DATA);
    return 0;
}
/* internal PHY (address 1) through MII_ACC (MAC CSR 6) / MII_DATA (MAC CSR 7) */
static short lan_phy_wait(void) {
    long t;
    unsigned long v;
    for (t = 0; t < 10000; t++) { if (lan_mac_rd(6, &v)) return -1; if (!(v & 1)) return 0; }
    return -1;
}
static short lan_phy_wr(unsigned char reg, unsigned short val) {
    if (lan_phy_wait()) return -1;
    if (lan_mac_wr(7, val)) return -1;
    if (lan_mac_wr(6, (1UL << 11) | ((unsigned long)reg << 6) | 2UL | 1UL)) return -1;
    return lan_phy_wait();
}
static short lan_phy_rd(unsigned char reg, unsigned short *val) {
    unsigned long v;
    if (lan_phy_wait()) return -1;
    if (lan_mac_wr(6, (1UL << 11) | ((unsigned long)reg << 6) | 1UL)) return -1;     /* read, PHY address 1 */
    if (lan_phy_wait()) return -1;
    if (lan_mac_rd(7, &v)) return -1;
    *val = (unsigned short)v;
    return 0;
}
static unsigned long lan_seed = 1;
static unsigned char lan_rand8(void) { lan_seed = lan_seed * 1103515245UL + 12345UL; return (unsigned char)(lan_seed >> 16); }

static unsigned char lan_tx_buf[1520];

static short lan_maxlen = 1514;
static unsigned long lan_bitbad[8];   /* bad bytes per wrong bit (data bit of the byte) */
static long lan_run(short channel, short w32, short frames, long *bad_bytes) {
    char message[160];      /* MCP 134: was 100 - the TX status line is longer and overwrote the stack (MCP hang) */
    short f, i, len, words, errs = 0, tx_errs = 0;
    unsigned long sts, t, v, rxlen, inf = 0;
    *bad_bytes = 0;
    /* flush the RX side (RX_DUMP) and the TX status FIFO */
    lan_wr(LAN_RX_CFG, 0x00008000UL);
    for (t = 0; t < 100000 && (lan_rd(LAN_RX_CFG) & 0x00008000UL); t++) ;
    while (lan_rd(LAN_TX_FIFO_INF) & 0x00FF0000UL) (void)lan_rd(LAN_TX_STS);
    for (f = 0; f < frames; f++) {
        len = 64 + (short)((((unsigned short)lan_rand8() << 8) | lan_rand8()) % (lan_maxlen - 64 + 1));
        for (i = 0; i < 6; i++) lan_tx_buf[i] = 0xFF;                       /* broadcast */
        for (i = 6; i < 12; i++) lan_tx_buf[i] = (unsigned char)(0x02 + i - 6);  /* locally administered source */
        lan_tx_buf[12] = 0x88; lan_tx_buf[13] = 0xB5;                         /* local experimental EtherType */
        for (i = 14; i < len; i++) lan_tx_buf[i] = lan_rand8();
        words = (len + 3) / 4;
        for (t = 0; t < 200000 && (lan_rd(LAN_TX_FIFO_INF) & 0xFFFFUL) < (unsigned long)(4 * words + 16); t++) ;
        if (t >= 200000) {
            sprintf(message, "  frame %d: TX FIFO stays full (TX_FIFO_INF %08lX, INT_STS %08lX)\n", f, lan_rd(LAN_TX_FIFO_INF), lan_rd(0x58));
            sys_chan_write(channel, message, strlen(message));
            return -1;
        }
        /* TX command A: first + last segment, buffer size; command B: tag + packet length */
        lan_fifo_wr(LAN_TX_DATA, lan_bswap32(0x00003000UL | (unsigned long)len), w32);
        lan_fifo_wr(LAN_TX_DATA, lan_bswap32(((unsigned long)f << 16) | (unsigned long)len), w32);
        for (i = 0; i < words; i++) {
            v = ((unsigned long)lan_tx_buf[4*i] << 24) | ((unsigned long)lan_tx_buf[4*i+1] << 16) |
                ((unsigned long)lan_tx_buf[4*i+2] << 8) | (unsigned long)lan_tx_buf[4*i+3];
            lan_fifo_wr(LAN_TX_DATA, v, w32);
        }
        /* wait for the RX status */
        for (t = 0; t < 200000; t++) { inf = lan_rd(LAN_RX_FIFO_INF); if (inf & 0x00FF0000UL) break; }
        if (t >= 200000) {
            sprintf(message, "  frame %d (%d bytes): no RX status (RX_FIFO_INF %08lX)\n", f, len, inf);
            sys_chan_write(channel, message, strlen(message));
            return -1;
        }
        sts = lan_bswap32(lan_fifo_rd(LAN_RX_STS, w32));
        rxlen = (sts >> 16) & 0x3FFF;                                         /* includes the 4 CRC bytes */
        if ((sts & 0x8000UL) || rxlen != (unsigned long)len + 4) {
            if (errs < 5) {
                sprintf(message, "  frame %d: RX status %08lX (length %lu, expected %d)\n", f, sts, rxlen, len + 4);
                sys_chan_write(channel, message, strlen(message));
            }
            errs++;
        }
        words = (short)((rxlen + 3) / 4);
        for (i = 0; i < words; i++) {
            short b;
            v = lan_fifo_rd(LAN_RX_DATA, w32);
            for (b = 0; b < 4; b++) {
                short k = 4 * i + b;
                unsigned char got = (unsigned char)(v >> (24 - 8 * b));
                if (k < len && got != lan_tx_buf[k]) {
                    short q;
                    for (q = 0; q < 8; q++) if ((got ^ lan_tx_buf[k]) & (1 << q)) lan_bitbad[q]++;
                    if (*bad_bytes < 5) {
                        sprintf(message, "  frame %d byte %d: %02X, expected %02X\n", f, k, got, lan_tx_buf[k]);
                        sys_chan_write(channel, message, strlen(message));
                    }
                    (*bad_bytes)++;
                }
            }
        }
        /* TX status of this frame: why the transmitter aborted, if it did */
        for (t = 0; t < 100000 && !(lan_rd(LAN_TX_FIFO_INF) & 0x00FF0000UL); t++) ;
        if (lan_rd(LAN_TX_FIFO_INF) & 0x00FF0000UL) {
            unsigned long txs = lan_rd(LAN_TX_STS);
            if ((txs & 0x8000UL) && tx_errs++ < 5) {
                sprintf(message, "  frame %d: TX status %08lX (bit 11 loss of carrier, 10 no carrier, 9 late coll., 8 excess coll., 2 ?, 1 ?)\n", f, txs);
                sys_chan_write(channel, message, strlen(message));
            }
        } else if (tx_errs++ < 5) {
            sprintf(message, "  frame %d: no TX status\n", f);
            sys_chan_write(channel, message, strlen(message));
        }
    }
    return errs;
}

/*
 * LANBUS [count]: LAN9221 local-bus data integrity, separating read errors from write errors.
 *  1. reads of BYTE_TEST (0x87654321; GAVIN longword = 0x21436587) - read path only;
 *  2. random patterns written to FIFO_INT (interrupt levels: plain read / write) and read back twice - a wrong value seen
 *     twice is a write error, a wrong first read followed by a correct second read is a read error.
 * Reports wrong bits per CPU data bit (D31..D0 of the GAVIN longword).
 */
short cli_test_lanbus(short channel, int argc, const char * argv[]) {
    char message[160];
    long n = 100000, i, rd_err = 0, wr_err = 0, rb_err = 0;
    unsigned long bits_rd[32], bits_wr[32], v, w, r1, r2, x;
    short b;

    if (argc > 1) n = cli_eval_number(argv[1]);
    for (b = 0; b < 32; b++) { bits_rd[b] = 0; bits_wr[b] = 0; }

    for (i = 0; i < n; i++) {
        v = LAN_L(LAN_BYTE_TEST);
        if (v != 0x21436587UL) { rd_err++; x = v ^ 0x21436587UL; for (b = 0; b < 32; b++) if (x & (1UL << b)) bits_rd[b]++; }
    }
    sprintf(message, "BYTE_TEST: %ld reads, %ld wrong\n", n, rd_err);
    sys_chan_write(channel, message, strlen(message));

    lan_seed = 777;
    for (i = 0; i < n; i++) {
        /* FIFO_INT numeric bits 15:8 are reserved = GAVIN longword bits 23:16 (byte-reversed): keep them 0 */
        w = (((unsigned long)lan_rand8() << 24) | ((unsigned long)lan_rand8() << 16) | ((unsigned long)lan_rand8() << 8) | lan_rand8()) & 0xFF00FFFFUL;
        LAN_L(0x68) = w;
        r1 = LAN_L(0x68) & 0xFF00FFFFUL;
        r2 = LAN_L(0x68) & 0xFF00FFFFUL;
        if (r1 != w || r2 != w) {
            if (r1 == r2) { wr_err++; x = r1 ^ w; for (b = 0; b < 32; b++) if (x & (1UL << b)) bits_wr[b]++; }
            else { rb_err++; x = (r1 != w) ? (r1 ^ w) : (r2 ^ w); for (b = 0; b < 32; b++) if (x & (1UL << b)) bits_rd[b]++; }
            if (wr_err + rb_err <= 8) {
                sprintf(message, "  wrote %08lX, read %08lX then %08lX\n", w, r1, r2);
                sys_chan_write(channel, message, strlen(message));
            }
        }
    }
    LAN_L(0x68) = lan_bswap32(0x48000000UL);                                    /* FIFO_INT default */
    sprintf(message, "FIFO_INT: %ld write/read pairs, %ld write errors, %ld read errors\n", n, wr_err, rb_err);
    sys_chan_write(channel, message, strlen(message));
    sprintf(message, "  wrong bits (CPU D31..D0) read:");
    sys_chan_write(channel, message, strlen(message));
    for (b = 31; b >= 0; b--) { sprintf(message, " %lu", bits_rd[b]); sys_chan_write(channel, message, strlen(message)); }
    sprintf(message, "\n  wrong bits (CPU D31..D0) write:");
    sys_chan_write(channel, message, strlen(message));
    for (b = 31; b >= 0; b--) { sprintf(message, " %lu", bits_wr[b]); sys_chan_write(channel, message, strlen(message)); }
    sys_chan_write(channel, "\n", 1);
    return (rd_err + wr_err + rb_err) ? -1 : 0;
}

short cli_test_lan(short channel, int argc, const char * argv[]) {
    char message[160];
    unsigned short bmsr = 0;
    short frames = 200, width = 0, w;
    unsigned long bt, t;
    unsigned short b0, b1;
    long errs, bad, total = 0;

    if (argc > 1) frames = (short)cli_eval_number(argv[1]);
    if (argc > 2) width = (short)cli_eval_number(argv[2]);
    lan_maxlen = 1514;
    if (argc > 3) lan_maxlen = (short)cli_eval_number(argv[3]);
    if (lan_maxlen < 64) lan_maxlen = 64;
    if (lan_maxlen > 1514) lan_maxlen = 1514;
    if (frames < 1) frames = 1;

    b0 = LAN_W(LAN_BYTE_TEST); b1 = LAN_W(LAN_BYTE_TEST + 2); bt = LAN_L(LAN_BYTE_TEST);
    sprintf(message, "BYTE_TEST: words %04X %04X (expect 2143 6587), longword %08lX (expect 21436587)\n", b0, b1, bt);
    sys_chan_write(channel, message, strlen(message));
    if (bt != 0x21436587UL) { sprintf(message, "LAN9221 not answering correctly - stopped\n"); sys_chan_write(channel, message, strlen(message)); return -1; }
    for (t = 0; t < 100000 && !(lan_rd(LAN_PMT_CTRL) & 1); t++) ;
    if (!(lan_rd(LAN_PMT_CTRL) & 1)) { sprintf(message, "LAN9221 not READY - stopped\n"); sys_chan_write(channel, message, strlen(message)); return -1; }

    /* MCP 134: a network cable with a link partner upsets the PHY loopback (frames cut at 136 bytes, loss of carrier):
       warn when the PHY has a link (basic status bit 2, read twice: the bit latches low) */
    if (!lan_phy_rd(1, &bmsr) && !lan_phy_rd(1, &bmsr) && (bmsr & 0x0004)) {
        sprintf(message, "Note: network link is up - the PHY loopback still works (2.0.20: store and forward fixed the old 136-byte cut)\n");
        sys_chan_write(channel, message, strlen(message));
    }
    /* PHY loopback at a fixed 100 Mbit full duplex (auto-negotiation off), so the MII clocks are stable without a
       cable; MAC: full duplex + promiscuous, TX + RX on. (MAC-internal loopback still runs on the PHY's MII clocks,
       which change speed while the PHY auto-negotiates: frames were cut after 136 bytes.) */
    if (lan_mac_wr(1, 0) || lan_phy_wr(0, 0x6100)) {
        sprintf(message, "MAC / PHY access failed - stopped\n"); sys_chan_write(channel, message, strlen(message)); return -1;
    }
    for (t = 0; t < sys_scale_loops(300000); t++) ;                         /* let the PHY settle (a few ms) */
    lan_wr(0x58, 0xFFFFFFFFUL);                                               /* clear INT_STS */
    if (lan_mac_wr(1, (1UL << 20) | (1UL << 18) | (1UL << 3) | (1UL << 2))) {
        sprintf(message, "MAC CSR busy - stopped\n"); sys_chan_write(channel, message, strlen(message)); return -1;
    }
    lan_wr(0x74, lan_rd(0x74) | 0x00100000UL);                                /* 2.0.20: HW_CFG.SF, store and forward */
    lan_wr(LAN_TX_CFG, 0x0000C000UL);                                         /* TX status + data dump */
    for (t = 0; t < 100000 && (lan_rd(LAN_TX_CFG) & 0x0000C000UL); t++) ;
    lan_wr(LAN_TX_CFG, 0x00000002UL);                                         /* TX_ON */

    for (w = 0; w < 2; w++) {
        short w32 = (w == 0) ? 1 : 0;
        if (width == 32 && !w32) continue;
        if (width == 16 && w32) continue;
        lan_seed = 12345 + w;
        { short q; for (q = 0; q < 8; q++) lan_bitbad[q] = 0; }
        errs = lan_run(channel, w32, frames, &bad);
        if (bad) {
            short q;
            sprintf(message, "  wrong bits per data bit 0..7:");
            sys_chan_write(channel, message, strlen(message));
            for (q = 0; q < 8; q++) { sprintf(message, " %lu", lan_bitbad[q]); sys_chan_write(channel, message, strlen(message)); }
            sys_chan_write(channel, "\n", 1);
        }
        sprintf(message, "%s accesses: %d frames, %ld bad RX status, %ld bad bytes, RX_DROP %lu, INT_STS %08lX -> %s\n",
                w32 ? "32-bit" : "16-bit", frames, errs < 0 ? 0L : errs, bad, lan_rd(LAN_RX_DROP), lan_rd(0x58),
                (errs == 0 && bad == 0) ? "PASS" : "FAIL");
        lan_wr(0x58, 0xFFFFFFFFUL);
        sys_chan_write(channel, message, strlen(message));
        total += (errs < 0 ? 1 : errs) + bad;
    }
    lan_mac_wr(1, 0);                                                        /* MAC off again */
    lan_phy_wr(0, 0x3300);                                                   /* PHY: auto-negotiation on, restart */
    lan_wr(LAN_TX_CFG, 0);
    return total ? -1 : 0;
}


/*
 * 2.0.20 (2026-11-06): LANNET [seconds] [ip] [peer ip] - the LAN9221 on a real network (cable plugged in).
 *  - waits for the PHY link and prints speed / duplex (auto-negotiation);
 *  - MAC address 02:46:4E:58:4B:25 (locally administered), IP default 192.168.0.245;
 *  - sends an ARP request for the peer (default: x.x.x.1 of the IP) once a second until it answers;
 *  - answers ARP requests and pings (ICMP echo) for the IP, so the PC can "ping -l 1472 <ip>" (full 1514-byte frames);
 *  - counts every received frame (good / bad status, broadcast / multicast / unicast, longest) for the run time.
 */
static unsigned char lan_rx_buf[1536];
static const unsigned char lan_my_mac[6] = { 0x02, 0x46, 0x4E, 0x58, 0x4B, 0x25 };

static short lan_parse_ip(const char * s, unsigned char * ip) {
    short i, n;
    for (i = 0; i < 4; i++) {
        n = 0;
        if (*s < '0' || *s > '9') return -1;
        while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; }
        if (n > 255) return -1;
        ip[i] = (unsigned char)n;
        if (i < 3) { if (*s != '.') return -1; s++; }
    }
    return *s ? -1 : 0;
}

static long lan_tx_bad = 0;
static unsigned long lan_tx_last_bad = 0;
/* send one frame from buf (len bytes, padded to 60), 32-bit FIFO accesses; returns 0, or -1 when the TX FIFO stays full */
static short lan_send(const unsigned char * buf, short len) {
    unsigned long t, v;
    short i, words;
    static unsigned short tag = 0;
    if (len < 60) len = 60;
    words = (len + 3) / 4;
    for (t = 0; t < 200000 && (lan_rd(LAN_TX_FIFO_INF) & 0xFFFFUL) < (unsigned long)(4 * words + 16); t++) ;
    if (t >= 200000) return -1;
    while (lan_rd(LAN_TX_FIFO_INF) & 0x00FF0000UL) {                         /* old TX status words */
        unsigned long ts = lan_rd(LAN_TX_STS);
        if (ts & 0x8000UL) { lan_tx_bad++; lan_tx_last_bad = ts; }
    }
    lan_fifo_wr(LAN_TX_DATA, lan_bswap32(0x00003000UL | (unsigned long)len), 1);
    lan_fifo_wr(LAN_TX_DATA, lan_bswap32(((unsigned long)(tag++) << 16) | (unsigned long)len), 1);
    for (i = 0; i < words; i++) {
        v = ((unsigned long)buf[4*i] << 24) | ((unsigned long)buf[4*i+1] << 16) | ((unsigned long)buf[4*i+2] << 8) | (unsigned long)buf[4*i+3];
        lan_fifo_wr(LAN_TX_DATA, v, 1);
    }
    return 0;
}

static unsigned short lan_csum(const unsigned char * p, short n) {
    unsigned long s = 0;
    short i;
    for (i = 0; i + 1 < n; i += 2) s += ((unsigned long)p[i] << 8) | p[i + 1];
    if (n & 1) s += (unsigned long)p[n - 1] << 8;
    while (s >> 16) s = (s & 0xFFFFUL) + (s >> 16);
    return (unsigned short)~s;
}

static void lan_arp(unsigned char * f, short op, const unsigned char * tha, const unsigned char * my_ip, const unsigned char * tpa) {
    short i;
    for (i = 0; i < 60; i++) f[i] = 0;
    for (i = 0; i < 6; i++) {
        f[i] = (op == 1) ? 0xFF : tha[i];
        f[6 + i] = lan_my_mac[i];
        f[22 + i] = lan_my_mac[i];
        f[32 + i] = (op == 1) ? 0 : tha[i];
    }
    f[12] = 0x08; f[13] = 0x06; f[14] = 0x00; f[15] = 0x01; f[16] = 0x08; f[17] = 0x00; f[18] = 6; f[19] = 4;
    f[20] = 0x00; f[21] = (unsigned char)op;
    for (i = 0; i < 4; i++) { f[28 + i] = my_ip[i]; f[38 + i] = tpa[i]; }
}

extern short cli_test_dac(short channel, int argc, const char * argv[]);
extern short cli_test_gdma(short channel, int argc, const char * argv[]);
extern short cli_test_blit(short channel, int argc, const char * argv[]);

short cli_test_lannet(short channel, int argc, const char * argv[]) {
    char message[160];
    unsigned char my_ip[4], peer[4];
    unsigned short bmsr = 0, pscsr = 0;
    long secs = 30, t0, now, last_arp = -1000;
    long n_good = 0, n_bad = 0, n_bc = 0, n_mc = 0, n_uc = 0, n_arp_req = 0, n_ping = 0, n_tx_err = 0;
    unsigned long maxlen = 0, t, sts, len, v;
    short i, words, have_peer = 0, fdx, speed;

    my_ip[0] = 192; my_ip[1] = 168; my_ip[2] = 0; my_ip[3] = 245;
    if (argc > 1) secs = cli_eval_number(argv[1]);
    if (argc > 2 && lan_parse_ip(argv[2], my_ip)) { print(channel, "LANNET: bad IP address\n"); return -1; }
    for (i = 0; i < 3; i++) peer[i] = my_ip[i];
    peer[3] = 1;
    if (argc > 3 && lan_parse_ip(argv[3], peer)) { print(channel, "LANNET: bad peer IP address\n"); return -1; }

    if (LAN_L(LAN_BYTE_TEST) != 0x21436587UL) { print(channel, "LANNET: LAN9221 not answering (BYTE_TEST)\n"); return -1; }
    for (t = 0; t < 100000 && !(lan_rd(LAN_PMT_CTRL) & 1); t++) ;
    if (!(lan_rd(LAN_PMT_CTRL) & 1)) { print(channel, "LANNET: LAN9221 not READY\n"); return -1; }

    /* PHY: auto-negotiation on (TEST LAN leaves it so too); wait up to 6 s for the link */
    lan_mac_wr(1, 0);
    lan_phy_rd(0, &bmsr);
    if (!(bmsr & 0x1000)) lan_phy_wr(0, 0x3300);
    t0 = rtc_get_jiffies();
    do {
        if (lan_phy_rd(1, &bmsr) || lan_phy_rd(1, &bmsr)) { print(channel, "LANNET: PHY access failed\n"); return -1; }
        if ((bmsr & 0x0024) == 0x0024) break;                                 /* link up + auto-negotiation done */
    } while (rtc_get_jiffies() - t0 < 360);
    if (!(bmsr & 0x0004)) {
        int b = bmsr;
        sprintf(message, "LANNET: no link (PHY status %04X) - is the cable plugged in?\n", b);
        print(channel, message);
        return -1;
    }
    lan_phy_rd(31, &pscsr);
    speed = ((pscsr >> 2) & 1) ? 10 : (((pscsr >> 3) & 1) ? 100 : 0);
    fdx = (pscsr >> 4) & 1;
    {
        int sp = speed, a = my_ip[0], b = my_ip[1], c = my_ip[2], d = my_ip[3], st = bmsr, sc = pscsr;
        const char * dx = fdx ? "full" : "half";
        long jl = rtc_get_jiffies() - t0;
        sprintf(message, "Link up: %d Mbit/s %s duplex (PHY status %04X, special %04X, %ld jiffies). IP %d.%d.%d.%d, MAC 02:46:4E:58:4B:25\n",
                sp, dx, st, sc, jl, a, b, c, d);
        print(channel, message);
    }

    /* MAC address, then the MAC: TX + RX on, duplex as negotiated, broadcast accepted, no promiscuous */
    lan_mac_wr(2, ((unsigned long)lan_my_mac[5] << 8) | lan_my_mac[4]);
    lan_mac_wr(3, ((unsigned long)lan_my_mac[3] << 24) | ((unsigned long)lan_my_mac[2] << 16) | ((unsigned long)lan_my_mac[1] << 8) | lan_my_mac[0]);
    lan_wr(LAN_RX_CFG, 0x00008000UL);                                         /* RX dump */
    for (t = 0; t < 100000 && (lan_rd(LAN_RX_CFG) & 0x00008000UL); t++) ;
    lan_wr(0x58, 0xFFFFFFFFUL);
    lan_mac_wr(1, (fdx ? (1UL << 20) : 0) | (1UL << 3) | (1UL << 2));
    /* 2.0.20: HW_CFG.SF (store and forward): send a frame only once all of it is in the TX FIFO. Without it the
       transmitter starts early, our CPU writes the FIFO slower than 100 Mbit/s empties it, and long frames underrun
       (INT_STS TDFU, the transmitter stops) */
    lan_wr(0x74, lan_rd(0x74) | 0x00100000UL);
    lan_wr(LAN_TX_CFG, 0x0000C000UL);                                         /* TX status + data dump */
    for (t = 0; t < 100000 && (lan_rd(LAN_TX_CFG) & 0x0000C000UL); t++) ;
    lan_wr(LAN_TX_CFG, 0x00000002UL);
    (void)lan_rd(LAN_RX_DROP);                                                /* clears on read */

    lan_tx_bad = 0; lan_tx_last_bad = 0;
    t0 = rtc_get_jiffies();
    while ((now = rtc_get_jiffies()) - t0 < secs * 60) {
        if (!have_peer && now - last_arp >= 60) {
            lan_arp(lan_tx_buf, 1, lan_my_mac, my_ip, peer);
            if (lan_send(lan_tx_buf, 60)) n_tx_err++;
            last_arp = now;
        }
        if (!(lan_rd(LAN_RX_FIFO_INF) & 0x00FF0000UL)) continue;
        sts = lan_bswap32(lan_fifo_rd(LAN_RX_STS, 1));
        len = (sts >> 16) & 0x3FFF;                                           /* includes the 4 CRC bytes */
        words = (short)((len + 3) / 4);
        for (i = 0; i < words; i++) {
            v = lan_fifo_rd(LAN_RX_DATA, 1);
            if (4 * i + 3 < (short)sizeof(lan_rx_buf)) {
                lan_rx_buf[4*i] = (unsigned char)(v >> 24); lan_rx_buf[4*i+1] = (unsigned char)(v >> 16);
                lan_rx_buf[4*i+2] = (unsigned char)(v >> 8); lan_rx_buf[4*i+3] = (unsigned char)v;
            }
        }
        if (sts & 0x8000UL) {
            if (n_bad++ < 5) { sprintf(message, "  bad RX status %08lX (length %lu)\n", sts, len); print(channel, message); }
            continue;
        }
        n_good++;
        if (len > maxlen) maxlen = len;
        if (sts & 0x2000UL) n_bc++; else if (sts & 0x0400UL) n_mc++; else n_uc++;
        if (len < 46 || len > sizeof(lan_rx_buf)) continue;
        /* ARP for us */
        if (lan_rx_buf[12] == 0x08 && lan_rx_buf[13] == 0x06 && lan_rx_buf[38] == my_ip[0] && lan_rx_buf[39] == my_ip[1] &&
            lan_rx_buf[40] == my_ip[2] && lan_rx_buf[41] == my_ip[3]) {
            if (lan_rx_buf[21] == 1) {                                        /* request: answer it */
                lan_arp(lan_tx_buf, 2, &lan_rx_buf[22], my_ip, &lan_rx_buf[28]);
                if (lan_send(lan_tx_buf, 60)) n_tx_err++;
                n_arp_req++;
            } else if (lan_rx_buf[21] == 2 && !have_peer && lan_rx_buf[28] == peer[0] && lan_rx_buf[29] == peer[1] &&
                       lan_rx_buf[30] == peer[2] && lan_rx_buf[31] == peer[3]) {
                int a = peer[0], b = peer[1], c = peer[2], d = peer[3];
                int m0 = lan_rx_buf[22], m1 = lan_rx_buf[23], m2 = lan_rx_buf[24], m3 = lan_rx_buf[25], m4 = lan_rx_buf[26], m5 = lan_rx_buf[27];
                long ms = (now - t0) * 1000 / 60;
                sprintf(message, "  ARP reply: %d.%d.%d.%d is %02X:%02X:%02X:%02X:%02X:%02X (after %ld ms)\n", a, b, c, d, m0, m1, m2, m3, m4, m5, ms);
                print(channel, message);
                have_peer = 1;
            }
            continue;
        }
        /* ICMP echo request to us (IPv4) */
        if (lan_rx_buf[12] == 0x08 && lan_rx_buf[13] == 0x00 && lan_rx_buf[23] == 1 && lan_rx_buf[30] == my_ip[0] &&
            lan_rx_buf[31] == my_ip[1] && lan_rx_buf[32] == my_ip[2] && lan_rx_buf[33] == my_ip[3]) {
            short ihl = (short)(lan_rx_buf[14] & 0x0F) * 4;
            short tot = (short)(((unsigned short)lan_rx_buf[16] << 8) | lan_rx_buf[17]);
            short icmp = 14 + ihl, ilen = tot - ihl;
            unsigned short cs;
            if (lan_rx_buf[icmp] != 8 || 14 + tot > (short)len - 4) continue;
            if (lan_csum(&lan_rx_buf[icmp], ilen) != 0) { if (n_bad++ < 5) print(channel, "  ping with a bad ICMP checksum\n"); continue; }
            for (i = 0; i < 14 + tot; i++) lan_tx_buf[i] = lan_rx_buf[i];
            for (i = 0; i < 6; i++) { lan_tx_buf[i] = lan_rx_buf[6 + i]; lan_tx_buf[6 + i] = lan_my_mac[i]; }
            for (i = 0; i < 4; i++) { lan_tx_buf[26 + i] = my_ip[i]; lan_tx_buf[30 + i] = lan_rx_buf[26 + i]; }
            lan_tx_buf[icmp] = 0; lan_tx_buf[icmp + 2] = 0; lan_tx_buf[icmp + 3] = 0;
            cs = lan_csum(&lan_tx_buf[icmp], ilen);
            lan_tx_buf[icmp + 2] = (unsigned char)(cs >> 8); lan_tx_buf[icmp + 3] = (unsigned char)cs;
            if (lan_send(lan_tx_buf, 14 + tot)) n_tx_err++;
            n_ping++;
            if (n_ping <= 3 || (n_ping % 50) == 0) {
                int fl = 14 + tot;
                long np = n_ping;
                sprintf(message, "  ping %ld answered (%d-byte frame)\n", np, fl);
                print(channel, message);
            }
        }
    }
    {
        unsigned long drop = lan_rd(LAN_RX_DROP), ints = lan_rd(0x58), hw = lan_rd(0x74), tl = lan_tx_last_bad;
        long tb = lan_tx_bad;
        const char * pr = have_peer ? "answered" : "NO ANSWER";
        sprintf(message, "LANNET %ld s: %ld good frames (%ld broadcast, %ld multicast, %ld unicast), %ld bad, longest %lu bytes, RX_DROP %lu\n",
                secs, n_good, n_bc, n_mc, n_uc, n_bad, maxlen, drop);
        print(channel, message);
        sprintf(message, "  ARP for the peer: %s; ARP requests answered %ld; pings answered %ld; TX FIFO full %ld; INT_STS %08lX\n",
                pr, n_arp_req, n_ping, n_tx_err, ints);
        print(channel, message);
        sprintf(message, "  TX status errors %ld (last %08lX), HW_CFG %08lX\n", tb, tl, hw);
        print(channel, message);
    }
    lan_mac_wr(1, 0);
    lan_wr(LAN_TX_CFG, 0);
    return (n_bad || !have_peer || n_tx_err || lan_tx_bad) ? -1 : 0;
}

/* 2.0.14 (2026-11-05): TEST FILECMP <a> [<b>] - read whole files into SDRAM (a at $0300_0000, b at $0380_0000,
 * up to 8 MB each) in 16 KB chunks (chan_read size is a short), print the time + KB/s of each read (file-system speed of /sd and /hd), then
 * compare a with b byte by byte. */
static long filecmp_read(short screen, const char * path, unsigned char * dst, long * jf) {
    char message[100];
    long total = 0, t0, t1;
    short n, fd = fsys_open(path, FA_READ);
    if (fd < 0) { err_print(screen, "Unable to open file", fd); return -1; }
    t0 = rtc_get_jiffies();
    do {
        n = chan_read(fd, dst + total, 16384);
        if (n > 0) total += n;
    } while (n == 16384 && total < 0x800000 - 16384);
    t1 = rtc_get_jiffies();
    fsys_close(fd);
    *jf = t1 - t0;
    sprintf(message, "%s: %ld bytes in %ld jiffies = %ld KB/s\n", path, total, t1 - t0,
            (t1 > t0) ? (total * 60 / 1024) / (t1 - t0) : 0L);
    print(screen, message);
    return total;
}

short cli_test_filecmp(short screen, int argc, const char * argv[]) {
    char message[100];
    unsigned char * a = (unsigned char *)0x03000000;
    unsigned char * b = (unsigned char *)0x03800000;
    long na, nb, i, ja, jb;
    if (argc < 2) { print(screen, "USAGE: TEST FILECMP <path a> [<path b>]\n"); return ERR_GENERAL; }
    na = filecmp_read(screen, argv[1], a, &ja);
    if (na < 0 || argc < 3) return 0;
    nb = filecmp_read(screen, argv[2], b, &jb);
    if (nb < 0) return 0;
    if (na != nb) { sprintf(message, "FILECMP: sizes differ (%ld / %ld)\n", na, nb); print(screen, message); }
    for (i = 0; i < na && i < nb; i++) {
        if (a[i] != b[i]) {
            sprintf(message, "FILECMP: first difference at %ld: %02X / %02X\n", i, a[i], b[i]);
            print(screen, message);
            return 0;
        }
    }
    if (na == nb) print(screen, "FILECMP: SAME\n");
    return 0;
}

short cli_test_create(short screen, int argc, const char * argv[]) {
    short n;

    if (argc > 1) {
        short channel = fsys_open(argv[1], FA_CREATE_NEW | FA_WRITE);
        if (channel >= 0) {
            char * message = "Hello, world!\n";
            n = chan_write(channel, message, strlen(message));
            if (n <= 0) {
                err_print(screen, "Unable to write to file", n);
            }

            fsys_close(channel);
            return 0;

        } else {
            err_print(screen, "Unable to open to file", channel);
            return ERR_GENERAL;
        }

    } else {
        print(screen, "USAGE: TEST CREATE <path>\n");
        return ERR_GENERAL;
    }
}

#if HAS_PARALLEL_PORT
short cli_test_lpt(short screen, int argc, const char * argv[]) {
    char message[160];
    unsigned char scancode;

    sprintf(message, "Test parallel port:\nF1: DATA='B'  F2: DATA='A'  F3: STRB=1  F4: STRB=0\n");
    sys_chan_write(screen, message, strlen(message));
    sprintf(message, "F5: INIT=1  F6: INIT=0  F7: SEL=1  F8: SEL=0\nESC: Quit\n");
    sys_chan_write(screen, message, strlen(message));

    unsigned char ctrl = 0;
    *LPT_CTRL_PORT = ctrl;

    while (1) {
        scancode = sys_kbd_scancode();
        switch (scancode) {
            case 0x3B:      /* F1 */
                print(0, "DATA = 'B'\n");
                *LPT_DATA_PORT = 'B';
                break;

            case 0x3C:      /* F2 */
                print(0, "DATA = 'A'\n");
                *LPT_DATA_PORT = 'A';
                break;

            case 0x3D:      /* F3 */
                ctrl |= LPT_CTRL_STROBE;
                *LPT_CTRL_PORT = ctrl;
                sprintf(message, "STROBE = TRUE [%02X]\n", ctrl);
                print(0, message);
                break;

            case 0x3E:      /* F4 */
                ctrl &= ~LPT_CTRL_STROBE;
                *LPT_CTRL_PORT = ctrl;
                sprintf(message, "STROBE = FALSE [%02X]\n", ctrl);
                print(0, message);
                break;

            case 0x3F:      /* F5 */
                ctrl |= LPT_CTRL_mINIT;
                *LPT_CTRL_PORT = ctrl;
                sprintf(message, "INIT = TRUE [%02X]\n", ctrl);
                print(0, message);
                break;

            case 0x40:      /* F6 */
                ctrl &= ~LPT_CTRL_mINIT;
                *LPT_CTRL_PORT = ctrl;
                sprintf(message, "INIT = FALSE [%02X]\n", ctrl);
                print(0, message);
                break;

            case 0x41:      /* F7 */
                ctrl |= LPT_CTRL_SELECT;
                *LPT_CTRL_PORT = ctrl;
                sprintf(message, "SELECT = TRUE [%02X]\n", ctrl);
                print(0, message);
                break;

            case 0x42:      /* F8 */
                ctrl &= ~LPT_CTRL_SELECT;
                *LPT_CTRL_PORT = ctrl;
                sprintf(message, "SELECT = FALSE [%02X]\n", ctrl);
                print(0, message);
                break;

            case 0x01:      /* ESC */
                return 0;

            case 0x02:      /* 1 */
                *LPT_DATA_PORT = '\r';
                break;

            default:
                break;
        }
    }

    return 0;
}

short cmd_test_print(short screen, int argc, const char * argv[]) {
    const char * test_pattern = "0123456789ABCDEFGHIJKLMNOPQRTSUVWZXYZ\r\n";

    char message[160];
    unsigned short scancode = 0;

    print(screen, "Initializing printer...\n");

    short lpt = sys_chan_open(CDEV_LPT, 0, 0);
    if (lpt > 0) {
        print(screen, "Sending test patterns to printer (ESC to quit)...\n");

        while (scancode != 0x01) {
            short result = sys_chan_write(lpt, test_pattern, strlen(test_pattern));
            if (result != 0) {
                sprintf(message, "Unable to print: %s\n", err_message(result));
                print(screen, message);

                sprintf(message, "Printer status: %02X\n", sys_chan_status(lpt));
                print(screen, message);
                break;
            }
            scancode = sys_kbd_scancode();
        }

        sys_chan_close(lpt);
    } else if (lpt == 0) {
        print(screen, "Unable to print: got a bad channel number.\n");

    } else {
        sprintf(message, "Could not open channel to printer: %s\n", err_message(lpt));
        print(screen, message);
    }
    return 0;
}
#endif

/**
 * Test the ANSI escape codes
 */
short cmd_test_ansi(short screen, int argc, const char * argv[]) {
    t_rect old_region, region;
    char buffer[255];

    // Clear the screen and home the cursor
    print(screen, "\x1b[2J\x1b[H0_________1_________2_________3_________\n");
    print(screen, "0123456789012345678901234567890123456789\n");

    // Test some positioning
    print(screen, "\x1b[21;3H20\x1b[50;1H\x1b[31m\x03\x1b[B\x1b[30m\x05\x1b[3D\x06\x1b[1B\x1b[31m\x04\n");

    // Test color bars
    for (int i = 0; i < 8; i++) {
        char * color_name;
        switch (i) {
            case 0: color_name = "BLACK"; break;
            case 1: color_name = "RED"; break;
            case 2: color_name = "GREEN"; break;
            case 3: color_name = "YELLOW"; break;
            case 4: color_name = "BLUE"; break;
            case 5: color_name = "ORANGE"; break;
            case 6: color_name = "CYAN"; break;
            case 7: color_name = "GREY"; break;
            default: color_name = "???"; break;
        }

        sprintf(buffer, "\x1b[%dm%10s\x1b[37;44m \x1b[30;%dm%10s\x1b[37;44m", 30 + i, color_name, 40 + i, color_name);
        sys_chan_write(screen, buffer, strlen(buffer));

        sprintf(buffer, "\t\x1b[%dm%10s\x1b[37;44m \x1b[30;%dm%10s\x1b[37;44m\n", 90 + i, color_name, 100 + i, color_name);
        sys_chan_write(screen, buffer, strlen(buffer));
    }

    print(screen, "\x1b[1;13H0123456789ABCDEF\x1b[6;13H\x1b[2@^^\x1b[20;13H<--2 characters inserted");
    print(screen, "\x1b[1;14H0123456789ABCDEF\x1b[6;14H\x1b[2P\x1b[20;14H<--2 characters deleted");
    print(screen, "\x1b[1;15H0123456789ABCDEF\x1b[8;15H\x1b[0K\x1b[20;15H<--2nd half of line deleted");
    print(screen, "\x1b[1;16H0123456789ABCDEF\x1b[8;16H\x1b[1K\x1b[20;16H<--1st half of line deleted");
    print(screen, "\x1b[1;17H0123456789ABCDEF\x1b[8;17H\x1b[2K\x1b[20;17H<-- Whole line deleted...\n");

    print(screen, "\x1b[1;20HDelete 2nd Half\x1b[22;20HDelete 1st Half\x1b[43;20HDelete All");

    txt_get_region(screen, &old_region);

    region.origin.x = 0;
    region.origin.y = 20;
    region.size.width = 20;
    region.size.height = 10;
    txt_set_region(screen, &region);
    txt_fill(screen, 'X');
    region.origin.x = 1;
    region.origin.y = 21;
    region.size.width = 18;
    region.size.height = 8;
    txt_set_region(screen, &region);
    print(screen, "\x1b[10;4H\x1b[0J");

    region.origin.x = 21;
    region.origin.y = 20;
    region.size.width = 20;
    region.size.height = 10;
    txt_set_region(screen, &region);
    txt_fill(screen, 'X');
    region.origin.x = 22;
    region.origin.y = 21;
    region.size.width = 18;
    region.size.height = 8;
    txt_set_region(screen, &region);
    print(screen, "\x1b[10;4H\x1b[1J");

    region.origin.x = 42;
    region.origin.y = 20;
    region.size.width = 20;
    region.size.height = 10;
    txt_set_region(screen, &region);
    txt_fill(screen, 'X');
    region.origin.x = 43;
    region.origin.y = 21;
    region.size.width = 18;
    region.size.height = 8;
    txt_set_region(screen, &region);
    print(screen, "\x1b[1;4H\x1b[2J");

    txt_set_region(screen, &old_region);
    print(screen, "\x1b[1;35H\x1b[0;44;37mDone!\n");

    return 0;
}

static short cli_test_textscroll (short screen, int argc, const char * argv[]) {
    int i;
    char c;    
    char *scr;
    short *swrw;
    char *dst;

    if (argc != 2) {
        printf("Copy size missing!");
        return 0;
    }        

#if HAS_DUAL_SCREEN
    switch (screen) {
    case TXT_SCREEN_A2560K_A: 
        scr = (char*)ScreenText_A;
        break;
    case TXT_SCREEN_A2560K_B:
        scr = (char*)ScreenText_B;
        break;
    default:
        // How would we even get here ??
        // TODO return error somehow
        return 0;
    }
#elif MODEL == MODEL_FOENIX_A2560U || MODEL == MODEL_FOENIX_A2560U_PLUS
    scr = (char*)ScreenText_A;
#endif 

    // Fill screen byte by byte
    const int line_length = 640/8; // # chars on a line
    const int full_screen_size= line_length*480/8;    
    unsigned long jiffies = sys_time_jiffies() + 60*3;

    if (strcmp("word", argv[1]) == 0) {       
        short *dstw;
        char w[2];

        // Fill using words
        for (i=0, dstw=(short*)scr; i<full_screen_size/2; i++) {
            if (w[0] > 'Y' || (i % (line_length/2)) == 0) {
                w[0] = 'A';
                w[1] = 'B';
            }
                
            *dstw++ = *((short*)&w);
            w[0] += 2;
            w[1] += 2;            
        }

        // Wait a bit
        while (sys_time_jiffies() < jiffies);

        for (i=0, dstw = (short*)scr; i<(full_screen_size-line_length)/2; i++) {
            *dstw = dstw[1];
            dstw++;
        }
    }
    else if (strcmp("byte", argv[1]) == 0) {
        // Fill using bytes
        for (i=0, dst=scr; i<full_screen_size; i++) {
            if (c > 'Z' || (i % line_length) == 0)
                c = 'A';
            *dst++ = c++;
        }

        // Wait a bit
        while (sys_time_jiffies() < jiffies);

        for (i=0, dst = scr; i<full_screen_size-line_length; i++) {
            *dst = dst[1];
            dst++;
        }
    }
    else
        print(screen, "Unrecognized option!");

    return 0;
}


static short cli_test_background (short screen, int argc, const char * argv[]) {
    if (argc != 2) {
        print (screen, "AARRGGBB color code missing.");
        return 0;
    }
    
    uint32_t jiffies = sys_time_jiffies() + 60*3;

    uint32_t old_ctrl = *MasterControlReg_A;
    uint32_t old_rgb = *BackGroundControlReg_A;
    uint32_t rgb = (uint32_t)cli_eval_number(argv[1]);

    *MasterControlReg_A = (old_ctrl & 0xfffffff0) | (VKY3_MCR_TEXT_EN|VKY3_MCR_TEXT_OVRLY|VKY3_MCR_GRAPH_EN);
    *BackGroundControlReg_A = rgb;

    // Wait a bit
    while (sys_time_jiffies() < jiffies);

    *MasterControlReg_A = old_ctrl; // Bitmap+screen overlay
    *BackGroundControlReg_A = old_rgb;

    return 0;
}

const t_cli_test_feature cli_test_features[] = {
    {"ANSI", "ANSI: test the ANSI escape codes", cmd_test_ansi},
    {"BACKGROUND", "BACKGROUND <0xaarrggbb>: set the background color to the give RGB color", cli_test_background},
    {"BITMAP", "BITMAP: test the bitmap screen", cli_test_bitmap},
    {"CREATE", "CREATE <path>: test creating a file", cli_test_create},
    {"BLIT", "BLIT [mode] [secs]: GUI blitter test (0 random blits + lists, checked; 4 diag; speed 1-22)", cli_test_blit},
    {"DAC", "DAC [0|2|3] [secs]: DAC player (0 the 19 checks, 2 test tone, 3 scales + chord)", cli_test_dac},
    {"FILECMP", "FILECMP <a> [<b>]: read files (time, KB/s) and compare them", cli_test_filecmp},
    {"LAN", "LAN [frames] [16|32] [maxlen]: LAN9221 FIFO test in PHY loopback (unplug the cable)", cli_test_lan},
    {"LANNET", "LANNET [secs] [ip] [peer]: LAN9221 on the network - link, ARP, answers ping (cable in)", cli_test_lannet},
    {"LANBUS", "LANBUS [count]: LAN9221 bus integrity - BYTE_TEST reads, FIFO_INT write/read-back", cli_test_lanbus},
    {"IDE", "IDE: test reading the MBR of the IDE drive", cli_test_ide},
#if HAS_FLOPPY
    {"FDC", "FDC DSKCHG | [<lba> [WRITE <data>]]: test reading/writing the MBR from the floppy drive", cli_test_fdc},
#endif
#if HAS_SNES_GAMEPAD
    {"GDMA", "GDMA [mode] [secs]: GDMA test (0 random transfers over all banks, checked; 4 diag; speed 1-13)", cli_test_gdma},
    {"GAMEPAD", "GAMEPAD [0 | 1]: test SNES gamepads", cli_test_gamepad},
#endif    
    {"JOY", "JOY: test the joystick", cli_test_joystick},
#if HAS_PARALLEL_PORT
    {"LPT", "LPT: test the parallel port", cli_test_lpt},
#endif
    {"MEM", "MEM [SYS|MERA]: test reading and writing of system or MERA memory", cli_mem_test},
#if HAS_SUPERIO
    {"MIDILOOP", "MIDILOOP: perform a loopback test on the MIDI ports", midi_loop_test},
    {"MIDIRX", "MIDIRX: perform a receive test on the MIDI ports", midi_rx_test},
    {"MIDITX", "MIDITX: send a note to a MIDI keyboard", midi_tx_test},
#endif
    {"OPL3", "OPL3 <delay>: test the OPL3 sound chip, with some optional delay between bytes", opl3_test},
#if HAS_OPN
    {"OPN", "OPN [EXT|INT]: test the OPN sound chip", opn_test},
#endif
#if HAS_OPM
    {"OPM", "OPM [EXT|INT]: test the OPM sound chip", opm_test},
#endif
    {"PANIC", "PANIC: test the kernel panic mechanism", cli_test_panic},
    {"PS2", "PS2: test the PS/2 keyboard", cli_test_ps2},
    {"PSG", "PSG [EXT|INTL|INTR|INTS]: test the PSG sound chip", psg_test},
#if HAS_PARALLEL_PORT
    {"PRINT", "PRINT: sent text to the printer", cmd_test_print},
#endif
#if HAS_FLOPPY
    {"RECALIBRATE", "RECALIBRATE: recalibrate the floppy drive", cli_test_recalibrate},
#endif
    {"RTC", "RTC: Test the RTC interrupts", cli_test_rtc},
#if HAS_FLOPPY
    {"SEEK", "SEEK <track>: move the floppy drive head to a track", cli_test_seek},
#endif
    {"SID", "SID [EXT|INT]: test the SID sound chips", sid_test},
    {"UART","UART [1|2]: test the serial port",cli_test_uart},
    {"VGE", "VGE: run the VGE board checks (V1 - V5)", cli_test_vge},
    {"VGEDEMO", "VGEDEMO: 256 bouncing VGE sprites with collision (any key stops)", cli_test_vgedemo},
    {"TEXTSCROLL", "TEXTSCROLL [byte|word]: fills the text memory with A..Z then scroll one char to the left", cli_test_textscroll},
    {"MEMTEXT", "MEMTEXT: Enable MemText Mode", memtext_enable_test},
    {"TOSGRAPH", "TOSGRAPH: Enable TOSGraph Mode", tosgraph_enable_test},
    {"END", "END", 0}
};

void test_help(short screen) {
    p_cli_test_feature f;
    int i;
    int count;

    print(screen, "USAGE: TEST <feature>\nFeatures supported...\n");

    count = sizeof(cli_test_features) / sizeof(t_cli_test_feature);
    for (i = 0; i < count - 1; i++) {
        if (cli_test_features[i].help != 0) {
            print(screen, cli_test_features[i].help);
            print(screen, "\n");
        }
    }
}

/*
 * Test command
 */
short cmd_test(short screen, int argc, const char * argv[]) {
    short i;
    p_cli_test_feature f;

    if (argc > 1) {
        char feature_upcase[160];

        /* Make an upper case copy of the feature to test */
        strncpy(feature_upcase, argv[1], 80);
        for (i = 0; i < strlen(feature_upcase); i++) {
            feature_upcase[i] = toupper(feature_upcase[i]);
        }

        for (f = (p_cli_test_feature)cli_test_features; f->name != 0; f++) {
            if (strcmp(f->name, feature_upcase) == 0) {
                f->handler(screen, argc - 1, &argv[1]);
                return 0;
            }
        }

        test_help(screen);
        return ERR_GENERAL;
    } else {
        test_help(screen);
        return ERR_GENERAL;
    }

    return 0;
}

// This is meant as a buffer so because of one byte missing in the transfer might get the whole command Test to fail.
// Don't use this routine.
short cmd_Dummy (short screen, int argc, const char * argv[]) 
{
    argc = argc + 1;
    return (argc);
}