/*
 * Version numbers for the kernel
 */

#ifndef __VERSION_H
#define __VERSION_H

#define VER_MAJOR 2
#define VER_MINOR 1
#define VER_BUILD 0

/* 2.0.30: shown after the version number when this MCP switches the CPU caches on (built with
   make ... CACHE=1). 2.1.0: "-cache" instead of "c", the same as the release file names; the boot
   loader (lvboot 1.4) shows it too ("MCP 2.1.0-cache"). */
#ifdef CACHE_ON
#define VER_SUFFIX "-cache"
#else
#define VER_SUFFIX ""
#endif

#endif
