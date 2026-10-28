// © Joseph Cameron - MIT License
//
// forced into loslib.c on Apple's devices, whose sdk makes system() unavailable, so os.execute
// reports that there is no shell.

#ifndef JFC_LUA51_APPLE_DEVICE_H
#define JFC_LUA51_APPLE_DEVICE_H

#include <stdlib.h>

#define system(command) ((command) ? -1 : 0)

#endif
