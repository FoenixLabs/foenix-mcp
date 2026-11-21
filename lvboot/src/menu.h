#ifndef MENU_H
#define MENU_H
#include <stdint.h>

#define LOADER_VERSION  "1.4"   /* 1.3: entries show the MCP version, "c" = caches on; 1.4: the name a block gives itself */

uint16_t menu_bg565(void);      /* the background colour (RGB565) for the frame buffer clear */
void     menu_create(void);     /* logo, labels, empty list: shown before the slow set-up */
void     menu_run(void);        /* loads boot.cfg, fills the list, counts down, starts an entry; never returns */

#endif
