/*
 * input.c - Gen1 LVGL boot loader: the keyboard (K: the built-in keyboard FIFO; X: PS/2), the PS/2 mouse (polled 8042
 * in the SuperIO) and the COM1 console. Port of the A2560Me loader (2026-11-06).
 *
 * The controller translates the keyboard to scan code set 1 (command byte bit 6). Each status read tells whether the
 * waiting byte comes from the mouse (bit 5). Keys go into one queue (input_key()); the menu takes the digits itself
 * and hands navigation keys to LVGL through input_lv_key(). The mouse is an LVGL pointer with a small arrow cursor.
 */
#include "lvgl.h"
#include "hw.h"
#include "board.h"
#include "video.h"
#include "input.h"

#define KEYQ_SIZE 16
static uint32_t keyq[KEYQ_SIZE];
static uint8_t keyq_in, keyq_out;

static uint32_t lv_pending, lv_release;         /* LVGL keypad: a key is pressed once, released on the next read */
static int16_t mouse_x = VIDEO_W / 2, mouse_y = VIDEO_H / 2;
static uint8_t mouse_btn, mouse_pkt[3], mouse_idx;
static uint8_t kbd_e0, kbd_skip, rc_esc;
static uint32_t rc_esc_ms;
static uint32_t activity;
static int mouse_present, kbd_present;
static lv_indev_t * pointer_indev, * keypad_indev;

/* ---- 8042 helpers ---- */

static int ps2_wait_write(void) {
    uint32_t t0 = board_ms();
    while (PS2_STAT & PS2_IBF) if (board_ms() - t0 > 20) return 0;
    return 1;
}

/* next byte from either port within ms milliseconds, -1 if none */
static int ps2_read(uint32_t ms) {
    uint32_t t0 = board_ms();
    do {
        if (PS2_STAT & PS2_OBF) return PS2_DATA;
    } while (board_ms() - t0 < ms);
    return -1;
}

static void ps2_ctrl(uint8_t c) { if (ps2_wait_write()) PS2_CMD = c; }
static void ps2_out(uint8_t d)  { if (ps2_wait_write()) PS2_DATA = d; }
static void ps2_flush(void)     { int n = 64; while (n-- && ps2_read(5) >= 0) ; }

static void sio_write(uint8_t index, uint8_t value) { SIO_INDEX = index; SIO_DATA = value; }

/* Take the SuperIO out of reset and switch on its keyboard controller (logical device 7: I/O $60 / $64, keyboard
   IRQ 1, mouse IRQ 12), as the MCP does at start-up. Returns 0 if the chip does not answer. */
static int superio_init(void) {
    uint32_t t0;
    int ok = 0;
    if (!(GAVIN_CTRL & GAVIN_LPC_RSTN)) {           /* after any board reset: still in reset */
        GAVIN_CTRL |= GAVIN_LPC_RSTN;
        delay_ms(1);
        GAVIN_CTRL &= ~GAVIN_LPC_RSTN;
        delay_ms(20);
        GAVIN_CTRL |= GAVIN_LPC_RSTN;
    }
    t0 = board_ms();
    do {                                            /* wait until the chip answers in configuration mode */
        SIO_INDEX = 0x55;                           /* enter configuration mode */
        sio_write(0x07, 0x07);                      /* select logical device 7 */
        SIO_INDEX = 0x07;
        ok = (SIO_DATA == 0x07);
        if (!ok) delay_ms(10);
    } while (!ok && board_ms() - t0 < 1500);
    if (ok) {
        sio_write(0x60, 0x00);                      /* base address $0060 */
        sio_write(0x61, 0x60);
        sio_write(0x70, 0x01);                      /* keyboard IRQ 1 */
        sio_write(0x72, 0x02);                      /* mouse IRQ (as the MCP sets it) */
        sio_write(0x30, 0x01);                      /* device on */
        sio_write(0x22, 0xFF);                      /* power: all devices on */
    }
    SIO_INDEX = 0xAA;                               /* leave configuration mode */
    dbg_printf("superio: %s after %lu ms\n", ok ? "configured" : "NO ANSWER", (unsigned long)(board_ms() - t0));
    return ok;
}

