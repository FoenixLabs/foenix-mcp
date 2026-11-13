/*
 * bootcfg.c - Gen1 LVGL boot loader: BOOT.CFG on the SD card (/sd in the MCP), the same file and format as the
 * A2560Me loader.
 *
 *     # comment                               ('#' starts a comment, also after a value)
 *     TIMEOUT = 3                             seconds, 0 = wait for a key
 *     DEFAULT = 1                             the last choice, written back when another entry is booted
 *     1 = $FFC80000  512K  "Foenix/MCP"       entry: flash address, block size (512K, 1M, 2M, 4M), name
 *     3.1 = "VioletBSD (Single User)"         option k of entry n: choosing the entry opens a pop-up
 *     3.DEFAULT = 1                           the option chosen last time, written back like DEFAULT
 *
 * The address must be a multiple of the size and not block 0 (this loader). Without a card, a file or a usable entry,
 * the non-blank 512 KB blocks 1-7 are listed instead, and the first of them starts after 5 s (Gen1).
 */
#include <string.h>
#include <ctype.h>
#include "lvgl.h"
#include "ff.h"
#include "hw.h"
#include "board.h"
#include "bootcfg.h"

#define CFG_NAME    "BOOT.CFG"
#define CFG_MAX     4096

boot_entry_t cfg_entries[CFG_MAX_ENTRIES + 1];
int cfg_timeout = 3;
int cfg_default = 0;
int cfg_state = CFG_NO_CARD;
char cfg_error[80];

static FATFS fs_sd, fs_hd;              /* 11-07: "0:" SD card, "1:" IDE drive */
static char cfg_path[16] = "0:BOOT.CFG";  /* the file that was read (DEFAULT is written back there) */
const char * cfg_source = "";           /* "hard drive" / "SD card" (menu text) */
static char text[CFG_MAX + 1];
static UINT text_len;

int flash_blank(uint32_t addr) { return *(volatile uint32_t *)addr == 0xFFFFFFFFUL; }

int layout_of(uint32_t size) {
    switch (size) {
        case 0x400000UL: return 0;
        case 0x200000UL: return 1;
        case 0x100000UL: return 2;
        case 0x080000UL: return 3;
        default:         return -1;
    }
}

const char * size_name(uint32_t size) {
    switch (size) {
        case 0x400000UL: return "4 MB";
        case 0x200000UL: return "2 MB";
        case 0x100000UL: return "1 MB";
        default:         return "512 KB";
    }
}

static const char * skip_ws(const char * p) { while (*p == ' ' || *p == '\t') p++; return p; }

static int parse_number(const char ** pp, uint32_t * v, int hex_default) {
    const char * p = skip_ws(*pp);
    uint32_t base = hex_default ? 16 : 10, x = 0;
    int n = 0;
    if (*p == '$') { base = 16; p++; }
    else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
    for (;;) {
        int c = toupper((unsigned char)*p), d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        x = x * base + (uint32_t)d; p++; n++;
    }
    *pp = p; *v = x;
    return n > 0;
}

static int parse_size(const char ** pp, uint32_t * v) {
    uint32_t n;
    const char * p;
    if (!parse_number(pp, &n, 0)) return 0;
    p = *pp;
    if (toupper((unsigned char)*p) == 'K') { n <<= 10; p++; }
    else if (toupper((unsigned char)*p) == 'M') { n <<= 20; p++; }
    *pp = p; *v = n;
    return layout_of(n) >= 0;
}

