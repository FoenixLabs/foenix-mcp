/*
 * menu.c - Gen1 LVGL boot loader: the boot menu screen and the options pop-up (port of the A2560Me loader).
 *
 *   logo (Foenix Labs)
 *   machine line, message line
 *   entries from BOOT.CFG on the SD card: number, name, address and size ("empty" when the block is blank)
 *   countdown bar + status line, key help
 *
 * Keys: 1-9 choose that entry, arrows / Tab move, Enter chooses the selected entry, any key or mouse move stops the
 * countdown. The mouse can click an entry. An entry with options (n.1 = "..." lines, e.g. VioletBSD) opens a pop-up
 * in its own colours: 1-n / arrows / Enter / click start an option, Esc or a click outside goes back. The countdown
 * starts the default entry with its remembered option, without the pop-up.
 * Starting = hand-over record in SRAM, DEFAULT / n.DEFAULT written back if they changed, then BOOT_SEL: the FPGA
 * reboots into that flash block.
 */
#include <string.h>
#include "lvgl.h"
#include "hw.h"
#include "board.h"
#include "video.h"
#include "input.h"
#include "bootcfg.h"
#include "menu.h"

#define BG565           0x1083                  /* (16, 16, 24): must match BG565 in tools/mklogo.py */
#define C_BG            lv_color_hex(0x101018)
#define C_PANEL         lv_color_hex(0x221E30)
#define C_PANEL_FOCUS   lv_color_hex(0x3A2140)
#define C_ACCENT        lv_color_hex(0xE8833A)
#define C_PRESS         lv_color_hex(0x8E2A5E)
#define C_DIM           lv_color_hex(0x8C88A0)
#define C_WARN          lv_color_hex(0xF0A030)

/* the options pop-up: the VioletBSD colours (its logo: white phoenix on #7337DA) */
#define C_POP_BG        lv_color_hex(0x7337DA)
#define C_POP_BORDER    lv_color_hex(0xB89CF2)
#define C_POP_BTN       lv_color_hex(0x5A27B4)
#define C_POP_FOCUS     lv_color_hex(0x8D5DEB)
#define C_POP_PRESS     lv_color_hex(0x431B8C)
#define C_POP_TEXT2     lv_color_hex(0xE2D6FB)

extern const lv_img_dsc_t * logo_get(void);
extern const lv_img_dsc_t * violet_logo_get(void);

/* lvboot 1.1: the logos are stored as 8-bit indexed images (small in flash), but LVGL decodes an indexed image through
   its palette pixel by pixel on every redraw. Both are fully opaque, so they are converted once into plain RGB565
   (true colour) images in SRAM, which LVGL draws as a straight copy. */
static lv_color_t logo_px[300 * 172], violet_px[150 * 150];
static lv_img_dsc_t logo_tc, violet_tc;

static const lv_img_dsc_t * true_color(const lv_img_dsc_t * src, lv_color_t * buf, lv_img_dsc_t * dst) {
    const lv_color32_t * pal = (const lv_color32_t *)src->data;     /* 256 palette entries, then the indices */
    const uint8_t * idx = src->data + 256 * sizeof(lv_color32_t);
    uint32_t n = (uint32_t)src->header.w * src->header.h, i;
    if (dst->data) return dst;                                       /* converted already */
    for (i = 0; i < n; i++) {
        lv_color32_t c = pal[idx[i]];
        buf[i] = lv_color_make(c.ch.red, c.ch.green, c.ch.blue);
    }
    *dst = *src;
    dst->header.cf = LV_IMG_CF_TRUE_COLOR;
    dst->data_size = n * sizeof(lv_color_t);
    dst->data = (const uint8_t *)buf;
    return dst;
}

static lv_obj_t * msg_label, * status_label, * bar, * list;
static lv_obj_t * buttons[CFG_MAX_ENTRIES + 1];
static lv_style_t st_btn, st_btn_focus, st_btn_press, st_pop_btn, st_pop_focus, st_pop_press;
static int counting, have_default;
static uint32_t count_start, count_len;

/* push the 68060 data cache (copyback since lvboot 1.1) to memory */
static void cache_push(void) = "\tnop\n\tcpusha\tdc\n\tnop";