/* send a byte to the keyboard (or the mouse) and wait for its $FA acknowledge; $FE (resend) or silence: try again */
static int dev_send(int mouse, uint8_t b) {
    int r, i, tries;
    for (tries = 0; tries < 3; tries++) {
        if (mouse) ps2_ctrl(0xD4);
        ps2_out(b);
        for (i = 0; i < 4; i++) {
            r = ps2_read(100);
            if (r == 0xFA) return 1;
            dbg_printf("ps2: %s $%02X -> %s$%02X\n", mouse ? "mouse" : "kbd", b, r < 0 ? "nothing " : "", r & 0xFF);
            if (r < 0 || r == 0xFE) break;
        }
    }
    return 0;
}

void input_init(void) {
    int r;
    if (!superio_init()) return;
    ps2_ctrl(0xAD);                     /* keyboard port off */
    ps2_ctrl(0xA7);                     /* mouse port off */
    ps2_flush();
    ps2_ctrl(0xAA);                     /* controller self test: $55 */
    r = ps2_read(100);
    if (r != 0x55) dbg_printf("ps2: controller self test -> $%02X\n", r & 0xFF);
    ps2_ctrl(0x60); ps2_out(0x44);      /* command byte: set 1 translation, no interrupts, both clocks on */
    ps2_ctrl(0xAE);                     /* keyboard port on */
#if MACHINE_HAS_MO_KEYBOARD
    kbd_present = 1;                    /* the built-in keyboard (KBD_MO_DATA FIFO); no PS/2 keyboard to probe */
    { int n; for (n = 0; n < 256 && (KBD_MO_DATA & KBD_MO_VALID); n++) ; }   /* drop keys pressed before the menu */
#else
    kbd_present = dev_send(0, 0xF4);    /* enable scanning (the keyboard is already reset at power-on) */
#endif
    if (!kbd_present) {
        kbd_present = dev_send(0, 0xFF);    /* no answer: reset it (self test $AA follows) */
        r = ps2_read(1000);
        dbg_printf("ps2: kbd reset -> %s, self test $%02X\n", kbd_present ? "ack" : "no ack", r & 0xFF);
        if (kbd_present) kbd_present = dev_send(0, 0xF4);
    }
    ps2_ctrl(0xA8);                     /* mouse port on */
    mouse_present = dev_send(1, 0xF6) && dev_send(1, 0xF4);    /* defaults, then streaming */
    ps2_flush();
    dbg_printf("input: keyboard %s, mouse %s\n", kbd_present ? "yes" : "no", mouse_present ? "yes" : "no");
}

static void key_push(uint32_t k) {
    uint8_t n = (uint8_t)((keyq_in + 1) % KEYQ_SIZE);
    activity++;
    if (n != keyq_out) { keyq[keyq_in] = k; keyq_in = n; }
}

uint32_t input_key(void) {
    uint32_t k;
    if (keyq_in == keyq_out) return 0;
    k = keyq[keyq_out];
    keyq_out = (uint8_t)((keyq_out + 1) % KEYQ_SIZE);
    return k;
}

void input_lv_key(uint32_t k) { lv_pending = k; }

lv_indev_t * input_keypad(void) { return keypad_indev; }

uint32_t input_activity(void) { return activity; }

