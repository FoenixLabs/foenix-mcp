#ifndef INPUT_H
#define INPUT_H
#include <stdint.h>
#include "lvgl.h"

void        input_init(void);           /* PS/2 controller, keyboard and mouse */
lv_group_t * input_lv_init(void);       /* LVGL keypad (+ pointer with a cursor if a mouse answered); the focus group */
void        input_poll(void);           /* call often: reads PS/2 and the console */
uint32_t    input_key(void);            /* next key: '1'-'9', LV_KEY_*, ' ' or '?'; 0 if none */
void        input_lv_key(uint32_t k);   /* hand a navigation key to LVGL's keypad */
lv_indev_t * input_keypad(void);       /* the keypad (to move it to another focus group) */
uint32_t    input_activity(void);       /* counts keys, mouse moves and clicks */

#endif
