// © Joseph Cameron - MIT License

#include <jfc/lua/internal.h>

#include <jfc/lua/table_reader.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>
#include <variant>

namespace jfc::lua {
    namespace {
        [[nodiscard]] std::string _joined(const std::string_view aWhere, const std::string_view aWhat) {
            if (aWhere.empty()) return std::string(aWhat);

            if (aWhat.empty()) return std::string(aWhere);

            return std::string(aWhere) + ": " + std::string(aWhat);
        }

        [[nodiscard]] std::string _listed(const std::vector<std::string> &aNames) {
            if (aNames.empty()) return "nothing";

            std::string out;

            for (std::size_t at = 0; at < aNames.size(); ++at)
                out.append(at == 0 ? "" : at + 1 == aNames.size() ? " and " : ", ").append(aNames[at]);

            return out;
        }

        [[nodiscard]] std::string _number_text(const double aNumber) {
            std::ostringstream out;

            detail::write_number(out, aNumber);

            return out.str();
        }

        [[nodiscard]] std::string _key_text(const key &aKey) {
            if (const auto *const pName = std::get_if<std::string>(&aKey.value())) return "\"" + *pName + "\"";

            if (const auto *const pNumber = std::get_if<double>(&aKey.value())) return _number_text(*pNumber);

            return std::get<bool>(aKey.value()) ? "true" : "false";
        }

        [[nodiscard]] std::string _found(const data_table &aIn, const key &aKey) {
            if (aIn.get_number(aKey)) return "a number";

            if (aIn.get_string(aKey)) return "a string";

            if (aIn.get_boolean(aKey)) return "a boolean";

            if (aIn.get_data_table(aKey)) return "a table";

            return "nil";
        }

        [[nodiscard]] exception _refusal(const std::string &aSubject, const std::string &aWanted,
            const std::string &aGot) {
            return exception(aSubject + " wants " + aWanted + ", got " + aGot);
        }

        [[nodiscard]] std::string _bounded(const std::string &aKind, const std::optional<std::string> &aLeast,
            const std::optional<std::string> &aMost) {
            if (aLeast && aMost && *aLeast == *aMost) return *aLeast;

            if (aLeast && aMost) return aKind + " from " + *aLeast + " to " + *aMost;

            if (aLeast) return aKind + " of at least " + *aLeast;

            if (aMost) return aKind + " of at most " + *aMost;

            return aKind;
        }

        [[nodiscard]] std::optional<std::string> _text_of(const std::optional<double> aNumber) {
            if (!aNumber) return std::nullopt;

            return _number_text(*aNumber);
        }
    }

    table_reader::table_reader(const data_table &aTable, std::string aWhere, std::vector<std::string> aFields)
    : table_reader(nullptr, aTable, std::move(aWhere), {}, std::move(aFields))
    {}

    table_reader::table_reader(std::shared_ptr<const data_table> pOwned, const data_table &aTable, std::string aWhere,
        std::string aPath, std::vector<std::string> aFields)
    : m_pOwned(std::move(pOwned))
    , m_pTable(&aTable)
    , m_Where(std::move(aWhere))
    , m_Path(std::move(aPath))
    , m_Fields(std::move(aFields))
    {
        for (const auto &each : m_pTable->keys()) {
            const auto *const pName = std::get_if<std::string>(&each.value());

            if (pName && std::find(m_Fields.begin(), m_Fields.end(), *pName) != m_Fields.end()) continue;

            const auto subject = _table_subject();

            throw exception((subject.empty() ? std::string("the table") : subject) + " has no field " + _key_text(each)
                + ": it takes " + _listed(m_Fields));
        }
    }

    std::optional<table_reader> table_reader::table(const std::string_view aField,
        std::vector<std::string> aFields) const {
        const auto name = _field(aField);

        if (!m_pTable->contains(name)) return std::nullopt;

        const auto pTable = _table(*m_pTable, key(name), _subject(name));

        return table_reader(pTable, *pTable, m_Where, _path_of(name), std::move(aFields));
    }

    std::vector<table_reader> table_reader::tables(const std::string_view aField,
        std::vector<std::string> aFields) const {
        const auto name = _field(aField);

        std::vector<table_reader> out;

        if (!m_pTable->contains(name)) return out;

        const auto subject = _subject(name);
        const auto pList = _list(*m_pTable, key(name), subject);

        out.reserve(pList->size());

        for (std::size_t at = 1; at <= pList->size(); ++at) {
            const auto index = "[" + std::to_string(at) + "]";
            const auto pEach = _table(*pList, key(at), subject + index);

            out.push_back(table_reader(pEach, *pEach, m_Where, _path_of(name) + index, aFields));
        }

        return out;
    }

