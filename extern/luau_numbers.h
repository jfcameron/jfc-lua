// © Joseph Cameron - MIT License
//
// forced into Luau's lobject.cpp and Parser.cpp, the two files that read numbers, both with strtod: under a locale
// with a comma, tonumber("0.5") is nil and a script spelling 0.5 does not compile. these read a point whatever the
// locale. Luau writes numbers itself, with a point.

#ifndef JFC_LUAU_NUMBERS_H
#define JFC_LUAU_NUMBERS_H

#include "point_numbers.h"

// the c library's headers are in before this, so only the two files' own calls are replaced
#define strtod(s, p) jfc_lua_point::read_number((s), (p))

#endif
