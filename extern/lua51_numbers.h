// © Joseph Cameron - MIT License
//
// forced into lobject.c and lvm.c, the two files that turn numbers into text and back. puc lua does
// both with strtod and sprintf, which take the c locale's decimal point: under one with a comma,
// tostring(0.5) is "0,5" and tonumber("0.5") is nil. these write and read a point whatever the locale.

#ifndef JFC_LUA51_NUMBERS_H
#define JFC_LUA51_NUMBERS_H

// both files are core, and luaconf.h only defines the core's number macros for those that say so
#define LUA_CORE
#include "luaconf.h"

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace jfc_lua51 {
    inline const char *foreign_point() {
        const char *const point = std::localeconv()->decimal_point;

        return point && *point && std::strcmp(point, ".") != 0 ? point : nullptr;
    }

    inline void number_to_string(char *const aOut, const double aNumber) {
        std::snprintf(aOut, LUAI_MAXNUMBER2STR, LUA_NUMBER_FMT, aNumber);

        const char *const point = foreign_point();

        if (!point) return;

        char *const at = std::strstr(aOut, point);

        if (!at) return;

        const std::size_t length = std::strlen(point);

        *at = '.';

        std::memmove(at + 1, at + length, std::strlen(at + length) + 1);
    }

    inline double string_to_number(const char *const aText, char **const aEnd) {
        const char *const point = foreign_point();

        if (!point) return std::strtod(aText, aEnd);

        std::string spelt;

        const char *end = aText;

        for (; *end && *end != *point; ++end) {
            if (*end == '.') spelt += point;
            else spelt += *end;
        }

        char *spentEnd = nullptr;

        const double number = std::strtod(spelt.c_str(), &spentEnd);

        std::size_t spent = static_cast<std::size_t>(spentEnd - spelt.c_str());

        const std::size_t length = std::strlen(point);

        const char *at = aText;

        for (; at < end; ++at) {
            const std::size_t step = *at == '.' ? length : 1;

            if (spent < step) break;

            spent -= step;
        }

        *aEnd = const_cast<char *>(at);

        return number;
    }
}

#undef lua_number2str
#undef lua_str2number

#define lua_number2str(s,n) jfc_lua51::number_to_string((s), (n))
#define lua_str2number(s,p) jfc_lua51::string_to_number((s), (p))

#endif