    exception table_reader::refused(const std::string_view aWhy) const {
        return exception(_joined(_table_subject(), aWhy));
    }

    std::string table_reader::_field(const std::string_view aField) const {
        if (std::find(m_Fields.begin(), m_Fields.end(), aField) != m_Fields.end()) return std::string(aField);

        const auto subject = _table_subject();

        throw exception((subject.empty() ? std::string("the table") : subject) + " was asked for \""
            + std::string(aField) + "\", not one of its fields: " + _listed(m_Fields));
    }

    std::string table_reader::_table_subject() const { return _joined(m_Where, m_Path); }

    std::string table_reader::_subject(const std::string_view aName) const { return _joined(m_Where, _path_of(aName)); }

    std::string table_reader::_path_of(const std::string_view aName) const {
        return m_Path.empty() ? std::string(aName) : m_Path + "." + std::string(aName);
    }

    double table_reader::_number(const data_table &aIn, const key &aKey, const std::string &aSubject,
        const range &aRange) {
        const auto number = aIn.get_number(aKey);
        const auto least = _text_of(aRange.least), most = _text_of(aRange.most);

        if (!number) throw _refusal(aSubject, _bounded("a number", least, most), _found(aIn, aKey));

        if (!std::isfinite(*number))
            throw _refusal(aSubject, _bounded("a finite number", least, most), _number_text(*number));

        if ((aRange.least && *number < *aRange.least) || (aRange.most && *number > *aRange.most))
            throw _refusal(aSubject, _bounded("a number", least, most), _number_text(*number));

        return *number;
    }

    double table_reader::_whole(const data_table &aIn, const key &aKey, const std::string &aSubject,
        const range &aRange, const int aDigits, const bool aSigned) {
        const double limit = std::ldexp(1.0, aDigits);
        const double lowest = aSigned ? -limit : 0.0;
        const auto highest = ~0ull >> (64 - aDigits);

        const auto least = aRange.least && *aRange.least > lowest ? _number_text(*aRange.least)
            : aSigned ? "-" + std::to_string(highest + 1) : std::string("0");
        const auto most = aRange.most && *aRange.most < limit ? _number_text(*aRange.most) : std::to_string(highest);

        const auto wanted = _bounded("a whole number", least, most);
        const auto number = aIn.get_number(aKey);

        if (!number) throw _refusal(aSubject, wanted, _found(aIn, aKey));

        const double value = *number;

        const bool fits = value == std::floor(value) && value >= lowest && value < limit
            && (!aRange.least || value >= *aRange.least) && (!aRange.most || value <= *aRange.most);

        if (!fits) throw _refusal(aSubject, wanted, _number_text(value));

        return value;
    }

    bool table_reader::_boolean(const data_table &aIn, const key &aKey, const std::string &aSubject) {
        const auto truth = aIn.get_boolean(aKey);

        if (!truth) throw _refusal(aSubject, "a boolean", _found(aIn, aKey));

        return *truth;
    }

    std::string table_reader::_text(const data_table &aIn, const key &aKey, const std::string &aSubject) {
        auto text = aIn.get_string(aKey);

        if (!text) throw _refusal(aSubject, "a string", _found(aIn, aKey));

        return std::move(*text);
    }

    std::shared_ptr<const data_table> table_reader::_table(const data_table &aIn, const key &aKey,
        const std::string &aSubject) {
        auto pTable = aIn.get_data_table(aKey);

        if (!pTable) throw _refusal(aSubject, "a table", _found(aIn, aKey));

        return pTable;
    }

    std::shared_ptr<const data_table> table_reader::_list(const data_table &aIn, const key &aKey,
        const std::string &aSubject) {
        auto pList = aIn.get_data_table(aKey);

        if (!pList) throw _refusal(aSubject, "a list", _found(aIn, aKey));

        const auto size = static_cast<double>(pList->size());

        for (const auto &each : pList->keys()) {
            const auto *const pAt = std::get_if<double>(&each.value());

            if (pAt && *pAt >= 1 && *pAt <= size && *pAt == std::floor(*pAt)) continue;

            throw _refusal(aSubject, "a list", "a table with the key " + _key_text(each));
        }

        return pList;
    }

    void table_reader::_expect_count(const data_table &aList, const std::size_t aCount, const std::string &aSubject) {
        if (aList.size() == aCount) return;

        throw _refusal(aSubject, "a list of " + std::to_string(aCount), "a list of " + std::to_string(aList.size()));
    }
}
