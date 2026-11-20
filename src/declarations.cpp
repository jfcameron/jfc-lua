// © Joseph Cameron - MIT License

#include <jfc/lua/declarations.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <stdexcept>
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

        [[nodiscard]] std::string _named(const std::string &aType) {
            if (aType == "boolean") return "boolean";
            if (aType == "integer" || aType == "number") return "number";
            if (aType == "string") return "string";
            if (aType == "table") return "{ [any]: any }";
            if (aType == "function") return "((...any) -> ...any)";
            if (aType == "nil") return "nil";
            if (aType == "value" || aType == "userdata" || !_is_name(aType)) return "any";

            return aType;
        }

        [[nodiscard]] std::string _type_of(const signature::value &aValue) {
            std::string out = _named(aValue.type);

            if (aValue.list) out = "{ " + out + " }";
            else if (!aValue.key.empty()) out = "{ [" + _named(aValue.key) + "]: " + out + " }";

            if (aValue.optional && out != "any" && out != "nil") out += "?";

            return out;
        }

        void _shapes_of(const signature::value &aValue, std::map<std::string, const shape *> &aOut);

        //! **aShape, and every shape its fields name**, by name: one name, one shape
        void _shapes_of(const shape &aShape, std::map<std::string, const shape *> &aOut) {
            const auto [at, added] = aOut.try_emplace(aShape.name, &aShape);

            if (!added) {
                if (!(*at->second == aShape))
                    throw std::invalid_argument("two shapes are called " + aShape.name + ", and their fields differ");

                return;
            }

            for (const auto &each : aShape.fields) _shapes_of(each.type, aOut);
        }

        void _shapes_of(const signature::value &aValue, std::map<std::string, const shape *> &aOut) {
            if (aValue.shape) _shapes_of(*aValue.shape, aOut);
        }

        void _shapes_of(const declared &aDeclared, std::map<std::string, const shape *> &aOut) {
            for (const auto &each : aDeclared.signature.arguments) _shapes_of(each, aOut);
            for (const auto &each : aDeclared.signature.results) _shapes_of(each, aOut);
        }

        //! `export type thing = { ... }`, each field on a line of its own with its words over it
        [[nodiscard]] std::string _shape(const shape &aShape) {
            std::string out = _comment(aShape.words, 0) + "export type " + aShape.name + " = {\n";

            for (const auto &each : aShape.fields)
                out += _comment(each.words, 1) + _indent(1) + _key(each.name) + ": " + _type_of(each.type) + ",\n";

            return out + "}\n";
        }

        //! what a value declared is: its signature's first result, or any where it has none
        [[nodiscard]] std::string _value_type(const declared &aValue) {
            return aValue.signature.results.empty() ? std::string("any") : _type_of(aValue.signature.results.front());
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
            const std::string function = !aNode.pFunction ? std::string()
                : aNode.pFunction->value ? _value_type(*aNode.pFunction) : _function_type(*aNode.pFunction);

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
        const std::map<std::string, std::vector<declared>> &aTypes, const std::vector<shape> &aShapes) {
        std::string out;

        std::map<std::string, const shape *> shapes;

        for (const auto &each : aShapes) _shapes_of(each, shapes);

        for (const auto &each : aFunctions) _shapes_of(each, shapes);

        for (const auto &[name, methods] : aTypes)
            for (const auto &each : methods) _shapes_of(each, shapes);

        for (const auto &[name, pShape] : shapes) {
            if (!_is_name(name)) continue;

            if (!out.empty()) out += "\n";

            out += _shape(*pShape);
        }

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

            if (global.children.empty() && global.pFunction && global.pFunction->value) {
                out += _comment(global.pFunction->words, 0);
                out += "declare " + name + ": " + _value_type(*global.pFunction) + "\n";
            }
            else if (global.children.empty() && global.pFunction) {
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
