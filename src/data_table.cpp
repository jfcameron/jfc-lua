// © Joseph Cameron - All Rights Reserved

#include <jfc/lua/internal.h>

#include <jfc/lua/exception.h>
#include <jfc/lua/key.h>
#include <jfc/lua/path.h>
#include <jfc/lua/data_table.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <cmath>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>

namespace jfc::lua {
    std::optional<std::size_t> data_table::_array_slot(const double aKey, const std::size_t aSize) {
        if (!(aKey >= 1)) return {};

        const auto index = static_cast<std::size_t>(aKey);

        if (static_cast<double>(index) != aKey) return {};

        if (index > aSize) return {};

        return index - 1;
    }

    const data_table::value_type *data_table::_find(const key &aKey) const {
        return std::visit([this](auto &&aKeyValue) -> const value_type * {
            using key_value_type = std::decay_t<decltype(aKeyValue)>;

            if constexpr (std::is_same_v<key_value_type, double>) {
                if (const auto slot = _array_slot(aKeyValue, m_Array.size())) return &m_Array[*slot];

                const auto found = m_NumberFields.find(aKeyValue);

                return found == m_NumberFields.end() ? nullptr : &found->second;
            }
            else if constexpr (std::is_same_v<key_value_type, bool>) {
                const auto &field = m_BooleanFields[static_cast<std::size_t>(aKeyValue)];

                return field ? &*field : nullptr;
            }
            else {
                const auto found = m_StringFields.find(aKeyValue);

                return found == m_StringFields.end() ? nullptr : &found->second;
            }
        }, aKey.value());
    }

    void data_table::_assign(const key &aKey, value_type aValue) {
        std::visit([this, &aValue](auto &&aKeyValue) {
            using key_value_type = std::decay_t<decltype(aKeyValue)>;

            if constexpr (std::is_same_v<key_value_type, double>) {
                const auto slot = _array_slot(aKeyValue, m_Array.size() + 1);

                if (!slot) { m_NumberFields[aKeyValue] = std::move(aValue); return; }

                if (*slot < m_Array.size()) { m_Array[*slot] = std::move(aValue); return; }

                m_Array.push_back(std::move(aValue));

                for (auto next = m_NumberFields.find(static_cast<double>(m_Array.size() + 1));
                     next != m_NumberFields.end();
                     next = m_NumberFields.find(static_cast<double>(m_Array.size() + 1))) {
                    m_Array.push_back(std::move(next->second));

                    m_NumberFields.erase(next);
                }
            }
            else if constexpr (std::is_same_v<key_value_type, bool>)
                m_BooleanFields[static_cast<std::size_t>(aKeyValue)] = std::move(aValue);
            else
                m_StringFields[aKeyValue] = std::move(aValue);
        }, aKey.value());
    }

    std::optional<double> data_table::get_number(const key &aKey) const {
        const auto *const found = _find(aKey);

        if (const auto *const value = found ? std::get_if<double>(found) : nullptr) return *value;

        return {};
    }

    std::optional<bool> data_table::get_boolean(const key &aKey) const {
        const auto *const found = _find(aKey);

        if (const auto *const value = found ? std::get_if<bool>(found) : nullptr) return *value;

        return {};
    }

    std::optional<std::string> data_table::get_string(const key &aKey) const {
        const auto *const found = _find(aKey);

        if (const auto *const value = found ? std::get_if<std::string>(found) : nullptr) return *value;

        return {};
    }

    std::shared_ptr<const data_table> data_table::get_data_table(const key &aKey) const {
        const auto *const found = _find(aKey);

        if (const auto *const value = found ? std::get_if<std::shared_ptr<data_table>>(found) : nullptr)
            return *value;

        return {};
    }

    void data_table::set(const key &aKey, const double aValue) { _assign(aKey, aValue); }

    void data_table::set(const key &aKey, const bool aValue) { _assign(aKey, aValue); }

    void data_table::set(const key &aKey, std::string aValue) { _assign(aKey, std::move(aValue)); }

    void data_table::set(const key &aKey, const char *const aValue) { _assign(aKey, std::string(aValue)); }

    void data_table::set(const key &aKey, const data_table &aValue) { 
        _assign(aKey, std::make_shared<data_table>(aValue)); 
    }

