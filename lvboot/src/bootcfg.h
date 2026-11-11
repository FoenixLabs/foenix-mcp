#ifndef BOOTCFG_H
#define BOOTCFG_H
#include <stdint.h>

#define CFG_MAX_ENTRIES 9
#define CFG_MAX_SUB     9

typedef struct {
    int      used;
    uint32_t addr;          /* CPU address of the flash block */
    uint32_t size;          /* block size: $8_0000, $10_0000, $20_0000 or $40_0000 */
    char     name[40];
    int      nsub;          /* options of the entry's pop-up (n.1 ... n.9), 0 = no pop-up */
    int      sub_default;   /* n.DEFAULT: the option chosen last time */
    char     sub_name[CFG_MAX_SUB + 1][40];     /* index 1..nsub */
} boot_entry_t;

enum { CFG_NO_CARD, CFG_NO_FILE, CFG_LOADED };

extern boot_entry_t cfg_entries[CFG_MAX_ENTRIES + 1];   /* index 1..9 */
extern int  cfg_timeout;    /* seconds, 0 = wait */
extern int  cfg_default;    /* 0 = none */
extern int  cfg_state;      /* CFG_NO_CARD, CFG_NO_FILE or CFG_LOADED */
extern char cfg_error[80];  /* first parse error, "" if none */
extern const char * cfg_source;  /* 11-07: "hard drive" / "SD card" - where BOOT.CFG came from */

void cfg_load(void);
int  cfg_count(void);
int  cfg_save(int d, int ent, int sub);         /* DEFAULT = d, and ent.DEFAULT = sub when ent > 0 */
int  flash_blank(uint32_t addr);
int  layout_of(uint32_t size);
const char * size_name(uint32_t size);

/*
 * Hand-over to the started program: written to SRAM just before the reboot (SRAM keeps its contents through the
 * BOOT_SEL reboot; the FPGA's boot copy only rewrites $0000_0000-$0000_FFFF). A program that has options (for example
 * VioletBSD) reads it at start-up; it is valid only if magic and check match.
 */
#define HANDOVER_ADDR   0x003FBF00UL            /* Gen1: SRAM is 4 MB (A2560Me: $007F_BF00) */
#define HANDOVER_MAGIC  0x4C564254UL            /* "LVBT" */
typedef struct {
    uint32_t magic;         /* HANDOVER_MAGIC */
    uint32_t entry;         /* boot.cfg entry number, 1-9 */
    uint32_t option;        /* pop-up option, 1-9; 0 = the entry has no options */
    uint32_t flash_addr;    /* the block that is started */
    char     entry_name[40];
    char     option_name[40];
    uint32_t check;         /* magic ^ entry ^ option ^ flash_addr */
} handover_t;

#endif