/* one line without its comment; returns 0 = fine or empty, else an error text */
static const char * parse_line(const char * line) {
    const char * p = skip_ws(line);
    uint32_t v, a, s;
    char word[16];
    int n = 0;
    if (*p == 0) return 0;
    if (p[0] >= '1' && p[0] <= '9' && p[1] == '.') {       /* n.k = "name" / n.DEFAULT = k: the entry's pop-up */
        boot_entry_t * e = &cfg_entries[p[0] - '0'];
        p += 2;
        while (isalnum((unsigned char)*p) && n < 15) word[n++] = (char)toupper((unsigned char)*p++);
        word[n] = 0;
        p = skip_ws(p);
        if (*p != '=') return "'=' expected";
        p = skip_ws(p + 1);
        if (!strcmp(word, "DEFAULT")) {
            if (!parse_number(&p, &v, 0) || v < 1 || v > CFG_MAX_SUB) return "n.DEFAULT must be 1-9";
            e->sub_default = (int)v;
            return 0;
        }
        if (n == 1 && word[0] >= '1' && word[0] <= '9') {
            int k = word[0] - '0';
            char * q = e->sub_name[k];
            if (*p != '"') return "a name in quotes expected";
            p++;
            while (*p && *p != '"' && q < e->sub_name[k] + sizeof(e->sub_name[k]) - 1) *q++ = *p++;
            *q = 0;
            if (k > e->nsub) e->nsub = k;
            return 0;
        }
        return "unknown setting";
    }
    while (isalnum((unsigned char)*p) && n < 15) word[n++] = (char)toupper((unsigned char)*p++);
    word[n] = 0;
    p = skip_ws(p);
    if (*p != '=') return "'=' expected";
    p++;
    if (!strcmp(word, "TIMEOUT")) {
        if (!parse_number(&p, &v, 0) || v > 99) return "TIMEOUT must be 0-99";
        cfg_timeout = (int)v;
        return 0;
    }
    if (!strcmp(word, "DEFAULT")) {
        if (!parse_number(&p, &v, 0) || v < 1 || v > CFG_MAX_ENTRIES) return "DEFAULT must be 1-9";
        cfg_default = (int)v;
        return 0;
    }
    if (n == 1 && word[0] >= '1' && word[0] <= '9') {
        boot_entry_t * e = &cfg_entries[word[0] - '0'];
        char * q;
        if (!parse_number(&p, &a, 1)) return "address expected";
        if (!parse_size(&p, &s)) return "size must be 512K, 1M, 2M or 4M";
        if (a < FLASH_BASE || a - FLASH_BASE + s > FLASH_SIZE || ((a - FLASH_BASE) & (s - 1)))
            return "address outside the flash or not a multiple of the size";
        if (a == FLASH_BASE) return "block 0 is the boot loader itself";
        p = skip_ws(p);
        e->used = 1;
        e->addr = a;
        e->size = s;
        e->name[0] = 0;
        if (*p == '"') {
            p++;
            q = e->name;
            while (*p && *p != '"' && q < e->name + sizeof(e->name) - 1) *q++ = *p++;
            *q = 0;
        }
        return 0;
    }
    return "unknown setting";
}

/*
 * mcp_label - lvboot 1.3 (2026-11-13): name an entry after the MCP that is in its flash block.
 *
 * Input:  e = a used entry
 * Output: none; e->name becomes for example "MCP 2.0.30 caches ON" when the block holds an MCP info record
 *
 * MCP 2.0.30 and later carry an info record in their vector table (vectors 16-19, offset $40 of the block, never
 * used by the CPU): "MCPI", the version (major << 24 | minor << 16 | build), flags (bit 0 = the CPU caches are on)
 * and the three XOR-ed as a check. The record replaces the BOOT.CFG name, so the menu always shows which MCP is
 * really in the flash. Blocks without a valid record (older MCPs, EmuTOS, ...) keep their BOOT.CFG name.
 */
#define MCP_INFO_OFFSET 0x40UL
#define MCP_INFO_MAGIC  0x4D435049UL            /* "MCPI" */

static void mcp_label(boot_entry_t * e) {
    const volatile uint32_t * r = (const volatile uint32_t *)(e->addr + MCP_INFO_OFFSET);
    uint32_t magic = r[0], ver = r[1], flags = r[2], check = r[3];

    if (magic != MCP_INFO_MAGIC || (magic ^ ver ^ flags) != check) return;
    lv_snprintf(e->name, sizeof(e->name), "MCP %d.%d.%d caches %s", (int)(ver >> 24), (int)((ver >> 16) & 0xFF),
                (int)(ver & 0xFFFF), (flags & 1) ? "ON" : "OFF");
    dbg_printf("bootcfg: block at $%08lX holds %s\n", (unsigned long)e->addr, e->name);
}

