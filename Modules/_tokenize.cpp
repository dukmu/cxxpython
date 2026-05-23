#include "module_registry.hpp"

#include "cxxpython/compiler_internal.hpp"
#include "cxxpython/runtime.hpp"

#include "../Objects/object_utils.hpp"

#include <cstdint>
#include <vector>

namespace cxxpython
{
    namespace
    {
        Object *tokenize_tokens(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "tokens", args);
            require_positional_range(&runtime, "tokens", args, 1, 2);
            auto *source = dynamic_cast<StrObject *>(args.positional[0]);
            if (!source)
                raise_type_error(&runtime, "tokens() source must be str");

            std::string filename = "<string>";
            if (args.positional.size() == 2)
            {
                auto *filename_object = dynamic_cast<StrObject *>(args.positional[1]);
                if (!filename_object)
                    raise_type_error(&runtime, "tokens() filename must be str");
                filename = filename_object->value;
            }

            std::vector<compiler_internal::Token> tokens =
                compiler_internal::lex_source(runtime, source->value, std::move(filename));

            std::vector<Object *> result;
            result.reserve(tokens.size());
            std::vector<Rooted<Object>> roots;
            roots.reserve(tokens.size() * 6);
            for (const compiler_internal::Token &token : tokens)
            {
                Object *kind = runtime.new_str(compiler_internal::token_kind_name(token.kind));
                Object *lexeme = runtime.new_str(token.lexeme);
                Object *line = runtime.new_int(static_cast<std::int64_t>(token.line));
                Object *column = runtime.new_int(static_cast<std::int64_t>(token.column));
                Object *int_value = runtime.new_int(token.int_value);
                roots.push_back(runtime.root(kind));
                roots.push_back(runtime.root(lexeme));
                roots.push_back(runtime.root(line));
                roots.push_back(runtime.root(column));
                roots.push_back(runtime.root(int_value));
                Object *entry = runtime.new_tuple({kind, lexeme, line, column, int_value});
                roots.push_back(runtime.root(entry));
                result.push_back(entry);
            }
            return runtime.new_list(std::move(result));
        }

        constexpr NativeModuleFunctionDef tokenize_methods[] = {
            {
                "tokens",
                tokenize_tokens,
                "Return the compiler token stream for source text.",
                "($module, source, filename='<string>', /)",
            },
        };

        constexpr NativeModuleDef tokenize_module = {
            "_tokenize",
            "Low-level tokenizer substrate used by the script tokenize module.",
            tokenize_methods,
        };
    }

    void initialize_tokenize_module(Runtime &runtime, ModuleObject &module)
    {
        initialize_native_module(runtime, module, tokenize_module);
    }
}