/* scan code set 1, make codes only */
static void kbd_byte(uint8_t b) {
    static const uint8_t keypad[] = { '7', '8', '9', 0, '4', '5', '6', 0, '1', '2', '3' };   /* $47-$51 */
    uint8_t code = b & 0x7F;
    if (kbd_skip) { kbd_skip--; return; }
    if (b == 0xE0) { kbd_e0 = 1; return; }
    if (b == 0xE1) { kbd_skip = 5; return; }        /* Pause: 6-byte sequence */
    if (b & 0x80) { kbd_e0 = 0; return; }           /* break code */
    if (kbd_e0) {
        kbd_e0 = 0;
        switch (code) {
            case 0x48: key_push(LV_KEY_UP); break;
            case 0x50: key_push(LV_KEY_DOWN); break;
            case 0x4B: key_push(LV_KEY_LEFT); break;
            case 0x4D: key_push(LV_KEY_RIGHT); break;
            case 0x1C: key_push(LV_KEY_ENTER); break;
            case 0x47: key_push(LV_KEY_HOME); break;
            case 0x4F: key_push(LV_KEY_END); break;
            default: break;
        }
        return;
    }
    if (code >= 0x02 && code <= 0x0A) { key_push('1' + code - 0x02); return; }
    if (code >= 0x47 && code <= 0x51 && keypad[code - 0x47]) { key_push(keypad[code - 0x47]); return; }
    switch (code) {
        case 0x1C: key_push(LV_KEY_ENTER); break;
        case 0x01: key_push(LV_KEY_ESC); break;
        case 0x0F: key_push(LV_KEY_NEXT); break;
        case 0x39: key_push(' '); break;
        default:   key_push('?'); break;        /* any other key still stops the countdown */
    }
}

static void mouse_byte(uint8_t b) {
    int dx, dy;
    if (mouse_idx == 0 && !(b & 0x08)) return;      /* the first byte always has bit 3 set: resynchronise */
    mouse_pkt[mouse_idx++] = b;
    if (mouse_idx < 3) return;
    mouse_idx = 0;
    if (mouse_pkt[0] & 0xC0) return;                /* overflow */
    dx = (int)mouse_pkt[1] - ((mouse_pkt[0] & 0x10) ? 256 : 0);
    dy = (int)mouse_pkt[2] - ((mouse_pkt[0] & 0x20) ? 256 : 0);
    mouse_x += dx;
    mouse_y -= dy;                                  /* PS/2 Y is positive upwards */
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x > VIDEO_W - 1) mouse_x = VIDEO_W - 1;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y > VIDEO_H - 1) mouse_y = VIDEO_H - 1;
    if (dx || dy || (mouse_pkt[0] & 1) != mouse_btn) activity++;
    mouse_btn = mouse_pkt[0] & 1;
}

#if MACHINE_HAS_MO_KEYBOARD
/* built-in keyboard word: [7:0] = scan code set 1, bit 7 = break; arrows / Home / End have their own codes, as in the
   MCP's kbd_mo table: $68 up, $69 left, $6A down, $6B right, $63 Home, $66 End, $6D keypad Enter */
static void mo_word(uint32_t w) {
    uint8_t b = (uint8_t)w, code = b & 0x7F;
    if (b & 0x80) return;                           /* break */
    switch (code) {
        case 0x68: key_push(LV_KEY_UP); return;
        case 0x69: key_push(LV_KEY_LEFT); return;
        case 0x6A: key_push(LV_KEY_DOWN); return;
        case 0x6B: key_push(LV_KEY_RIGHT); return;
        case 0x63: key_push(LV_KEY_HOME); return;
        case 0x66: key_push(LV_KEY_END); return;
        case 0x6D: key_push(LV_KEY_ENTER); return;
        default:   break;
    }
    if (code == 0x2A || code == 0x36 || code == 0x1D || code == 0x38 || code == 0x3A || code == 0x5B ||
        code == 0x5C || code == 0x5D || code == 0x5E) return;      /* modifiers alone do nothing */
    kbd_byte(code);
}
#endif

/* console keys: digits, RETURN, ESC, and the ANSI arrows ESC [ A..D */
static void rc_char(char c) {
    if (rc_esc == 1) { rc_esc = (c == '[') ? 2 : 0; if (!rc_esc) key_push(LV_KEY_ESC); return; }
    if (rc_esc == 2) {
        rc_esc = 0;
        if (c == 'A') key_push(LV_KEY_UP);
        else if (c == 'B') key_push(LV_KEY_DOWN);
        else if (c == 'C') key_push(LV_KEY_RIGHT);
        else if (c == 'D') key_push(LV_KEY_LEFT);
        return;
    }
    if (c == 0x1B) { rc_esc = 1; rc_esc_ms = board_ms(); return; }
    if (c == '\r' || c == '\n') key_push(LV_KEY_ENTER);
    else if (c >= '1' && c <= '9') key_push((uint32_t)c);
    else if (c == '\t') key_push(LV_KEY_NEXT);
    else key_push('?');
}

