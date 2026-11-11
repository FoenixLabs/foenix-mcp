#ifndef VIDEO_H_INCLUDED
#define VIDEO_H_INCLUDED
#include <stdint.h>

#define VIDEO_W 640
#define VIDEO_H 480

void video_init(uint16_t bg565);    /* GUI engine on, frame buffer cleared to bg565, LVGL display registered */
void video_off(void);

#endif
