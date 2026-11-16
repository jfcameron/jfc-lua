// © Joseph Cameron - MIT License

#include <jfc/lua/checker.h>
#include <jfc/lua/exception.h>

#include <Luau/Ast.h>
#include <Luau/Autocomplete.h>
#include <Luau/BuiltinDefinitions.h>
#include <Luau/ConfigResolver.h>
#include <Luau/Error.h>
#include <Luau/FileResolver.h>
#include <Luau/Frontend.h>
#include <Luau/ToString.h>

#include <algorithm>
#include <sstream>
#include <utility>

namespace jfc::lua {
    namespace {
        struct files final : Luau::FileResolver {
            checker::reader read;
            checker::resolver resolve;
            checker::placer place;

            files(checker::reader aRead, checker::resolver aResolve, checker::placer aPlace)
            : read(std::move(aRead))
            , resolve(std::move(aResolve))
            , place(std::move(aPlace)) {}

            std::optional<Luau::SourceCode> readSource(const Luau::ModuleName &aName) override {
                auto text = read(aName);

                if (!text) return std::nullopt;

                return Luau::SourceCode{std::move(*text), Luau::SourceCode::Module};
            }

            std::optional<Luau::ModuleInfo> resolveModule(const Luau::ModuleInfo *const pContext,
                Luau::AstExpr *const pExpression, const Luau::TypeCheckLimits &) override {
                const auto *const pName = pExpression ? pExpression->as<Luau::AstExprConstantString>() : nullptr;

                if (!pName || !pContext) return std::nullopt;

                auto found = resolve(pContext->name, std::string(pName->value.data, pName->value.size));

                if (!found) return std::nullopt;

                return Luau::ModuleInfo{std::move(*found)};
            }

            std::optional<std::string> getEnvironmentForModule(const Luau::ModuleName &aName) const override {
                auto environment = place(aName);

                if (environment.empty()) return std::nullopt;

                return environment;
            }
        };
    }

    struct checker::impl final {
        files resolver;
        Luau::NullConfigResolver config;
        Luau::Frontend frontend;

        impl(reader aRead, checker::resolver aResolve, placer aPlace)
        : resolver(std::move(aRead), std::move(aResolve), std::move(aPlace))
        , frontend(Luau::SolverMode::Old, &resolver, &config) {}
    };

    checker::checker(const std::map<std::string, std::string> &aDeclarations, reader aRead, resolver aResolve,
        placer aPlace)
    : m_pImpl(std::make_unique<impl>(std::move(aRead), std::move(aResolve), std::move(aPlace))) {
        auto &frontend = m_pImpl->frontend;

        Luau::registerBuiltinGlobals(frontend, frontend.globals);
        Luau::registerBuiltinGlobals(frontend, frontend.globalsForAutocomplete, true);

        for (const auto &[name, text] : aDeclarations) {
            const auto scope = frontend.addEnvironment(name);
            const auto loaded = frontend.loadDefinitionFile(frontend.globals, scope, text, "@" + name, false);

            if (loaded.success) continue;

            std::ostringstream why;

            for (const auto &each : loaded.parseResult.errors)
                why << "\n" << each.getLocation().begin.line + 1 << ": " << each.getMessage();

            if (loaded.module)
                for (const auto &each : loaded.module->errors)
                    why << "\n" << each.location.begin.line + 1 << ": " << Luau::toString(each);

            throw exception("the declarations of " + name + " do not load:" + why.str());
        }

        Luau::freeze(frontend.globals.globalTypes);
        Luau::freeze(frontend.globalsForAutocomplete.globalTypes);
    }

    checker::~checker() = default;

    std::vector<checker::problem> checker::check(const std::string &aFile) {
        const auto result = m_pImpl->frontend.check(aFile);

        std::vector<problem> out;

        for (const auto &each : result.errors) {
            if (each.moduleName != aFile) continue;

            out.push_back({each.moduleName, static_cast<int>(each.location.begin.line) + 1,
                static_cast<int>(each.location.begin.column) + 1, Luau::toString(each)});
        }

        return out;
    }

    void checker::changed(const std::string &aFile) { m_pImpl->frontend.markDirty(aFile); }

    std::vector<checker::suggestion> checker::complete(const std::string &aFile, const int aLine, const int aColumn) {
        auto &frontend = m_pImpl->frontend;

        Luau::FrontendOptions options;

        options.retainFullTypeGraphs = true;
        options.forAutocomplete = true;

        static_cast<void>(frontend.check(aFile, options));

        const Luau::Position at{static_cast<unsigned>(std::max(aLine - 1, 0)),
            static_cast<unsigned>(std::max(aColumn - 1, 0))};

        const auto found = Luau::autocomplete(frontend, aFile, at,
            [](std::string, std::optional<const Luau::ExternType *>, std::optional<std::string>)
                -> std::optional<Luau::AutocompleteEntryMap> { return std::nullopt; });

        const auto *const pIndex = found.ancestry.empty() ? nullptr
            : found.ancestry.back()->as<Luau::AstExprIndexName>();
        const bool colon = pIndex && pIndex->op == ':';

        std::vector<suggestion> out;

        for (const auto &[name, each] : found.entryMap) {
            suggestion one{name};

            switch (each.kind) {
                case Luau::AutocompleteEntryKind::Property: one.type = suggestion::kind::property; break;
                case Luau::AutocompleteEntryKind::Binding: one.type = suggestion::kind::binding; break;
                case Luau::AutocompleteEntryKind::Keyword: one.type = suggestion::kind::keyword; break;
                case Luau::AutocompleteEntryKind::Type: one.type = suggestion::kind::type; break;
                default: continue;
            }

            if (each.type) {
                Luau::ToStringOptions written;

                written.functionTypeArguments = true;

                one.annotation = Luau::toString(*each.type, written);
            }

            one.method = colon && one.type == suggestion::kind::property;

            out.push_back(std::move(one));
        }

        std::sort(out.begin(), out.end(), [](const suggestion &aA, const suggestion &aB) { return aA.name < aB.name; });

        return out;
    }

    bool checker::strict(const std::string &aSource) {
        std::istringstream lines(aSource);

        for (std::string line; std::getline(lines, line);) {
            if (line.rfind("--!strict", 0) == 0) return true;

            if (!line.empty() && line.rfind("--", 0) != 0) return false;
        }

        return false;
    }
}
