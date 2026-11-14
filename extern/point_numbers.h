// © Joseph Cameron - MIT License
//
// numbers read as lua spells them, with a point, whatever the c locale's decimal point: strtod takes the locale's, so
// under one with a comma it reads "0.5" as 0. for the backends that read numbers with strtod.

#ifndef JFC_LUA_POINT_NUMBERS_H
#define JFC_LUA_POINT_NUMBERS_H

#include <clocale>
#include <cstdlib>
#include <cstring>
#include <string>

namespace jfc_lua_point {
    //! the c locale's decimal point where it is not a point, else null
    inline const char *foreign_point() {
        const char *const point = std::localeconv()->decimal_point;

        return point && *point && std::strcmp(point, ".") != 0 ? point : nullptr;
    }

    //! aText's number as strtod reads it under the c locale, aEnd where it stopped
    inline double read_number(const char *const aText, char **const aEnd) {
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

        if (aEnd) *aEnd = const_cast<char *>(at);

        return number;
    }
}

#endif
