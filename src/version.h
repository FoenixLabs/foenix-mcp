/*
 * Version numbers for the kernel
 */

#ifndef __VERSION_H
#define __VERSION_H

#define VER_MAJOR 2
#define VER_MINOR 0
#define VER_BUILD 33

/* 2.0.30: shown after the version number. "c" = this MCP switches the CPU caches on (built with
   make ... CACHE=1); the boot loader shows it too ("MCP 2.0.30c"). */
#ifdef CACHE_ON
#define VER_SUFFIX "c"
#else
#define VER_SUFFIX ""
#endif

#endif