    void data_table::erase(const key &aKey) {
        std::visit([this](auto &&aKeyValue) {
            using key_value_type = std::decay_t<decltype(aKeyValue)>;

            if constexpr (std::is_same_v<key_value_type, double>) {
                const auto slot = _array_slot(aKeyValue, m_Array.size());

                if (!slot) { m_NumberFields.erase(aKeyValue); return; }

                for (std::size_t i = *slot + 1; i < m_Array.size(); ++i)
                    m_NumberFields[static_cast<double>(i + 1)] = std::move(m_Array[i]);

                m_Array.resize(*slot);
            }
            else if constexpr (std::is_same_v<key_value_type, bool>)
                m_BooleanFields[static_cast<std::size_t>(aKeyValue)].reset();
            else m_StringFields.erase(aKeyValue);
        }, aKey.value());
    }

    bool data_table::contains(const key &aKey) const { return _find(aKey); }

    std::size_t data_table::size() const {
        std::size_t total = m_Array.size() + m_NumberFields.size() + m_StringFields.size();

        for (const auto &field : m_BooleanFields) if (field) ++total;

        return total;
    }

    bool data_table::empty() const { return size() == 0; }

    std::vector<key> data_table::keys() const {
        std::vector<key> out;

        out.reserve(size());

        auto sparse = m_NumberFields.begin();

        std::size_t dense = 0;

        while (sparse != m_NumberFields.end() || dense < m_Array.size()) {
            const auto denseKey = static_cast<double>(dense + 1);

            if (dense < m_Array.size() && (sparse == m_NumberFields.end() || denseKey < sparse->first)) {
                out.push_back(denseKey);

                ++dense;
            }
            else out.push_back((sparse++)->first);
        }

        for (std::size_t i = 0; i < m_BooleanFields.size(); ++i)
            if (m_BooleanFields[i]) out.push_back(static_cast<bool>(i));

        for (const auto &field : m_StringFields) out.push_back(field.first);

        return out;
    }

    namespace {
        //! lua's reserved words: these cannot be used as keys
        constexpr const char *RESERVED[] = {
            "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in",
            "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"
        };

        [[nodiscard]] bool _is_bare_key(const std::string &aName) {
            if (aName.empty()) return false;

            if (!std::isalpha(static_cast<unsigned char>(aName.front())) && aName.front() != '_')
                return false;

            for (const char c : aName)
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;

            for (const auto *const reserved : RESERVED) if (aName == reserved) return false;

            return true;
        }

        void _write_quoted(std::ostream &aOut, const std::string &aText) {
            aOut << '"';

            for (const char c : aText) {
                switch (c) {
                    case '"':  aOut << "\\\""; break;
                    case '\\': aOut << "\\\\"; break;
                    case '\n': aOut << "\\n"; break;
                    case '\r': aOut << "\\r"; break;
                    case '\t': aOut << "\\t"; break;

                    default:
                        if (static_cast<unsigned char>(c) < 0x20) {
                            const auto previous = aOut.fill('0');

                            aOut << "\\" << std::setw(3)
                                << static_cast<int>(static_cast<unsigned char>(c));

                            aOut.fill(previous);
                        }
                        else aOut << c;
                }
            }

            aOut << '"';
        }
    }

    namespace {
        class literal_reader final {
        public:
            literal_reader(const std::string &aText) : m_Text(aText) {}

            [[nodiscard]] data_table read_table()
            {
                expect('{');

                data_table out;

                if (peek() == '}') { ++m_At; return out; }

                for (;;)
                {
                    const auto field = read_key();

                    expect('=');

                    read_value_into(out, field);

                    if (peek() == ',') { ++m_At; continue; }

                    break;
                }

                expect('}');

                return out;
            }

            void expect_end() { if (m_At != m_Text.size()) fail("trailing characters"); }

        private:
            [[noreturn]] void fail(const std::string &aWhy) const {
                throw exception("could not parse a data_table at character "
                    + std::to_string(m_At) + ": " + aWhy);
            }

            [[nodiscard]] char peek() const { return m_At < m_Text.size() ? m_Text[m_At] : '\0'; }

            void expect(const char aCharacter) {
                if (peek() != aCharacter) fail(std::string("expected '") + aCharacter + "'");

                ++m_At;
            }

            [[nodiscard]] std::string read_quoted() {
                expect('"');

                std::string out;

                while (m_At < m_Text.size() && m_Text[m_At] != '"') {
                    if (m_Text[m_At] != '\\') { out += m_Text[m_At++]; continue; }

                    if (++m_At >= m_Text.size()) fail("a string ends in an escape");

                    const char escaped = m_Text[m_At++];

                    switch (escaped) {
                        case 'n': out += '\n'; break;
                        case 'r': out += '\r'; break;
                        case 't': out += '\t'; break;
                        case '"': out += '"'; break;
                        case '\\': out += '\\'; break;

                        default:
                            if (!std::isdigit(static_cast<unsigned char>(escaped)))
                                fail(std::string("unknown escape \\") + escaped);
                            {
                                int code = escaped - '0';

                                for (int i = 0; i < 2 && m_At < m_Text.size()
                                    && std::isdigit(static_cast<unsigned char>(m_Text[m_At])); ++i)
                                    code = code * 10 + (m_Text[m_At++] - '0');

                                out += static_cast<char>(code);
                            }
                    }
                }

                expect('"');

                return out;
            }