static void scan_flash(void) {
    int b;
    for (b = 1; b < 8; b++) {
        uint32_t a = FLASH_BASE + (uint32_t)b * 0x80000UL;
        if (flash_blank(a)) continue;
        cfg_entries[b].used = 1;
        cfg_entries[b].addr = a;
        cfg_entries[b].size = 0x80000UL;
        lv_snprintf(cfg_entries[b].name, sizeof(cfg_entries[b].name), "Flash block %d", b);
    }
}

int cfg_count(void) {
    int i, n = 0;
    for (i = 1; i <= CFG_MAX_ENTRIES; i++) if (cfg_entries[i].used) n++;
    return n;
}

void cfg_load(void) {
    FIL f;
    FRESULT r;
    const char * p;
    char line[160];
    int line_no = 1, tries;

    memset(cfg_entries, 0, sizeof(cfg_entries));
    cfg_error[0] = 0;
    text_len = 0;
    /* 11-07 (owner): the fixed drive first (IDE, first FAT partition), then the SD card, then the flash list */
    {
        FRESULT rh = f_mount(&fs_hd, "1:", 1);
        FRESULT rs;
        cfg_state = CFG_NO_CARD;
        r = FR_NO_FILE;
        if (rh == FR_OK) {
            cfg_state = CFG_NO_FILE;
            r = f_open(&f, "1:" CFG_NAME, FA_READ);
            dbg_printf("bootcfg: hard drive mounted, %s %s\n", CFG_NAME, r == FR_OK ? "found" : "not there");
            if (r == FR_OK) { strcpy(cfg_path, "1:" CFG_NAME); cfg_source = "hard drive"; }
        } else dbg_printf("bootcfg: no hard drive volume (FatFs %d)\n", rh);
        if (r != FR_OK) {
            rs = f_mount(&fs_sd, "0:", 1);
            for (tries = 1; rs != FR_OK && tries < 3; tries++) { delay_ms(300); rs = f_mount(&fs_sd, "0:", 1); }
            if (rs == FR_OK) {
                cfg_state = CFG_NO_FILE;
                r = f_open(&f, "0:" CFG_NAME, FA_READ);
                dbg_printf("bootcfg: SD card mounted, %s %s\n", CFG_NAME, r == FR_OK ? "found" : "not there");
                if (r == FR_OK) { strcpy(cfg_path, "0:" CFG_NAME); cfg_source = "SD card"; }
            } else dbg_printf("bootcfg: no SD card volume (FatFs %d)\n", rs);
        }
    }
    if (r != FR_OK) {
        /* cfg_state: CFG_NO_CARD (neither volume mounted) or CFG_NO_FILE (mounted, no BOOT.CFG) */
    } else {
        f_read(&f, text, CFG_MAX, &text_len);
        f_close(&f);
        text[text_len] = 0;
        cfg_state = CFG_LOADED;
        dbg_printf("bootcfg: ---- %s (%s) ----\n", cfg_path, cfg_source);
        dbg_puts(text);
        dbg_puts("\nbootcfg: ---- end ----\n");
        p = text;
        while (*p) {
            const char * err;
            int n = 0, quote = 0;
            while (*p && *p != '\n' && *p != '\r') {
                if (*p == '"') quote = !quote;
                if (*p == '#' && !quote) { while (*p && *p != '\n' && *p != '\r') p++; break; }
                if (n < (int)sizeof(line) - 1) line[n++] = *p;
                p++;
            }
            line[n] = 0;
            if ((err = parse_line(line)) != 0 && !cfg_error[0])
                lv_snprintf(cfg_error, sizeof(cfg_error), "boot.cfg line %d: %s", line_no, err);
            if (*p == '\r') p++;
            if (*p == '\n') p++;
            line_no++;
        }
        dbg_printf("bootcfg: %d bytes, %d entries, TIMEOUT %d, DEFAULT %d %s\n", (int)text_len, cfg_count(), cfg_timeout,
                   cfg_default, cfg_error);
    }
    if (cfg_count() == 0) {
        int k;
        scan_flash();
        /* Gen1: nothing configured (no card, no file): count down to the first program found (block 1 = the MCP),
           so the machine still starts without an SD card; any key stops the countdown (A2560Me: wait for a key) */
        cfg_default = 0;
        for (k = 1; k <= CFG_MAX_ENTRIES && !cfg_default; k++) if (cfg_entries[k].used) cfg_default = k;
        cfg_timeout = cfg_default ? 5 : 0;
    }
    /* lvboot 1.3: entries whose block holds an MCP with an info record show its version and cache setting */
    {
        int k;
        for (k = 1; k <= CFG_MAX_ENTRIES; k++)
            if (cfg_entries[k].used && !flash_blank(cfg_entries[k].addr)) mcp_label(&cfg_entries[k]);
    }
}

