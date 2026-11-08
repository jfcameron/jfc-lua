// © Joseph Cameron - MIT License

#ifndef JFC_LUA_TABLE_READER_H
#define JFC_LUA_TABLE_READER_H

#include <jfc/lua/data_table.h>
#include <jfc/lua/exception.h>
#include <jfc/lua/key.h>

#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace jfc::lua
{
    /// \brief reads a data table as a format says it is: only the fields the format has, each of the kind it is
    ///
    /// A reader is made with the fields its table may have, and refuses any other, so a misspelt field is an error
    /// rather than a value quietly left at its default. Each read refuses a field holding the wrong kind of value. A
    /// refusal is an exception saying where it is and what was wanted, as a typed call's does:
    /// `sky.lua: phases[2].sun.elevation wants a number, got a string`.
    class table_reader final
    {
    public:
        /// \brief bounds on a number, each inclusive, either left out for no bound
        struct range final {
            std::optional<double> least = std::nullopt;
            std::optional<double> most = std::nullopt;
        };

        /// \brief reads aTable, which may hold only the string keys named in aFields
        ///
        /// aTable must outlive the reader. aWhere begins every refusal, naming the file or what the table is.
        ///
        /// \warning throws exception when aTable holds any other key, naming it and aFields
        table_reader(const data_table &aTable, std::string aWhere, std::vector<std::string> aFields);

        /// \brief the value of aField as a value_type
        ///
        /// value_type is `double` or `float`, which takes a finite number; a whole number type, which takes a number
        /// that is whole and fits it; `bool`; `std::string`; a `data_table`, read as it is; a `std::vector` of one of
        /// these, which is a list; or a `std::array` of one, which is a list of exactly that many. A `std::optional` of
        /// any of them is nothing where aField is absent.
        ///
        /// \warning throws exception when aField holds the wrong kind of value; or is absent and value_type is not a
        /// `std::optional`; or is not one of the fields the reader was given
        template <typename value_type>
        [[nodiscard]] value_type get(const std::string_view aField) const
        {
            return get<value_type>(aField, range{});
        }

        /// \brief the value of aField as a value_type, as get does, a number of it within aRange
        ///
        /// \warning throws exception as get does, and for a number outside aRange
        template <typename value_type>
        [[nodiscard]] value_type get(const std::string_view aField, const range &aRange) const
        {
            const auto name = _field(aField);

            return _read<value_type>(*m_pTable, key(name), _subject(name), aRange);
        }

        /// \brief the table at aField, read by a reader of its own, which takes aFields; nothing where it is absent
        ///
        /// \warning throws exception when aField holds something else, or its table holds a key not in aFields
        [[nodiscard]] std::optional<table_reader> table(std::string_view aField,
            std::vector<std::string> aFields) const;

        /// \brief each table of the list at aField, read by a reader of its own, which takes aFields; none where it is
        /// absent
        ///
        /// \warning throws exception when aField holds something else, or a table of it holds a key not in aFields
        [[nodiscard]] std::vector<table_reader> tables(std::string_view aField, std::vector<std::string> aFields) const;

        /// \brief an exception saying aWhy, after where this reader is: for what a format refuses beyond kinds and
        /// ranges
        [[nodiscard]] exception refused(std::string_view aWhy) const;

    private:
        template <typename> struct is_optional : std::false_type {};
        template <typename held_type> struct is_optional<std::optional<held_type>> : std::true_type {};

        template <typename> struct is_vector : std::false_type {};
        template <typename held_type> struct is_vector<std::vector<held_type>> : std::true_type {};

        template <typename> struct is_array : std::false_type {};
        template <typename held_type, std::size_t count>
        struct is_array<std::array<held_type, count>> : std::true_type {};

        table_reader(std::shared_ptr<const data_table> pOwned, const data_table &aTable, std::string aWhere,
            std::string aPath, std::vector<std::string> aFields);

        /// \brief where this table is, as a refusal begins
        [[nodiscard]] std::string _table_subject() const;

        /// \brief aField, refused unless it is one of the reader's fields
        [[nodiscard]] std::string _field(std::string_view aField) const;

        /// \brief where aName is, as a refusal begins
        [[nodiscard]] std::string _subject(std::string_view aName) const;

        /// \brief where a field aName of this table's is, as a path below the table read first
        [[nodiscard]] std::string _path_of(std::string_view aName) const;

        template <typename value_type>
        [[nodiscard]] static value_type _read(const data_table &aIn, const key &aKey, const std::string &aSubject,
            const range &aRange)
        {
            if constexpr (is_optional<value_type>::value) {
                if (!aIn.contains(aKey)) return std::nullopt;

                return _read<typename value_type::value_type>(aIn, aKey, aSubject, aRange);
            }
            else if constexpr (std::is_same_v<value_type, bool>) return _boolean(aIn, aKey, aSubject);
            else if constexpr (std::is_same_v<value_type, std::string>) return _text(aIn, aKey, aSubject);
            else if constexpr (std::is_same_v<value_type, data_table>) return *_table(aIn, aKey, aSubject);
            else if constexpr (std::is_integral_v<value_type>) {
                return static_cast<value_type>(_whole(aIn, aKey, aSubject, aRange,
                    std::numeric_limits<value_type>::digits, std::is_signed_v<value_type>));
            }
            else if constexpr (std::is_floating_point_v<value_type>) {
                return static_cast<value_type>(_number(aIn, aKey, aSubject, aRange));
            }
            else if constexpr (is_vector<value_type>::value || is_array<value_type>::value) {
                const auto pList = _list(aIn, aKey, aSubject);

                value_type out{};

                if constexpr (is_array<value_type>::value) _expect_count(*pList, out.size(), aSubject);
                else out.reserve(pList->size());

                for (std::size_t at = 0; at < pList->size(); ++at) {
                    auto each = _read<typename value_type::value_type>(*pList, key(at + 1),
                        aSubject + "[" + std::to_string(at + 1) + "]", aRange);

                    if constexpr (is_array<value_type>::value) out[at] = std::move(each);
                    else out.push_back(std::move(each));
                }

                return out;
            }
            else static_assert(!sizeof(value_type), "table_reader::get reads numbers, bools, strings, data tables, "
                "and vectors, arrays and optionals of them");
        }

        [[nodiscard]] static double _number(const data_table &aIn, const key &aKey, const std::string &aSubject,
            const range &aRange);

        [[nodiscard]] static double _whole(const data_table &aIn, const key &aKey, const std::string &aSubject,
            const range &aRange, int aDigits, bool aSigned);

        [[nodiscard]] static bool _boolean(const data_table &aIn, const key &aKey, const std::string &aSubject);

        [[nodiscard]] static std::string _text(const data_table &aIn, const key &aKey, const std::string &aSubject);

        [[nodiscard]] static std::shared_ptr<const data_table> _table(const data_table &aIn, const key &aKey,
            const std::string &aSubject);

        /// \brief the table at aKey, refused unless its keys are 1 to its size
        [[nodiscard]] static std::shared_ptr<const data_table> _list(const data_table &aIn, const key &aKey,
            const std::string &aSubject);

        static void _expect_count(const data_table &aList, std::size_t aCount, const std::string &aSubject);

        /// \brief keeps a table a reader of a field reads, so it may outlive the reader that made it
        std::shared_ptr<const data_table> m_pOwned;

        const data_table *m_pTable;

        std::string m_Where;

        /// \brief where this table is below the table read first: empty for that one, `phases[2].sun` for another
        std::string m_Path;

        std::vector<std::string> m_Fields;
    };
}

#endif