void input_poll(void) {
    int i;
    char c;
    for (i = 0; i < 32; i++) {
        uint8_t st = PS2_STAT;
        uint8_t b;
        if (!(st & PS2_OBF)) break;
        b = PS2_DATA;
        if (st & PS2_AUX) mouse_byte(b); else kbd_byte(b);
    }
#if MACHINE_HAS_MO_KEYBOARD
    for (i = 0; i < 16; i++) {
        uint32_t w = KBD_MO_DATA;                   /* a long read pops one word */
        if (!(w & KBD_MO_VALID)) break;
        mo_word(w);
    }
#endif
    while ((c = dbg_getc()) != 0) rc_char(c);
    /* a lone ESC from the console: no '[' within 100 ms */
    if (rc_esc == 1 && board_ms() - rc_esc_ms > 100) { rc_esc = 0; key_push(LV_KEY_ESC); }
}

/* ---- LVGL input devices ---- */

static void keypad_read(lv_indev_drv_t * drv, lv_indev_data_t * data) {
    (void)drv;
    if (lv_release) {
        data->key = lv_release;
        data->state = LV_INDEV_STATE_RELEASED;
        lv_release = 0;
    } else if (lv_pending) {
        data->key = lv_pending;
        data->state = LV_INDEV_STATE_PRESSED;
        lv_release = lv_pending;
        lv_pending = 0;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void pointer_read(lv_indev_drv_t * drv, lv_indev_data_t * data) {
    (void)drv;
    data->point.x = mouse_x;
    data->point.y = mouse_y;
    data->state = mouse_btn ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* arrow cursor, 11 x 19: X = black outline, O = white, . = transparent (chroma key green) */
static const char * const cursor_art[19] = {
    "X..........", "XX.........", "XOX........", "XOOX.......", "XOOOX......", "XOOOOX.....", "XOOOOOX....",
    "XOOOOOOX...", "XOOOOOOOX..", "XOOOOOOOOX.", "XOOOOOXXXXX", "XOOXOOX....", "XOX.XOOX...", "XX..XOOX...",
    "X....XOOX..", ".....XOOX..", "......XOOX.", "......XOOX.", ".......XX..",
};
static lv_color_t cursor_px[11 * 19];
static lv_img_dsc_t cursor_dsc;

lv_group_t * input_lv_init(void) {
    static lv_indev_drv_t kdrv, pdrv;
    lv_indev_t * kbd;
    lv_group_t * g = lv_group_create();
    int x, y;

    lv_indev_drv_init(&kdrv);
    kdrv.type = LV_INDEV_TYPE_KEYPAD;
    kdrv.read_cb = keypad_read;
    kbd = lv_indev_drv_register(&kdrv);
    keypad_indev = kbd;
    lv_indev_set_group(kbd, g);
    lv_group_set_default(g);

    if (mouse_present) {
        lv_obj_t * cur;
        for (y = 0; y < 19; y++)
            for (x = 0; x < 11; x++) {
                char ch = cursor_art[y][x];
                cursor_px[y * 11 + x] = (ch == 'X') ? lv_color_black() : (ch == 'O') ? lv_color_white() : LV_COLOR_CHROMA_KEY;
            }
        cursor_dsc.header.always_zero = 0;
        cursor_dsc.header.cf = LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED;
        cursor_dsc.header.w = 11;
        cursor_dsc.header.h = 19;
        cursor_dsc.data_size = sizeof(cursor_px);
        cursor_dsc.data = (const uint8_t *)cursor_px;

        lv_indev_drv_init(&pdrv);
        pdrv.type = LV_INDEV_TYPE_POINTER;
        pdrv.read_cb = pointer_read;
        pointer_indev = lv_indev_drv_register(&pdrv);
        cur = lv_img_create(lv_layer_sys());
        lv_img_set_src(cur, &cursor_dsc);
        lv_indev_set_cursor(pointer_indev, cur);
    }
    return g;
}