            [[nodiscard]] key read_key() {
                if (peek() != '[') {
                    const auto start = m_At;

                    while (m_At < m_Text.size()
                        && (std::isalnum(static_cast<unsigned char>(m_Text[m_At])) || m_Text[m_At] == '_'))
                        ++m_At;

                    if (m_At == start) fail("expected a key");

                    return key(m_Text.substr(start, m_At - start));
                }

                ++m_At;

                key out = 0.0;

                if (peek() == '"') out = read_quoted();
                else if (m_Text.compare(m_At, 4, "true") == 0) { m_At += 4; out = true; }
                else if (m_Text.compare(m_At, 5, "false") == 0) { m_At += 5; out = false; }
                else out = read_number();

                expect(']');

                return out;
            }

            [[nodiscard]] double read_number() {
                const auto *const first = m_Text.c_str() + m_At;

                char *last = nullptr;

                const double value = std::strtod(first, &last);

                if (last == first) fail("expected a number");

                m_At += static_cast<std::size_t>(last - first);

                return value;
            }

            void read_value_into(data_table &aOut, const key &aKey) {
                switch (peek()) {
                    case '"': aOut.set(aKey, read_quoted()); return;
                    case '{': aOut.set(aKey, read_table()); return;

                    default: break;
                }

                if (m_Text.compare(m_At, 4, "true") == 0) { m_At += 4; aOut.set(aKey, true); return; }
                if (m_Text.compare(m_At, 5, "false") == 0) { m_At += 5; aOut.set(aKey, false); return; }

                aOut.set(aKey, read_number());
            }

            const std::string &m_Text;
            std::size_t m_At = 0;
        };
    }

    std::ostream &operator<<(std::ostream &out, const data_table &a) {
        std::stringstream stream;

        stream << "{";

        const auto fields = a.keys();

        std::size_t remaining = fields.size();

        const auto write = [&stream](const data_table::value_type &aValue) {
            std::visit([&stream](auto &&value) {
                using value_type = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<value_type, double>) stream << value;
                else if constexpr (std::is_same_v<value_type, bool>) stream << (value ? "true" : "false");
                else if constexpr (std::is_same_v<value_type, std::string>) _write_quoted(stream, value);
                else if constexpr (std::is_same_v<value_type, std::shared_ptr<data_table>>) {
                    if (!value) throw exception("table operator<<: null subtable");

                    stream << *value;
                }
                else throw exception("table operator<<: unsupported type");
            }, aValue);
        };

        const auto separate = [&stream, &remaining]() { if (--remaining) stream << ","; };

        for (const auto &field : fields) {
            if (field.is_number()) stream << "[" << std::get<double>(field.value()) << "]=";
            else if (field.is_boolean())
                stream << "[" << (std::get<bool>(field.value()) ? "true" : "false") << "]=";
            else {
                const auto &name = std::get<std::string>(field.value());

                if (_is_bare_key(name)) stream << name << "=";
                else {
                    stream << "[";

                    _write_quoted(stream, name);

                    stream << "]=";
                }
            }

            write(*a._find(field));
            separate();
        }

        stream << "}";

        out << stream.str();