static lv_obj_t * popup;                        /* the pop-up: its panel, or with POPUP_DIM the dark overlay (NULL when closed) */
static lv_group_t * main_group, * popup_group;
static int popup_entry;

uint16_t menu_bg565(void) { return BG565; }

static void set_msg(const char * fmt, int a, const char * s) {
    char m[120];
    lv_snprintf(m, sizeof(m), fmt, a, s);
    lv_label_set_text(msg_label, m);
    dbg_printf("menu: %s\n", m);
}

static void stop_countdown(void) {
    if (!counting) return;
    counting = 0;
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(status_label, "Choose an entry: 1-9, or select it and press Enter (or click it).");
}

/* a slim scrollbar, shown only when the content is taller than the object (remove_style_all also removed LVGL's) */
static void scrollbar_style(lv_obj_t * o, lv_color_t c) {
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(o, c, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(o, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(o, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(o, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(o, 2, LV_PART_SCROLLBAR);
}

/* the option an entry would start with: its n.DEFAULT if valid, else 1; 0 for an entry without options */
static int default_option(const boot_entry_t * e) {
    if (e->nsub <= 0) return 0;
    return (e->sub_default >= 1 && e->sub_default <= e->nsub) ? e->sub_default : 1;
}

static const char * start_name(const boot_entry_t * e, int opt) {
    if (opt > 0 && e->sub_name[opt][0]) return e->sub_name[opt];
    return e->name[0] ? e->name : "(no name)";
}

static void handover(int i, int opt) {
    const boot_entry_t * e = &cfg_entries[i];
    handover_t * h = (handover_t *)HANDOVER_ADDR;
    memset(h, 0, sizeof(*h));
    h->magic = HANDOVER_MAGIC;
    h->entry = (uint32_t)i;
    h->option = (uint32_t)opt;
    h->flash_addr = e->addr;
    lv_snprintf(h->entry_name, sizeof(h->entry_name), "%s", e->name);
    if (opt > 0) lv_snprintf(h->option_name, sizeof(h->option_name), "%s", e->sub_name[opt]);
    h->check = h->magic ^ h->entry ^ h->option ^ h->flash_addr;
}

/* start entry i with option opt (0 = none): never returns */
static void start_entry(int i, int opt) {
    boot_entry_t * e = &cfg_entries[i];
    char m[96];
    uint32_t sel;
    stop_countdown();
    if (FPGA_BUILD < BOOT_MIN_BUILD || FPGA_BUILD > 0x9999UL) {     /* no BOOT_SEL in this FPGA: it would do nothing */
        set_msg("This FPGA (build %04X) cannot boot a flash block: build 1110 or later is needed.%s", (int)FPGA_BUILD, "");
        return;
    }
    lv_snprintf(m, sizeof(m), "Starting %s ...", start_name(e, opt));
    lv_label_set_text(status_label, m);
    lv_label_set_text(msg_label, "");
    lv_refr_now(NULL);
    if (i != cfg_default || (opt > 0 && opt != e->sub_default)) cfg_save(i, opt > 0 ? i : 0, opt);
    handover(i, opt);
    sel = BOOT_KEY | ((uint32_t)layout_of(e->size) << 4) | ((e->addr - FLASH_BASE) / e->size);
    dbg_printf("menu: start %d.%d (%s) at $%08lX, BOOT_SEL $%08lX\n", i, opt, start_name(e, opt),
               (unsigned long)e->addr, (unsigned long)sel);
    delay_ms(300);
    video_off();
    cache_push();                               /* the hand-over record must be in SRAM, not only in the data cache */
    BOOT_SEL = sel;                             /* the FPGA reboots the board into that block now */
    for (;;) ;
}

/* ---- the options pop-up ---- */

static void overlay_event(lv_event_t * ev);

#ifndef POPUP_DIM
/* lvboot 1.1: each pop-up is built once, hidden, when the menu starts (build_popup); opening only shows it and closing
   hides it, which saves about 70 ms of object building per open. */
static lv_obj_t * pre_panel[CFG_MAX_ENTRIES + 1];
static lv_group_t * pre_group[CFG_MAX_ENTRIES + 1];
static lv_obj_t * pre_focus[CFG_MAX_ENTRIES + 1];
#endif

static void close_popup(void) {
    if (!popup) return;
    lv_indev_set_group(input_keypad(), main_group);
    lv_group_set_default(main_group);
#ifdef POPUP_DIM
    lv_obj_del(popup);
    lv_group_del(popup_group);
#else
    lv_obj_clear_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_event_cb(lv_layer_top(), overlay_event);
    lv_obj_add_flag(popup, LV_OBJ_FLAG_HIDDEN);     /* kept for the next open */
#endif
    popup = NULL;
    dbg_printf("menu: pop-up closed\n");
}

static void option_event(lv_event_t * ev) {
    start_entry(popup_entry, (int)(long)lv_event_get_user_data(ev));
}

static void overlay_event(lv_event_t * ev) {
#ifdef POPUP_DIM
    if (lv_event_get_target(ev) == popup) close_popup();        /* a click outside the panel (on the dark overlay) */
#else
    if (lv_event_get_target(ev) == lv_layer_top()) close_popup();   /* a click outside the panel */
#endif
}

/* the pop-up's panel for entry i, in group g (its option buttons join g); returns the panel, *focus = the default
   option's button */
static lv_obj_t * popup_panel(int i, lv_obj_t * parent, lv_group_t * g, lv_obj_t ** focus) {
    boot_entry_t * e = &cfg_entries[i];
    lv_obj_t * panel, * col, * o, * b;
    char m[64];
    int k, h;

    lv_group_set_default(g);                    /* the option buttons join this group, not the main list's */
    h = 110 + e->nsub * 44;
    if (h < 190) h = 190;
    if (h > 440) h = 440;                           /* more options than fit: the option column scrolls */
    panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 560, h);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, C_POP_BG, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, C_POP_BORDER, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    o = lv_img_create(panel);
    lv_img_set_src(o, true_color(violet_logo_get(), violet_px, &violet_tc));
    lv_obj_align(o, LV_ALIGN_LEFT_MID, 12, 0);

    col = lv_obj_create(panel);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 370, h - 24);
    lv_obj_align(col, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(col, 6, 0);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);
    scrollbar_style(col, lv_color_white());

    o = lv_label_create(col);
    lv_obj_set_style_text_font(o, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(o, lv_color_white(), 0);
    lv_label_set_text(o, e->name);

    *focus = NULL;
    for (k = 1; k <= e->nsub; k++) {
        b = lv_btn_create(col);
        lv_obj_remove_style_all(b);
        lv_obj_add_style(b, &st_pop_btn, 0);
        lv_obj_add_style(b, &st_pop_focus, LV_STATE_FOCUSED);
        lv_obj_add_style(b, &st_pop_press, LV_STATE_PRESSED);
        lv_obj_set_size(b, LV_PCT(100), 38);
        lv_obj_add_event_cb(b, option_event, LV_EVENT_CLICKED, (void *)(long)k);
        o = lv_label_create(b);
        lv_snprintf(m, sizeof(m), "%d    %s", k, e->sub_name[k][0] ? e->sub_name[k] : "(no name)");
        lv_label_set_text(o, m);
        lv_obj_set_style_text_color(o, lv_color_white(), 0);
        lv_obj_align(o, LV_ALIGN_LEFT_MID, 0, 0);
        if (k == default_option(e)) *focus = b;
    }

    o = lv_label_create(col);
    lv_obj_set_style_text_color(o, C_POP_TEXT2, 0);
    lv_snprintf(m, sizeof(m), "1-%d  start      Enter  start      Esc  back", e->nsub);
    lv_label_set_text(o, m);

    lv_group_set_default(main_group);
    return panel;
}

#ifndef POPUP_DIM
static void build_popup(int i) {
    if (pre_panel[i]) return;
    pre_group[i] = lv_group_create();
    pre_panel[i] = popup_panel(i, lv_layer_top(), pre_group[i], &pre_focus[i]);
    lv_obj_add_flag(pre_panel[i], LV_OBJ_FLAG_HIDDEN);
}
#endif

static void open_popup(int i) {
    boot_entry_t * e = &cfg_entries[i];
    lv_obj_t * focus;

    popup_entry = i;
#ifdef POPUP_DIM
    /* the menu dimmed behind the panel: a see-through black layer over the whole screen. Every open and close then
       redraws and blends all 307 200 pixels in software (about 0.4 s with the data cache on). */
    popup_group = lv_group_create();
    popup = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(popup);
    lv_obj_set_size(popup, VIDEO_W, VIDEO_H);
    lv_obj_set_style_bg_color(popup, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(popup, LV_OPA_60, 0);
    lv_obj_add_flag(popup, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(popup, overlay_event, LV_EVENT_CLICKED, NULL);
    popup_panel(i, popup, popup_group, &focus);
#else
    /* no dimming layer: the panel sits straight on the top layer, so opening and closing redraw only the panel's area.
       The (invisible) top layer catches the clicks outside the panel while the pop-up is open. */
    build_popup(i);
    popup = pre_panel[i];
    popup_group = pre_group[i];
    focus = pre_focus[i];
    lv_obj_add_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lv_layer_top(), overlay_event, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_HIDDEN);
#endif
    lv_indev_set_group(input_keypad(), popup_group);
    if (focus) lv_group_focus_obj(focus);
    dbg_printf("menu: pop-up for entry %d (%d options, default %d)\n", i, e->nsub, default_option(e));
}

/* an entry was chosen (key, Enter or click): its pop-up if it has options, else start it */
static void choose_entry(int i) {
    boot_entry_t * e = &cfg_entries[i];
    if (i < 1 || i > CFG_MAX_ENTRIES || !e->used) { set_msg("Entry %d does not exist.%s", i, ""); return; }
    if (flash_blank(e->addr)) { set_msg("Entry %d (%s) is empty: its flash block is blank.", i, e->name); return; }
    if (e->nsub > 0) open_popup(i);
    else start_entry(i, 0);
}

static void entry_event(lv_event_t * ev) {
    choose_entry((int)(long)lv_event_get_user_data(ev));
}

static void button_style(lv_style_t * s, lv_color_t bg) {
    lv_style_init(s);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
    lv_style_set_bg_color(s, bg);
    lv_style_set_radius(s, 6);
    lv_style_set_pad_hor(s, 14);
    lv_style_set_pad_ver(s, 4);
    lv_style_set_pad_column(s, 14);
    lv_style_set_border_width(s, 2);
    lv_style_set_border_color(s, bg);
    lv_style_set_text_color(s, lv_color_white());
}

static void make_styles(void) {
    button_style(&st_btn, C_PANEL);
    lv_style_init(&st_btn_focus);
    lv_style_set_bg_color(&st_btn_focus, C_PANEL_FOCUS);
    lv_style_set_border_color(&st_btn_focus, C_ACCENT);
    lv_style_init(&st_btn_press);
    lv_style_set_bg_color(&st_btn_press, C_PRESS);

    button_style(&st_pop_btn, C_POP_BTN);
    lv_style_set_text_font(&st_pop_btn, &lv_font_montserrat_14);
    lv_style_init(&st_pop_focus);
    lv_style_set_bg_color(&st_pop_focus, C_POP_FOCUS);
    lv_style_set_border_color(&st_pop_focus, lv_color_white());
    lv_style_init(&st_pop_press);
    lv_style_set_bg_color(&st_pop_press, C_POP_PRESS);
}

static lv_obj_t * label(lv_obj_t * parent, const lv_font_t * font, lv_color_t col, const char * txt) {
    lv_obj_t * l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_label_set_text(l, txt);
    return l;
}

/* the screen without the entries: shown at once, before the SD card and the PS/2 devices are set up */
void menu_create(void) {
    lv_obj_t * scr = lv_scr_act();
    lv_obj_t * o;
    char m[96];

    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    make_styles();

    o = lv_img_create(scr);
    lv_img_set_src(o, true_color(logo_get(), logo_px, &logo_tc));
    lv_obj_align(o, LV_ALIGN_TOP_MID, 0, 8);

    lv_snprintf(m, sizeof(m), "%s   |   FPGA build %04lX   |   boot loader %s", MACHINE_NAME, (unsigned long)FPGA_BUILD, LOADER_VERSION);
    o = label(scr, &lv_font_montserrat_14, C_DIM, m);
    lv_obj_align(o, LV_ALIGN_TOP_MID, 0, 186);

    msg_label = label(scr, &lv_font_montserrat_14, C_WARN, "");
    lv_obj_set_width(msg_label, 600);
    lv_obj_set_style_text_align(msg_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msg_label, LV_ALIGN_TOP_MID, 0, 206);

    list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 480, 176);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 228);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_style_pad_all(list, 2, 0);
    lv_obj_set_style_pad_right(list, 10, 0);       /* room for the scrollbar */
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    scrollbar_style(list, C_ACCENT);                /* more than 4 entries: the list scrolls (focus follows) */

    bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 476, 6);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 412);
    lv_obj_set_style_bg_color(bar, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_anim_time(bar, 0, 0);
    lv_bar_set_range(bar, 0, 1000);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);

    status_label = label(scr, &lv_font_montserrat_14, lv_color_white(), "Reading boot.cfg ...");
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 424);

    o = label(scr, &lv_font_montserrat_14, C_DIM,
              "1-9  choose an entry      " LV_SYMBOL_UP " " LV_SYMBOL_DOWN "  select      Enter  start      Esc  stop the countdown");
    lv_obj_align(o, LV_ALIGN_BOTTOM_MID, 0, -10);
}

