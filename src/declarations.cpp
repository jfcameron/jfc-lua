// © Joseph Cameron - MIT License

#include <jfc/lua/declarations.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <string_view>

namespace jfc::lua {
    namespace {
        constexpr std::array<std::string_view, 21> KEYWORDS{"and", "break", "do", "else", "elseif", "end", "false",
            "for", "function", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until",
            "while"};

        struct node final {
            const declared *pFunction{nullptr};
            std::map<std::string, node> children;
        };

        [[nodiscard]] bool _is_name(const std::string_view aText) {
            if (aText.empty() || std::isdigit(static_cast<unsigned char>(aText.front()))) return false;

            for (const char each : aText)
                if (!std::isalnum(static_cast<unsigned char>(each)) && each != '_') return false;

            return std::find(KEYWORDS.begin(), KEYWORDS.end(), aText) == KEYWORDS.end();
        }

        [[nodiscard]] std::string _key(const std::string &aName) {
            if (_is_name(aName)) return aName;

            std::string out = "[\"";

            for (const char each : aName) {
                if (each == '"' || each == '\\') out += '\\';

                out += each;
            }

            return out + "\"]";
        }

        [[nodiscard]] std::string _indent(const int aDepth) {
            return std::string(static_cast<std::size_t>(aDepth) * 4, ' ');
        }

        [[nodiscard]] std::string _comment(const std::string &aWords, const int aDepth) {
            if (aWords.empty()) return {};

            std::string line = aWords;

            std::replace(line.begin(), line.end(), '\n', ' ');

            return _indent(aDepth) + "-- " + line + "\n";
        }

        [[nodiscard]] std::string _type_of(const signature::value &aValue) {
            std::string out;

            if (aValue.type == "boolean") out = "boolean";
            else if (aValue.type == "integer" || aValue.type == "number") out = "number";
            else if (aValue.type == "string") out = "string";
            else if (aValue.type == "table") out = "{ [any]: any }";
            else if (aValue.type == "function") out = "((...any) -> ...any)";
            else if (aValue.type == "nil") out = "nil";
            else if (aValue.type == "value" || aValue.type == "userdata" || !_is_name(aValue.type)) out = "any";
            else out = aValue.type;

            if (aValue.optional && out != "any" && out != "nil") out += "?";

            return out;
        }

        //! `a: number, b: string?`, each named as aFunction names it or by its place, self first where aSelf, and the
        //! rest of the arguments as aRest has them: `...: any` in a declaration, `...any` in a function's type
        [[nodiscard]] std::string _parameters(const declared &aFunction, const bool aSelf,
            const std::string_view aRest) {
            std::vector<std::string> out;
            std::set<std::string> taken;

            if (aSelf) {
                out.emplace_back("self");
                taken.insert("self");
            }

            const auto &arguments = aFunction.signature.arguments;

            for (std::size_t i = 0; i < arguments.size(); ++i) {
                std::string name = i < aFunction.parameters.size() ? aFunction.parameters[i] : std::string();

                if (!_is_name(name) || taken.contains(name)) name = "a" + std::to_string(i + 1);

                taken.insert(name);
                out.push_back(name + ": " + _type_of(arguments[i]));
            }

            if (aFunction.signature.takes_rest) out.emplace_back(aRest);

            std::string joined;

            for (const auto &each : out) joined += (joined.empty() ? "" : ", ") + each;

            return joined;
        }

        [[nodiscard]] std::string _results(const signature &aSignature) {
            std::vector<std::string> out;

            for (const auto &each : aSignature.results) out.push_back(_type_of(each));

            if (aSignature.gives_rest) out.emplace_back("...any");

            if (out.size() == 1) return out.front();

            std::string joined;

            for (const auto &each : out) joined += (joined.empty() ? "" : ", ") + each;

            return "(" + joined + ")";
        }

        [[nodiscard]] std::string _function_type(const declared &aFunction) {
            return "(" + _parameters(aFunction, false, "...any") + ") -> " + _results(aFunction.signature);
        }

        [[nodiscard]] std::string _type_of(const node &aNode, const int aDepth) {
            const std::string function = aNode.pFunction ? _function_type(*aNode.pFunction) : std::string();

            if (aNode.children.empty()) return function;

            std::string table = "{\n";

            for (const auto &[name, child] : aNode.children) {
                if (child.pFunction) table += _comment(child.pFunction->words, aDepth + 1);

                table += _indent(aDepth + 1) + _key(name) + ": " + _type_of(child, aDepth + 1) + ",\n";
            }

            table += _indent(aDepth) + "}";

            return function.empty() ? table : "(" + function + ") & " + table;
        }
    }

    std::string luau_declarations(const std::vector<declared> &aFunctions,
        const std::map<std::string, std::vector<declared>> &aTypes) {
        std::string out;

        for (const auto &[name, methods] : aTypes) {
            if (!_is_name(name)) continue;

            if (!out.empty()) out += "\n";

            out += "declare extern type " + name + " with\n";

            for (const auto &method : methods) {
                if (!_is_name(method.path)) continue;

                out += _comment(method.words, 1);
                out += "    function " + method.path + "(" + _parameters(method, true, "...: any") + "): "
                    + _results(method.signature) + "\n";
            }

            out += "end\n";
        }

        std::map<std::string, node> globals;

        for (const auto &function : aFunctions) {
            auto *pLevel = &globals;
            node *pNode = nullptr;
            std::string_view rest = function.path;

            while (!rest.empty()) {
                const auto dot = rest.find('.');
                const std::string part(rest.substr(0, dot));

                pNode = &(*pLevel)[part];
                pLevel = &pNode->children;
                rest = dot == std::string_view::npos ? std::string_view() : rest.substr(dot + 1);
            }

            if (pNode) pNode->pFunction = &function;
        }

        for (const auto &[name, global] : globals) {
            if (!_is_name(name)) continue;

            if (!out.empty()) out += "\n";

            if (global.children.empty() && global.pFunction) {
                out += _comment(global.pFunction->words, 0);
                out += "declare function " + name + "(" + _parameters(*global.pFunction, false, "...: any") + "): "
                    + _results(global.pFunction->signature) + "\n";
            }
            else {
                if (global.pFunction) out += _comment(global.pFunction->words, 0);

                out += "declare " + name + ": " + _type_of(global, 0) + "\n";
            }
        }

        return out;
    }
}
