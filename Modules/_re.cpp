#include "module_registry.hpp"

#include "cxxpython/runtime.hpp"

#include "../Objects/object_utils.hpp"

#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace cxxpython
{
    namespace
    {
        class RePatternObject final : public Object
        {
        public:
            RePatternObject(TypeObject *type, std::string pattern, std::uint32_t flags, std::regex compiled)
                : Object(type), pattern_(std::move(pattern)), flags_(flags), compiled_(std::move(compiled))
            {
            }

            const std::string &pattern() const noexcept { return pattern_; }
            const std::regex &compiled() const noexcept { return compiled_; }

        private:
            std::string pattern_;
            std::uint32_t flags_;
            std::regex compiled_;
        };

        class ReMatchObject final : public Object
        {
        public:
            struct Span
            {
                std::size_t start;
                std::size_t end;
            };

            ReMatchObject(TypeObject *type,
                          RePatternObject *pattern,
                          std::string input,
                          std::vector<std::optional<Span>> spans)
                : Object(type), pattern_(pattern), input_(std::move(input)), spans_(std::move(spans))
            {
            }

            RePatternObject *pattern() const noexcept { return pattern_; }
            const std::string &input() const noexcept { return input_; }
            const std::vector<std::optional<Span>> &spans() const noexcept { return spans_; }

        private:
            RePatternObject *pattern_;
            std::string input_;
            std::vector<std::optional<Span>> spans_;
        };

        std::string pattern_repr(const Object &object)
        {
            const auto *pattern = static_cast<const RePatternObject *>(&object);
            std::ostringstream out;
            out << "re.compile(" << std::quoted(pattern->pattern()) << ")";
            return out.str();
        }

        std::string match_repr(const Object &object)
        {
            const auto *match = static_cast<const ReMatchObject *>(&object);
            const auto &spans = match->spans();
            if (spans.empty() || !spans[0].has_value())
                return "<re.Match object>";
            return "<re.Match object; span=(" +
                   std::to_string(spans[0]->start) + ", " +
                   std::to_string(spans[0]->end) + ")>";
        }

        void match_traverse(const Object &object, const VisitChildren &visitor)
        {
            const auto *match = static_cast<const ReMatchObject *>(&object);
            if (match->pattern())
                visitor(match->pattern());
        }

        std::uint32_t require_flags(Runtime &runtime, Object *object)
        {
            if (!object || object == runtime.none())
                return 0;
            auto *flags = dynamic_cast<IntObject *>(object);
            if (!flags)
                raise_type_error(&runtime, "flags must be int");
            if (flags->value < 0)
                raise_value_error(&runtime, "flags must be non-negative");
            return static_cast<std::uint32_t>(flags->value);
        }

        std::regex compile_regex(const std::string &pattern, std::uint32_t flags)
        {
            std::regex::flag_type syntax = std::regex::ECMAScript;
            if ((flags & 1U) != 0)
                syntax |= std::regex::icase;
            return std::regex(pattern, syntax);
        }

        std::vector<std::optional<ReMatchObject::Span>> make_spans(const std::smatch &match)
        {
            std::vector<std::optional<ReMatchObject::Span>> spans;
            spans.reserve(match.size());
            for (std::size_t i = 0; i < match.size(); ++i)
            {
                if (!match[i].matched)
                {
                    spans.push_back(std::nullopt);
                    continue;
                }
                spans.push_back(ReMatchObject::Span{
                    static_cast<std::size_t>(match.position(i)),
                    static_cast<std::size_t>(match.position(i) + match.length(i)),
                });
            }
            return spans;
        }

        ModuleObject *re_module_object(Runtime &runtime)
        {
            auto *module = dynamic_cast<ModuleObject *>(runtime.modules_dict()->get(runtime.new_str("_re")));
            if (!module)
                raise_type_error(&runtime, "_re module is not initialized");
            return module;
        }

        TypeObject *module_type(Runtime &runtime, const char *name)
        {
            Object *object = re_module_object(runtime)->dict()->get(runtime.new_str(name));
            auto *type = dynamic_cast<TypeObject *>(object);
            if (!type)
                raise_type_error(&runtime, std::string("_re.") + name + " type is not initialized");
            return type;
        }

        Object *new_match(Runtime &runtime,
                          TypeObject *match_type,
                          RePatternObject *pattern,
                          const std::string &input,
                          const std::smatch &match)
        {
            return runtime.allocate<ReMatchObject>(match_type, pattern, input, make_spans(match));
        }

        Object *pattern_match(Runtime &runtime, Object *self, ObjectSpan args)
        {
            auto *pattern = dynamic_cast<RePatternObject *>(self);
            if (!pattern)
                raise_type_error(&runtime, "Pattern.match() receiver does not have regex layout");
            if (args.size() != 1)
                raise_type_error(&runtime, "Pattern.match() takes exactly one argument");
            auto *text = dynamic_cast<StrObject *>(args[0]);
            if (!text)
                raise_type_error(&runtime, "Pattern.match() argument must be str");
            std::smatch match;
            if (!std::regex_search(text->value, match, pattern->compiled(), std::regex_constants::match_continuous))
                return runtime.none();
            return new_match(runtime, module_type(runtime, "Match"), pattern, text->value, match);
        }

        Object *match_group(Runtime &runtime, Object *self, ObjectSpan args)
        {
            auto *match = dynamic_cast<ReMatchObject *>(self);
            if (!match)
                raise_type_error(&runtime, "Match.group() receiver does not have regex layout");
            if (args.size() > 1)
                raise_type_error(&runtime, "Match.group() takes at most one argument");
            std::size_t index = 0;
            if (!args.empty())
            {
                auto *number = dynamic_cast<IntObject *>(args[0]);
                if (!number)
                    raise_type_error(&runtime, "Match.group() index must be int");
                if (number->value < 0)
                    return runtime.none();
                index = static_cast<std::size_t>(number->value);
            }
            const auto &spans = match->spans();
            if (index >= spans.size() || !spans[index].has_value())
                return runtime.none();
            const auto span = spans[index].value();
            return runtime.new_str(match->input().substr(span.start, span.end - span.start));
        }

        TypeObject *new_module_type(Runtime &runtime, std::string name, TypeSlots slots)
        {
            TypeObject *type = runtime.new_heap_type(std::move(name), runtime.object_type(), slots);
            type->flags &= ~TypeFlagHasInstanceDict;
            type->module = "_re";
            type->dict->set(runtime.new_str("__module__"), runtime.new_str("_re"));
            return type;
        }

        Object *re_compile(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "compile", args);
            require_positional_range(&runtime, "compile", args, 1, 2);
            auto *pattern = dynamic_cast<StrObject *>(args.positional[0]);
            if (!pattern)
                raise_type_error(&runtime, "compile() pattern must be str");
            const std::uint32_t flags = args.positional.size() == 2 ? require_flags(runtime, args.positional[1]) : 0U;
            return runtime.allocate<RePatternObject>(
                module_type(runtime, "Pattern"),
                pattern->value,
                flags,
                compile_regex(pattern->value, flags));
        }

        Object *re_match(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "match", args);
            require_positional_range(&runtime, "match", args, 2, 3);
            auto *pattern = dynamic_cast<StrObject *>(args.positional[0]);
            auto *text = dynamic_cast<StrObject *>(args.positional[1]);
            if (!pattern)
                raise_type_error(&runtime, "match() pattern must be str");
            if (!text)
                raise_type_error(&runtime, "match() string must be str");
            const std::uint32_t flags = args.positional.size() == 3 ? require_flags(runtime, args.positional[2]) : 0U;
            std::regex compiled = compile_regex(pattern->value, flags);
            std::smatch match;
            if (!std::regex_search(text->value, match, compiled, std::regex_constants::match_continuous))
                return runtime.none();
            auto *compiled_pattern = runtime.allocate<RePatternObject>(
                module_type(runtime, "Pattern"),
                pattern->value,
                flags,
                std::move(compiled));
            return new_match(runtime, module_type(runtime, "Match"), compiled_pattern, text->value, match);
        }

        Object *re_search(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "search", args);
            require_positional_range(&runtime, "search", args, 2, 3);
            auto *pattern = dynamic_cast<StrObject *>(args.positional[0]);
            auto *text = dynamic_cast<StrObject *>(args.positional[1]);
            if (!pattern)
                raise_type_error(&runtime, "search() pattern must be str");
            if (!text)
                raise_type_error(&runtime, "search() string must be str");
            const std::uint32_t flags = args.positional.size() == 3 ? require_flags(runtime, args.positional[2]) : 0U;
            std::regex compiled = compile_regex(pattern->value, flags);
            std::smatch match;
            if (!std::regex_search(text->value, match, compiled))
                return runtime.none();
            auto *compiled_pattern = runtime.allocate<RePatternObject>(
                module_type(runtime, "Pattern"),
                pattern->value,
                flags,
                std::move(compiled));
            return new_match(runtime, module_type(runtime, "Match"), compiled_pattern, text->value, match);
        }

        constexpr NativeModuleFunctionDef re_methods[] = {
            {"compile", re_compile, "Compile a regular expression pattern.", "($module, pattern, flags=0, /)"},
            {"match", re_match, "Match pattern at the start of the string.", "($module, pattern, string, flags=0, /)"},
            {"search", re_search, "Search the string for a match.", "($module, pattern, string, flags=0, /)"},
        };

        constexpr NativeModuleDef re_module = {
            "_re",
            "Provide regular expression substrate objects.",
            re_methods,
        };
    }

        void initialize_regex_module(Runtime &runtime, ModuleObject &module)
        {
        TypeSlots pattern_slots;
        pattern_slots.repr = pattern_repr;
        TypeSlots match_slots;
        match_slots.repr = match_repr;
        match_slots.traverse = match_traverse;

        TypeObject *pattern_type = new_module_type(runtime, "Pattern", pattern_slots);
        auto pattern_type_root = runtime.root(pattern_type);
        TypeObject *match_type = new_module_type(runtime, "Match", match_slots);
        auto match_type_root = runtime.root(match_type);

        pattern_type_root->dict->set(runtime.new_str("match"),
                                     runtime.new_method_descriptor("match",
                                                                   pattern_type_root.get(),
                                                                   pattern_match,
                                                                   "Match this pattern at the start of a string.",
                                                                   "($self, string, /)"));
        match_type_root->dict->set(runtime.new_str("group"),
                                   runtime.new_method_descriptor("group",
                                                                 match_type_root.get(),
                                                                 match_group,
                                                                 "Return one subgroup of the match.",
                                                                 "($self, group=0, /)"));

        initialize_native_module(runtime, module, re_module);
        module.dict()->set(runtime.new_str("Pattern"), pattern_type_root.get());
        module.dict()->set(runtime.new_str("Match"), match_type_root.get());
        module.dict()->set(runtime.new_str("__all__"), runtime.new_tuple({
                                                        runtime.new_str("compile"),
                                                        runtime.new_str("match"),
                                                        runtime.new_str("search"),
                                                        runtime.new_str("Pattern"),
                                                        runtime.new_str("Match"),
                                                    }));
    }
}