static void add_entry(int i) {
    boot_entry_t * e = &cfg_entries[i];
    lv_obj_t * b, * l;
    char m[80], opts[16];
    int blank = flash_blank(e->addr);

    b = lv_btn_create(list);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &st_btn, 0);
    lv_obj_add_style(b, &st_btn_focus, LV_STATE_FOCUSED);
    lv_obj_add_style(b, &st_btn_press, LV_STATE_PRESSED);
    lv_obj_set_size(b, LV_PCT(100), 40);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(b, entry_event, LV_EVENT_CLICKED, (void *)(long)i);
    buttons[i] = b;

    lv_snprintf(m, sizeof(m), "%d", i);
    label(b, &lv_font_montserrat_20, C_ACCENT, m);
    l = label(b, &lv_font_montserrat_20, blank ? C_DIM : lv_color_white(), e->name[0] ? e->name : "(no name)");
    lv_obj_set_flex_grow(l, 1);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    opts[0] = 0;
    if (e->nsub > 0) lv_snprintf(opts, sizeof(opts), "%d options   ", e->nsub);
    if (blank)
        lv_snprintf(m, sizeof(m), "empty");
    else
        lv_snprintf(m, sizeof(m), "%s%s$%08lX  %s", i == cfg_default ? "default   " : "", opts, (unsigned long)e->addr,
                    size_name(e->size));
    label(b, &lv_font_montserrat_14, blank ? C_WARN : C_DIM, m);
}