        return out;
    }

    std::string data_table::to_string() const {
        std::ostringstream out;

        out << *this;

        return out.str();
    }

    data_table data_table::from_string(const std::string &aText) {
        literal_reader reader(aText);

        auto out = reader.read_table();

        reader.expect_end();

        return out;
    }

    void data_table::push_to_lua_state(lua_State *L) const {
        lua_newtable(L);

        const auto push_value = [L](const auto &aValue, const auto &aSetter) {
            std::visit([L, &aSetter](auto &&value) {
                using value_type = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<value_type, double>) lua_pushnumber(L, value);
                else if constexpr (std::is_same_v<value_type, bool>) lua_pushboolean(L, value);
                else if constexpr (std::is_same_v<value_type, std::string>) lua_pushstring(L, value.c_str());
                else if constexpr (std::is_same_v<value_type, std::shared_ptr<data_table>>)
                {
                    if (!value) throw exception("data_table::push_to_lua_state: null subtable");

                    value->push_to_lua_state(L);
                }
                else throw exception("data_table::push_to_lua_state: unsupported value type");

                aSetter();
            }, aValue);
        };

        for (const auto &field : keys()) {
            std::visit([this, L, &field, &push_value](auto &&aKey) {
                using key_value_type = std::decay_t<decltype(aKey)>;

                if constexpr (std::is_same_v<key_value_type, std::string>)
                {
                    push_value(*_find(field), [L, &aKey]() { lua_setfield(L, -2, aKey.c_str()); });
                }
                else
                {
                    if constexpr (std::is_same_v<key_value_type, double>) lua_pushnumber(L, aKey);
                    else lua_pushboolean(L, static_cast<int>(aKey));

                    push_value(*_find(field), [L]() { lua_settable(L, -3); });
                }
            }, field.value());
        }
    }

    struct data_table::read_frame final {
        const read_frame *parent;
        const key *segment;
    };

    struct data_table::read_context final {
        unsupported policy;

        const read_frame *deepest = nullptr;

        std::vector<const void *> ancestors;
    };

    void data_table::_reject(const read_context &aContext, const std::string &aWhy) {
        if (aContext.policy == unsupported::skip) return;

        std::vector<key> segments;

        for (const auto *frame = aContext.deepest; frame; frame = frame->parent)
            segments.push_back(*frame->segment);

        std::reverse(segments.begin(), segments.end());

        std::stringstream where;

        if (segments.empty()) where << "the table";
        else where << path(segments);

        throw exception("table: " + where.str() + " " + aWhy);
    }

    std::optional<key> data_table::_to_key(lua_State *L, const int aIndex, const read_context &aContext) {
        switch (lua_type(L, aIndex)) {
            case LUA_TSTRING:  return key(std::string(lua_tostring(L, aIndex)));
            case LUA_TNUMBER:  return key(lua_tonumber(L, aIndex));
            case LUA_TBOOLEAN: return key(static_cast<bool>(lua_toboolean(L, aIndex)));

            default:
                _reject(aContext, std::string("has a key of type ") + lua_typename(L, lua_type(L, aIndex))
                    + ", and only number, boolean and string keys can be stored");

                return {};
        }
    }

    std::optional<data_table::value_type> data_table::_to_value(lua_State *L, const int aIndex,
        read_context &aContext) {
        switch (lua_type(L, aIndex))
        {
            case LUA_TSTRING:  return value_type(std::string(lua_tostring(L, aIndex)));
            case LUA_TNUMBER:  return value_type(lua_tonumber(L, aIndex));
            case LUA_TBOOLEAN: return value_type(static_cast<bool>(lua_toboolean(L, aIndex)));

            case LUA_TTABLE:
            {
                const void *const identity = lua_topointer(L, aIndex);

                if (std::find(aContext.ancestors.begin(), aContext.ancestors.end(), identity)
                    != aContext.ancestors.end())
                {
                    _reject(aContext, "reaches a table that already contains it, which cannot be "
                        "written down");

                    return {};
                }

                auto nested = std::make_shared<data_table>();

                nested->_read(L, aIndex, aContext);

                return value_type(std::move(nested));
            }

            default:
                _reject(aContext, std::string("holds a ") + lua_typename(L, lua_type(L, aIndex))
                    + ", which cannot be stored");

                return {};
        }
    }

    void data_table::_read(lua_State *L, const int aIndex, read_context &aContext) {
        if (!lua_istable(L, aIndex)) throw exception("index must point to a table");

        if (aContext.ancestors.size() >= MAXIMUM_DEPTH)
            throw exception("table: nesting deeper than " + std::to_string(MAXIMUM_DEPTH)
                + " levels cannot be stored");

        if (!lua_checkstack(L, 5))
            throw exception("table: the lua stack cannot grow enough to read this table");

        aContext.ancestors.push_back(lua_topointer(L, aIndex));

        lua_pushvalue(L, aIndex);

        lua_pushnil(L);
        while (lua_next(L, -2)) {
            lua_pushvalue(L, -2);

            const auto parsedKey = _to_key(L, -1, aContext);

            lua_pop(L, 1);

            if (parsedKey) {
                const read_frame frame{aContext.deepest, &*parsedKey};

                aContext.deepest = &frame;

                auto parsedValue = _to_value(L, -1, aContext);

                aContext.deepest = frame.parent;

                if (parsedValue) _assign(*parsedKey, std::move(*parsedValue));
            }

            lua_pop(L, 1);
        }

        lua_pop(L, 1);

        aContext.ancestors.pop_back();
    }

    data_table::data_table(lua_State *L, int aIndex, const unsupported aPolicy)
    {
        read_context context;

        context.policy = aPolicy;

        _read(L, aIndex, context);
    }
}
