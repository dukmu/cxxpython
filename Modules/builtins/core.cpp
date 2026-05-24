#include "builtins.hpp"

#include "../../Objects/object_utils.hpp"

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace cxxpython
{
    namespace
    {
        StrObject *keyword_as_str(Runtime &runtime, DictObject *keywords, const char *name, const char *function_name)
        {
            if (!keywords)
                return nullptr;
            Object *value = keywords->get(runtime.new_str(name));
            if (!value || value == runtime.none())
                return nullptr;
            auto *text = dynamic_cast<StrObject *>(value);
            if (!text)
                raise_type_error(&runtime, std::string(function_name) + "() " + name + " must be None or a string");
            return text;
        }

        void require_print_keywords(Runtime &runtime, const CallArguments &args)
        {
            if (!args.keywords)
                return;
            for (const DictObject::Entry &entry : args.keywords->entries())
            {
                auto *name = dynamic_cast<StrObject *>(entry.key);
                if (!name)
                    raise_type_error(&runtime, "print() keywords must be strings");
                if (name->value != "sep" && name->value != "end")
                {
                    raise_type_error(&runtime,
                                     "print() got an unexpected keyword argument '" + name->value + "'");
                }
            }
        }

        bool rich_compare_truth(Runtime &runtime, Object *left, Object *right, RichCompareOp op)
        {
            auto left_root = runtime.root(left);
            auto right_root = runtime.root(right);
            Object *result = object_rich_compare(left_root.get(), right_root.get(), op);
            auto result_root = runtime.root(result);
            return is_true(result_root.get());
        }

        Object *next_or_null(Runtime &runtime, Object *iterator)
        {
            try
            {
                return object_next(iterator);
            }
            catch (const PythonError &error)
            {
                if (error.type() == runtime.stop_iteration_type())
                    return nullptr;
                throw;
            }
        }

        Object *select_extreme_from_iterator(Runtime &runtime,
                                             std::string_view name,
                                             Object *iterable,
                                             RichCompareOp op)
        {
            Object *iterator = object_iter(iterable);
            auto iterator_root = runtime.root(iterator);
            Object *first = next_or_null(runtime, iterator_root.get());
            if (!first)
                raise_value_error(&runtime, std::string(name) + "() arg is an empty sequence");

            auto best_root = runtime.root(first);
            while (Object *candidate = next_or_null(runtime, iterator_root.get()))
            {
                auto candidate_root = runtime.root(candidate);
                if (rich_compare_truth(runtime, candidate_root.get(), best_root.get(), op))
                    best_root = runtime.root(candidate_root.get());
            }
            return best_root.get();
        }

        Object *select_extreme(Runtime &runtime,
                               std::string_view name,
                               const CallArguments &args,
                               RichCompareOp op)
        {
            require_no_keywords(&runtime, name, args);
            if (args.positional.empty())
                raise_type_error(&runtime, std::string(name) + "() expected at least one argument");

            if (args.positional.size() == 1)
                return select_extreme_from_iterator(runtime, name, args.positional[0], op);

            Object *best = args.positional.front();
            auto best_root = runtime.root(best);
            for (std::size_t index = 1; index < args.positional.size(); ++index)
            {
                Object *candidate = args.positional[index];
                auto candidate_root = runtime.root(candidate);
                if (rich_compare_truth(runtime, candidate_root.get(), best_root.get(), op))
                    best_root = runtime.root(candidate_root.get());
            }
            return best_root.get();
        }
    }

    Object *builtin_print(Runtime &runtime, const CallArguments &args)
    {
        require_print_keywords(runtime, args);
        StrObject *sep_arg = keyword_as_str(runtime, args.keywords, "sep", "print");
        StrObject *end_arg = keyword_as_str(runtime, args.keywords, "end", "print");
        const std::string sep = sep_arg ? sep_arg->value : " ";
        const std::string end = end_arg ? end_arg->value : "\n";
        std::ostream &out = runtime.stdout_stream();
        for (std::size_t index = 0; index < args.positional.size(); ++index)
        {
            if (index != 0)
                out << sep;
            out << object_str(args.positional[index]);
        }
        out << end;
        return runtime.none();
    }

    Object *builtin_len(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "len", args);
        require_positional_count(&runtime, "len", args, 1);
        return runtime.new_int(static_cast<std::int64_t>(object_length(args.positional[0])));
    }

    Object *builtin_iter(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "iter", args);
        require_positional_count(&runtime, "iter", args, 1);
        return object_iter(args.positional[0]);
    }

    Object *builtin_next(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "next", args);
        require_positional_count(&runtime, "next", args, 1);
        return object_next(args.positional[0]);
    }

    Object *builtin_reversed(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "reversed", args);
        require_positional_count(&runtime, "reversed", args, 1);
        StrObject *name = runtime.new_str("__reversed__");
        auto name_root = runtime.root(name);
        try
        {
            Object *method = object_get_attr(args.positional[0], name_root.get());
            auto method_root = runtime.root(method);
            return object_call(method_root.get(), {});
        }
        catch (const PythonError &error)
        {
            if (!error.type() || !is_subclass(error.type(), runtime.attribute_error_type()))
                throw;
        }
        return runtime.new_reverse_iterator(args.positional[0]);
    }

    Object *builtin_repr(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "repr", args);
        require_positional_count(&runtime, "repr", args, 1);
        return runtime.new_str(object_repr(args.positional[0]));
    }

    Object *builtin_hash(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "hash", args);
        require_positional_count(&runtime, "hash", args, 1);
        return runtime.new_int(object_hash(args.positional[0]));
    }

    Object *builtin_all(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "all", args);
        require_positional_count(&runtime, "all", args, 1);
        Object *iterator = object_iter(args.positional[0]);
        auto iterator_root = runtime.root(iterator);
        while (Object *item = next_or_null(runtime, iterator_root.get()))
        {
            auto item_root = runtime.root(item);
            if (!is_true(item_root.get()))
                return runtime.false_value();
        }
        return runtime.true_value();
    }

    Object *builtin_any(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "any", args);
        require_positional_count(&runtime, "any", args, 1);
        Object *iterator = object_iter(args.positional[0]);
        auto iterator_root = runtime.root(iterator);
        while (Object *item = next_or_null(runtime, iterator_root.get()))
        {
            auto item_root = runtime.root(item);
            if (is_true(item_root.get()))
                return runtime.true_value();
        }
        return runtime.false_value();
    }

    Object *builtin_sum(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "sum", args);
        require_positional_range(&runtime, "sum", args, 1, 2);

        Object *total = args.positional.size() == 2 ? args.positional[1] : runtime.new_int(0);
        auto total_root = runtime.root(total);
        Object *iterator = object_iter(args.positional[0]);
        auto iterator_root = runtime.root(iterator);
        while (Object *item = next_or_null(runtime, iterator_root.get()))
        {
            auto item_root = runtime.root(item);
            Object *next_total = object_binary_op(total_root.get(), item_root.get(), BinaryOp::Add);
            total_root = runtime.root(next_total);
        }
        return total_root.get();
    }

    Object *builtin_divmod(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "divmod", args);
        require_positional_count(&runtime, "divmod", args, 2);
        Object *result = object_binary_op(args.positional[0], args.positional[1], BinaryOp::FloorDivide);
        auto result_root = runtime.root(result);
        Object *remainder = object_binary_op(args.positional[0], args.positional[1], BinaryOp::Modulo);
        auto remainder_root = runtime.root(remainder);
        return runtime.new_tuple({result_root.get(), remainder_root.get()});
    }

    Object *builtin_min(Runtime &runtime, const CallArguments &args)
    {
        return select_extreme(runtime, "min", args, RichCompareOp::Less);
    }

    Object *builtin_max(Runtime &runtime, const CallArguments &args)
    {
        return select_extreme(runtime, "max", args, RichCompareOp::Greater);
    }

    Object *builtin_sorted(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "sorted", args);
        require_positional_count(&runtime, "sorted", args, 1);
        std::vector<Object *> items = collect_iterable_items(&runtime, args.positional[0]);
        std::stable_sort(items.begin(), items.end(), [&runtime](Object *left, Object *right)
                         { return rich_compare_truth(runtime, left, right, RichCompareOp::Less); });
        return runtime.new_list(std::move(items));
    }

    Object *builtin_format(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "format", args);
        require_positional_range(&runtime, "format", args, 1, 2);
        Object *value = args.positional[0];
        auto value_root = runtime.root(value);
        Object *spec = args.positional.size() == 2 ? args.positional[1] : runtime.new_str("");
        auto spec_root = runtime.root(spec);
        auto *format_spec = dynamic_cast<StrObject *>(spec_root.get());
        if (!format_spec)
            raise_type_error(&runtime, "format() argument 2 must be str");
        return object_format(value_root.get(), format_spec);
    }
}