#ifdef POPUPBENCH
/* Pop-up timing test (make RAM=1 EXTRA=-DPOPUPBENCH): opens and closes the pop-up of the first entry that has options
   5 times, each time forcing the redraw (lv_refr_now), and redraws the whole screen once per round. The times (ms) go
   to SRAM $007F_BD00: 'BNCH', rounds, then per round { open, close, full screen }; the host reads them live over the
   debug port. */
extern uint32_t video_flush_ticks;
static void popup_bench(void) {
    volatile uint32_t * r = (volatile uint32_t *)0x007FBD00UL;
    int i, e = 0, k;
    uint32_t t0, t1, t2, t3;
    for (i = 1; i <= CFG_MAX_ENTRIES && !e; i++) if (cfg_entries[i].used && cfg_entries[i].nsub > 0) e = i;
    r[0] = 0; r[1] = 0;
    if (!e) { r[0] = 0x4E4F4E45UL; cache_push(); return; }   /* 'NONE': no entry with options */
    lv_refr_now(NULL);
    for (k = 0; k < 5; k++) {
        t0 = board_ms();
        open_popup(e);  r[23] = board_ms() - t0;  lv_refr_now(NULL);  t1 = board_ms();      /* r[23]: building the objects */
        close_popup();  lv_refr_now(NULL);  t2 = board_ms();
        video_flush_ticks = 0;
        lv_obj_invalidate(lv_scr_act());  lv_refr_now(NULL);  t3 = board_ms();
        r[2 + 3 * k] = t1 - t0;  r[3 + 3 * k] = t2 - t1;  r[4 + 3 * k] = t3 - t2;
        r[18 + k] = video_flush_ticks / board_ticks_per_ms();          /* ms of the full-screen redraw spent in flush */
        r[1] = k + 1;
    }
    r[0] = 0x424E4348UL;                                    /* 'BNCH' */
    cache_push();
    open_popup(e);                                          /* left open for a screen capture */
}
#endif

