# the LVGL 8.3 sources this loader compiles (everything else is switched off in lv_conf.h)
find lvgl/src/core lvgl/src/hal lvgl/src/misc lvgl/src/widgets -name "*.c"
ls lvgl/src/draw/*.c lvgl/src/draw/sw/*.c
ls lvgl/src/font/lv_font.c lvgl/src/font/lv_font_fmt_txt.c lvgl/src/font/lv_font_loader.c \
   lvgl/src/font/lv_font_montserrat_14.c lvgl/src/font/lv_font_montserrat_20.c
ls lvgl/src/extra/lv_extra.c lvgl/src/extra/layouts/flex/*.c lvgl/src/extra/themes/default/*.c
