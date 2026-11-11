# Local changes to LVGL 8.3.11 (A2560Me boot loader)

LVGL is MIT licensed (see `LICENCE.txt`). The sources are the v8.3.11 release, with only these changes:

1. `src/misc/lv_color.h`: `lv_color16_t` lists its bit-fields red, green, blue when `LV_BIG_ENDIAN_SYSTEM` is 1.
   VBCC (like GCC on m68k) fills bit-fields from the top bit. Without this, `full` would hold blue in bits 15:11 and
   the GUI engine (RGB565) would show red and blue swapped. `LV_COLOR_MAKE16` (a positional initialiser) is
   changed to the same order, or every colour would come out with red and blue swapped.
2. `src/core/lv_indev.c` (`indev_gesture`) and `src/core/lv_refr.c` (`refr_obj`): `static` added to the definitions.
   They are declared `static` earlier in the file; VBCC rejects a definition without it ("redeclaration with new
   storage-class").

VBCC note: compile with backslash paths (`lvgl\src\core\lv_obj.c`). VBCC finds `#include "..."` files relative to the
source file only when the path uses `\`.