void menu_run(void) {
    uint32_t info = BOOT_INFO, act, last_shown = 0xFFFFFFFFUL;
    int i, first = 0;
    char m[96];

    main_group = lv_group_get_default();
    cfg_load();
    for (i = 1; i <= CFG_MAX_ENTRIES; i++) {
        if (!cfg_entries[i].used) continue;
        add_entry(i);
        if (!first && !flash_blank(cfg_entries[i].addr)) first = i;
#ifndef POPUP_DIM
        if (cfg_entries[i].nsub > 0) build_popup(i);        /* built once, hidden (lvboot 1.1) */
#endif
    }
    if (cfg_count() == 0) label(list, &lv_font_montserrat_14, C_DIM, "No flash block holds a program.");

    have_default = cfg_default >= 1 && cfg_default <= CFG_MAX_ENTRIES && cfg_entries[cfg_default].used &&
                   !flash_blank(cfg_entries[cfg_default].addr);
    if (have_default) lv_group_focus_obj(buttons[cfg_default]);
    else if (first) lv_group_focus_obj(buttons[first]);

    if (cfg_state == CFG_NO_CARD) set_msg("No hard drive or SD card volume: the flash blocks found are listed.%s", 0, "");
    else if (cfg_state == CFG_NO_FILE) set_msg("No boot.cfg on the hard drive or the SD card: the flash blocks found are listed.%s", 0, "");
    else if (cfg_error[0]) set_msg("%s%s", 0, cfg_error);
    if ((info & 0xC0) == 0xC0) {
        set_msg("The last choice could not be started (its flash block is blank): choose again.%s", 0, "");
        have_default = 0;
    }

    counting = have_default && cfg_timeout > 0;
    if (counting) {
        count_len = (uint32_t)cfg_timeout * 1000;
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        counting = 1;
        stop_countdown();
    }
    count_start = board_ms();
    act = input_activity();
#ifdef POPUPBENCH
    stop_countdown();
    popup_bench();
#endif

    for (;;) {
        uint32_t k;
        input_poll();
        while ((k = input_key()) != 0) {
            stop_countdown();
            if (popup) {                                            /* the pop-up has the keys */
                const boot_entry_t * e = &cfg_entries[popup_entry];
                if (k >= '1' && k <= '9' && (int)(k - '0') <= e->nsub) start_entry(popup_entry, (int)(k - '0'));
                else if (k == LV_KEY_ESC) close_popup();
                else if (k == LV_KEY_UP || k == LV_KEY_LEFT) input_lv_key(LV_KEY_PREV);
                else if (k == LV_KEY_DOWN || k == LV_KEY_RIGHT || k == LV_KEY_NEXT) input_lv_key(LV_KEY_NEXT);
                else if (k == LV_KEY_ENTER) input_lv_key(LV_KEY_ENTER);
                continue;
            }
            if (k >= '1' && k <= '9') choose_entry((int)(k - '0'));
            else if (k == LV_KEY_UP || k == LV_KEY_LEFT) input_lv_key(LV_KEY_PREV);
            else if (k == LV_KEY_DOWN || k == LV_KEY_RIGHT || k == LV_KEY_NEXT) input_lv_key(LV_KEY_NEXT);
            else if (k == LV_KEY_ENTER) input_lv_key(LV_KEY_ENTER);
        }
        if (counting) {
            uint32_t el = board_ms() - count_start, left;
            const boot_entry_t * e = &cfg_entries[cfg_default];
            if (input_activity() != act) stop_countdown();          /* the mouse moved or clicked */
            else if (el >= count_len) start_entry(cfg_default, default_option(e));
            else {
                left = (count_len - el + 999) / 1000;
                lv_bar_set_value(bar, (int32_t)(1000 - el * 1000 / count_len), LV_ANIM_OFF);
                if (left != last_shown) {
                    last_shown = left;
                    lv_snprintf(m, sizeof(m), "Starting %s in %lu s - press any key to stop.",
                                start_name(e, default_option(e)), (unsigned long)left);
                    lv_label_set_text(status_label, m);
                }
            }
        }
        lv_timer_handler();
    }
}