/* does the line start with KEY (case ignored) followed by a blank or '='? */
static int key_at(const char * q, const char * key) {
    while (*key) if (toupper((unsigned char)*q++) != *key++) return 0;
    return !isalnum((unsigned char)*q) && *q != '.';
}

/* Write DEFAULT = d (and, if ent > 0, ent.DEFAULT = sub) back into the file. Everything else stays as it is; a
   comment after the value keeps its column. A key that is not in the file is added at the end. */
int cfg_save(int d, int ent, int sub) {
    static char out[CFG_MAX + 128];
    const char * p = text;
    char * o = out;
    char key2[12];
    int done1 = 0, done2 = (ent <= 0), tries;
    FIL f;
    UINT bw;
    if (cfg_state != CFG_LOADED) return 0;
    lv_snprintf(key2, sizeof(key2), "%d.DEFAULT", ent);
    while (*p) {
        const char * line_start = p;
        char * out_line = o;
        const char * q = skip_ws(p);
        const char * key = 0;
        int val = 0;
        if (!done1 && key_at(q, "DEFAULT")) { key = "DEFAULT"; val = d; done1 = 1; }
        else if (!done2 && key_at(q, key2)) { key = key2; val = sub; done2 = 1; }
        if (key) {
            const char * rest = q;
            while (*rest && *rest != '\n' && *rest != '\r' && *rest != '#') rest++;
            while (p < q) *o++ = *p++;                  /* leading blanks */
            o += lv_snprintf(o, 24, "%s = %d", key, val);
            if (*rest == '#') {                         /* the comment stays in its column (at least one blank) */
                do *o++ = ' '; while (o - out_line < rest - line_start);
                while (*rest && *rest != '\n' && *rest != '\r') *o++ = *rest++;
            }
            p = rest;
            while (*p && *p != '\n' && *p != '\r') p++;
        } else {
            while (*p && *p != '\n' && *p != '\r') *o++ = *p++;
        }
        if (*p == '\r') *o++ = *p++;
        if (*p == '\n') *o++ = *p++;
    }
    if (o > out && o[-1] != '\n' && (!done1 || !done2)) *o++ = '\n';
    if (!done1) o += lv_snprintf(o, 32, "DEFAULT = %d\n", d);
    if (!done2) o += lv_snprintf(o, 32, "%s = %d\n", key2, sub);
    /* write, read back, compare: up to 3 times */
    for (tries = 1; tries <= 3; tries++) {
        UINT br = 0;
        if (f_open(&f, cfg_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) continue;
        f_write(&f, out, (UINT)(o - out), &bw);
        f_close(&f);
        if (f_open(&f, cfg_path, FA_READ) != FR_OK) continue;
        f_read(&f, text, CFG_MAX, &br);
        f_close(&f);
        if (bw == (UINT)(o - out) && br == bw && !memcmp(text, out, br)) {
            dbg_printf("bootcfg: DEFAULT = %d, %s = %d written and verified (%d bytes, try %d)\n", d, key2, sub, (int)bw, tries);
            return 1;
        }
    }
    dbg_printf("bootcfg: DEFAULT = %d could NOT be written correctly\n", d);
    return 0;
}
