// © Joseph Cameron - All Rights Reserved

#include <jfc/lua_exception.h>
#include <jfc/lua_key.h>
#include <jfc/lua_path.h>

#include <cstdlib>
#include <utility>
#include <ostream>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace
{
    constexpr const char *DELIMITERS = ".[]'\"";

    [[nodiscard]] jfc::lua_exception _malformed(const std::string &aPath, const std::string &aWhy) {
        return jfc::lua_exception("could not parse the path \"" + aPath + "\": " + aWhy);
    }

    [[nodiscard]] std::string _parse_quoted(const std::string &aPath, std::size_t &aIndex) {
        const char quote = aPath[aIndex++];

        std::string out;

        while (aIndex < aPath.size() && aPath[aIndex] != quote) {
            if (aPath[aIndex] == '\\' && aIndex + 1 < aPath.size()) ++aIndex;

            out += aPath[aIndex++];
        }

        if (aIndex >= aPath.size()) throw _malformed(aPath, "a quoted key is never closed");

        ++aIndex;

        return out;
    }

    [[nodiscard]] jfc::lua::key _parse_bracket(const std::string &aPath, std::size_t &aIndex) {
        ++aIndex;

        if (aIndex >= aPath.size()) throw _malformed(aPath, "a bracket is never closed");

        if (aPath[aIndex] == ']') throw _malformed(aPath, "a bracket contains no key");

        jfc::lua::key parsed = 0.0;

        if (aPath[aIndex] == '\'' || aPath[aIndex] == '"') {
            parsed = _parse_quoted(aPath, aIndex);
        }
        else {
            const std::size_t start = aIndex;

            while (aIndex < aPath.size() && aPath[aIndex] != ']') ++aIndex;

            if (aIndex >= aPath.size()) throw _malformed(aPath, "a bracket is never closed");

            const std::string token = aPath.substr(start, aIndex - start);

            if (token == "true") parsed = true;
            else if (token == "false") parsed = false;
            else {
                const char *const first = token.c_str();

                char *last = nullptr;

                const double number = std::strtod(first, &last);

                if (last != first + token.size() || token.empty())
                    throw _malformed(aPath, "\"" + token +
                        "\" is not a number, true, false, or a quoted string");

                parsed = number;
            }
        }

        if (aIndex >= aPath.size() || aPath[aIndex] != ']')
            throw _malformed(aPath, "a bracket is never closed");

        ++aIndex;

        return parsed;
    }

    [[nodiscard]] std::string _parse_name(const std::string &aPath, std::size_t &aIndex) {
        const std::size_t start = aIndex;

        const std::size_t end = aPath.find_first_of(DELIMITERS, start);

        aIndex = end == std::string::npos ? aPath.size() : end;

        if (aIndex == start) throw _malformed(aPath, "a name is empty");

        return aPath.substr(start, aIndex - start);
    }

    [[nodiscard]] std::vector<jfc::lua::key> _parse(const std::string &aPath) {
        if (aPath.empty()) throw _malformed(aPath, "it is empty");

        std::vector<jfc::lua::key> segments;

        std::size_t i = 0;

        if (aPath[i] == '[') segments.push_back(_parse_bracket(aPath, i));
        else segments.push_back(_parse_name(aPath, i));

        while (i < aPath.size()) {
            if (aPath[i] == '[') {
                segments.push_back(_parse_bracket(aPath, i));
            }
            else if (aPath[i] == '.') {
                ++i;

                segments.push_back(_parse_name(aPath, i));
            }
            else throw _malformed(aPath, std::string("unexpected '") + aPath[i] + "'");
        }

        return segments;
    }
}

namespace jfc::lua {
    path::path(std::initializer_list<key> aSegments)
    : m_Segments(aSegments)
    {}

    path::path(std::vector<key> aSegments)
    : m_Segments(std::move(aSegments))
    {}

    path::path(const std::string &aPathString)
    : m_Segments(_parse(aPathString))
    {}

    path::path(const char *const aPathString)
    : m_Segments(_parse(aPathString))
    {}

    std::size_t path::size() const { return m_Segments.size(); }

    bool path::empty() const { return m_Segments.empty(); }

    const key &path::operator[](const std::size_t aIndex) const { return m_Segments[aIndex]; }

    const std::vector<key> &path::segments() const { return m_Segments; }

    bool path::operator==(const path &a) const { return m_Segments == a.m_Segments; }

    bool path::operator!=(const path &a) const { return !(*this == a); }

    std::ostream &operator<<(std::ostream &out, const path &a)
    {
        std::stringstream stream;

        for (std::size_t i = 0; i < a.m_Segments.size(); ++i) {
            const auto &segment = a.m_Segments[i];

            if (segment.is_string()) {
                const auto &name = std::get<std::string>(segment.value());

                if (!name.empty() && name.find_first_of(DELIMITERS) == std::string::npos) {
                    if (i) stream << ".";

                    stream << name;
                }
                else {
                    stream << "['";

                    for (const char c : name)
                    {
                        if (c == '\'' || c == '\\') stream << '\\';

                        stream << c;
                    }

                    stream << "']";
                }
            }
            else if (segment.is_boolean()) {
                stream << "[" << (std::get<bool>(segment.value()) ? "true" : "false") << "]";
            }
            else stream << "[" << std::get<double>(segment.value()) << "]";
        }

        out << stream.str();

        return out;
    }
}
