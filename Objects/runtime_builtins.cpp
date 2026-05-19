#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"
#include "cxxpython/compiler.hpp"

#include "object_utils.hpp"

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <set>
#include <sstream>
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
            for (Object *item : collect_iterable_items(&runtime, args.positional[0]))
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
            for (Object *item : collect_iterable_items(&runtime, args.positional[0]))
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
            if (args.positional.empty() || args.positional.size() > 2)
                raise_type_error(&runtime, "sum() takes one or two positional arguments");

            Object *total = args.positional.size() == 2 ? args.positional[1] : runtime.new_int(0);
            auto total_root = runtime.root(total);
            for (Object *item : collect_iterable_items(&runtime, args.positional[0]))
            {
                auto item_root = runtime.root(item);
                Object *next_total = object_binary_op(total_root.get(), item_root.get(), BinaryOp::Add);
                total_root = runtime.root(next_total);
            }
            return total_root.get();
        }

        bool rich_compare_truth(Runtime &runtime, Object *left, Object *right, RichCompareOp op)
        {
            auto left_root = runtime.root(left);
            auto right_root = runtime.root(right);
            Object *result = object_rich_compare(left_root.get(), right_root.get(), op);
            auto result_root = runtime.root(result);
            return is_true(result_root.get());
        }

        Object *select_extreme(Runtime &runtime,
                               std::string_view name,
                               const CallArguments &args,
                               RichCompareOp op)
        {
            require_no_keywords(&runtime, name, args);
            if (args.positional.empty())
                raise_type_error(&runtime, std::string(name) + "() expected at least one argument");

            std::vector<Object *> items;
            if (args.positional.size() == 1)
            {
                items = collect_iterable_items(&runtime, args.positional[0]);
                if (items.empty())
                    raise_value_error(&runtime, std::string(name) + "() arg is an empty sequence");
            }
            else
            {
                items = args.positional;
            }

            Object *best = items.front();
            auto best_root = runtime.root(best);
            for (std::size_t index = 1; index < items.size(); ++index)
            {
                Object *candidate = items[index];
                auto candidate_root = runtime.root(candidate);
                if (rich_compare_truth(runtime, candidate_root.get(), best_root.get(), op))
                    best_root = runtime.root(candidate_root.get());
            }
            return best_root.get();
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

        bool classinfo_matches(Runtime &runtime, TypeObject *type, Object *classinfo, const char *function_name)
        {
            if (auto *candidate = dynamic_cast<TypeObject *>(classinfo))
                return is_subclass(type, candidate);
            if (auto *tuple = dynamic_cast<TupleObject *>(classinfo))
            {
                for (Object *item : tuple->items())
                {
                    if (classinfo_matches(runtime, type, item, function_name))
                        return true;
                }
                return false;
            }
            raise_type_error(&runtime,
                             std::string(function_name) +
                                 "() arg 2 must be a type or tuple of types");
        }

        Object *builtin_isinstance(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "isinstance", args);
            require_positional_count(&runtime, "isinstance", args, 2);
            Object *object = args.positional[0];
            if (!object)
                raise_type_error(&runtime, "isinstance() arg 1 cannot be null");
            return classinfo_matches(runtime, object->type(), args.positional[1], "isinstance")
                       ? runtime.true_value()
                       : runtime.false_value();
        }

        Object *builtin_issubclass(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "issubclass", args);
            require_positional_count(&runtime, "issubclass", args, 2);
            auto *type = dynamic_cast<TypeObject *>(args.positional[0]);
            if (!type)
                raise_type_error(&runtime, "issubclass() arg 1 must be a class");
            return classinfo_matches(runtime, type, args.positional[1], "issubclass")
                       ? runtime.true_value()
                       : runtime.false_value();
        }

        Object *builtin_callable(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "callable", args);
            require_positional_count(&runtime, "callable", args, 1);
            Object *object = args.positional[0];
            return object && object->type() && object->type()->slots.call
                       ? runtime.true_value()
                       : runtime.false_value();
        }

        Object *builtin_id(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "id", args);
            require_positional_count(&runtime, "id", args, 1);
            return runtime.new_int(static_cast<std::int64_t>(
                reinterpret_cast<std::uintptr_t>(args.positional[0])));
        }

        Frame *current_frame_or_error(Runtime &runtime, const char *function_name)
        {
            VirtualMachine *vm = runtime.active_vm();
            Frame *frame = vm ? vm->thread_state().current_frame() : nullptr;
            if (!frame)
                raise_type_error(&runtime, std::string(function_name) + "() requires an active frame");
            return frame;
        }

        Object *builtin_globals(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "globals", args);
            require_positional_count(&runtime, "globals", args, 0);
            return current_frame_or_error(runtime, "globals")->globals();
        }

        Object *builtin_locals(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "locals", args);
            require_positional_count(&runtime, "locals", args, 0);
            return current_frame_or_error(runtime, "locals")->locals();
        }

        void collect_dict_names(std::set<std::string> &names, DictObject *dict)
        {
            if (!dict)
                return;
            for (const DictObject::Entry &entry : dict->entries())
            {
                if (auto *name = dynamic_cast<StrObject *>(entry.key))
                    names.insert(name->value);
            }
        }

        void collect_type_names(std::set<std::string> &names, TypeObject *type)
        {
            if (!type)
                return;
            if (type->mro.empty())
            {
                collect_dict_names(names, type->dict);
                return;
            }
            for (TypeObject *item : type->mro)
                collect_dict_names(names, item ? item->dict : nullptr);
        }

        void filter_inaccessible_instance_names(std::set<std::string> &names, Object *object)
        {
            if (!object || dynamic_cast<TypeObject *>(object) || dynamic_cast<ModuleObject *>(object))
                return;
            if ((object->type()->flags & TypeFlagHasInstanceDict) == 0)
                names.erase("__dict__");
        }

        std::vector<Object *> names_as_sorted_str_objects(Runtime &runtime, const std::set<std::string> &names)
        {
            std::vector<Object *> result;
            result.reserve(names.size());
            std::vector<Rooted<StrObject>> roots;
            for (const std::string &name : names)
            {
                StrObject *text = runtime.new_str(name);
                roots.push_back(runtime.root(text));
                result.push_back(text);
            }
            return result;
        }

        Object *builtin_dir(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "dir", args);
            require_positional_at_most(&runtime, "dir", args, 1);

            std::set<std::string> names;
            if (args.positional.empty())
            {
                collect_dict_names(names, current_frame_or_error(runtime, "dir")->locals());
            }
            else
            {
                Object *object = args.positional[0];
                if (auto *module = dynamic_cast<ModuleObject *>(object))
                {
                    collect_dict_names(names, module->dict());
                }
                else if (auto *type = dynamic_cast<TypeObject *>(object))
                {
                    collect_type_names(names, type);
                }
                else
                {
                    collect_dict_names(names, object ? object->instance_dict() : nullptr);
                    collect_type_names(names, object ? object->type() : nullptr);
                    filter_inaccessible_instance_names(names, object);
                }
            }
            return runtime.new_list(names_as_sorted_str_objects(runtime, names));
        }

        Object *builtin_vars(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "vars", args);
            require_positional_at_most(&runtime, "vars", args, 1);
            if (args.positional.empty())
                return current_frame_or_error(runtime, "vars")->locals();

            Object *object = args.positional[0];
            auto object_root = runtime.root(object);
            try
            {
                return object_get_attr(object_root.get(), "__dict__");
            }
            catch (const PythonError &error)
            {
                if (error.type() == runtime.attribute_error_type())
                    raise_type_error(&runtime, "vars() argument must have __dict__ attribute");
                throw;
            }
        }

        Object *builtin_format(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "format", args);
            if (args.positional.empty() || args.positional.size() > 2)
                raise_type_error(&runtime, "format() expected 1 or 2 arguments");
            Object *value = args.positional[0];
            auto value_root = runtime.root(value);
            Object *spec = args.positional.size() == 2 ? args.positional[1] : runtime.new_str("");
            auto spec_root = runtime.root(spec);
            if (!dynamic_cast<StrObject *>(spec_root.get()))
                raise_type_error(&runtime, "format() argument 2 must be str");
            Object *format_method = object_get_attr(value_root.get(), "__format__");
            auto method_root = runtime.root(format_method);
            return object_call(method_root.get(), {spec_root.get()});
        }

        CodeObject *compile_source(Runtime &runtime,
                                   std::string_view source,
                                   std::string filename,
                                   std::string_view mode)
        {
            if (mode == "eval")
                return compile_eval_input(runtime, source, std::move(filename));
            if (mode == "exec")
                return compile_exec_input(runtime, source, std::move(filename));
            raise_value_error(&runtime, "compile() mode must be 'exec' or 'eval'");
        }

        Object *builtin_compile(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "compile", args);
            require_positional_count(&runtime, "compile", args, 3);
            auto *source = dynamic_cast<StrObject *>(args.positional[0]);
            auto *filename = dynamic_cast<StrObject *>(args.positional[1]);
            auto *mode = dynamic_cast<StrObject *>(args.positional[2]);
            if (!source)
                raise_type_error(&runtime, "compile() source must be str");
            if (!filename)
                raise_type_error(&runtime, "compile() filename must be str");
            if (!mode)
                raise_type_error(&runtime, "compile() mode must be str");
            return compile_source(runtime, source->value, filename->value, mode->value);
        }

        DictObject *optional_dict_arg(Runtime &runtime,
                                      const CallArguments &args,
                                      std::size_t index,
                                      DictObject *fallback,
                                      const char *function_name,
                                      const char *argument_name)
        {
            if (args.positional.size() <= index || args.positional[index] == runtime.none())
                return fallback;
            auto *dict = dynamic_cast<DictObject *>(args.positional[index]);
            if (!dict)
            {
                raise_type_error(&runtime,
                                 std::string(function_name) + "() " +
                                     argument_name + " must be a dict");
            }
            return dict;
        }

        CodeObject *code_from_source_or_code(Runtime &runtime,
                                             Object *source_or_code,
                                             std::string_view mode,
                                             const char *function_name)
        {
            if (auto *code = dynamic_cast<CodeObject *>(source_or_code))
                return code;
            auto *source = dynamic_cast<StrObject *>(source_or_code);
            if (!source)
                raise_type_error(&runtime, std::string(function_name) + "() argument must be str or code");
            return compile_source(runtime, source->value, "<string>", mode);
        }

        Object *builtin_eval(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "eval", args);
            if (args.positional.empty() || args.positional.size() > 3)
                raise_type_error(&runtime, "eval() expected 1 to 3 arguments");
            Frame *frame = current_frame_or_error(runtime, "eval");
            DictObject *globals = optional_dict_arg(runtime, args, 1, frame->globals(), "eval", "globals");
            DictObject *locals = optional_dict_arg(runtime, args, 2, globals, "eval", "locals");
            CodeObject *code = code_from_source_or_code(runtime, args.positional[0], "eval", "eval");
            auto code_root = runtime.root(code);
            VirtualMachine *vm = runtime.active_vm();
            if (!vm)
                raise_type_error(&runtime, "eval() requires an active VM");
            return vm->execute(ExecutionRequest{
                .code = code_root.get(),
                .locals = locals,
                .globals = globals,
                .builtins = runtime.builtins_dict(),
                .mode = ExecutionMode::Eval,
            });
        }

        Object *builtin_exec(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "exec", args);
            if (args.positional.empty() || args.positional.size() > 3)
                raise_type_error(&runtime, "exec() expected 1 to 3 arguments");
            Frame *frame = current_frame_or_error(runtime, "exec");
            DictObject *globals = optional_dict_arg(runtime, args, 1, frame->globals(), "exec", "globals");
            DictObject *locals = optional_dict_arg(runtime, args, 2, globals, "exec", "locals");
            CodeObject *code = code_from_source_or_code(runtime, args.positional[0], "exec", "exec");
            auto code_root = runtime.root(code);
            VirtualMachine *vm = runtime.active_vm();
            if (!vm)
                raise_type_error(&runtime, "exec() requires an active VM");
            (void)vm->execute(ExecutionRequest{
                .code = code_root.get(),
                .locals = locals,
                .globals = globals,
                .builtins = runtime.builtins_dict(),
                .mode = ExecutionMode::Exec,
            });
            return runtime.none();
        }
    }

    void Runtime::initialize_builtins()
    {
        DictObject *dict = new_dict();
        dict->set(new_str("__name__"), new_str("builtins"));
        dict->set(new_str("__file__"), new_str(""));

        builtins_module_ = allocate<ModuleObject>(module_type_, "builtins", "", dict);
        dict->set(new_str("__builtins__"), builtins_module_);
        dict->set(new_str("NotImplemented"), not_implemented_);
        dict->set(new_str("BaseException"), base_exception_type_);
        dict->set(new_str("Exception"), exception_type_);
        dict->set(new_str("TypeError"), type_error_type_);
        dict->set(new_str("ValueError"), value_error_type_);
        dict->set(new_str("IndexError"), index_error_type_);
        dict->set(new_str("KeyError"), key_error_type_);
        dict->set(new_str("SyntaxError"), syntax_error_type_);
        dict->set(new_str("NameError"), name_error_type_);
        dict->set(new_str("UnboundLocalError"), unbound_local_error_type_);
        dict->set(new_str("AttributeError"), attribute_error_type_);
        dict->set(new_str("StopIteration"), stop_iteration_type_);
        dict->set(new_str("None"), none_);
        dict->set(new_str("True"), true_);
        dict->set(new_str("False"), false_);
        dict->set(new_str("object"), object_type_);
        dict->set(new_str("type"), type_type_);
        dict->set(new_str("int"), int_type_);
        dict->set(new_str("bool"), bool_type_);
        dict->set(new_str("str"), str_type_);
        dict->set(new_str("list"), list_type_);
        dict->set(new_str("tuple"), tuple_type_);
        dict->set(new_str("dict"), dict_type_);
        dict->set(new_str("range"), range_type_);
        dict->set(new_str("enumerate"), enumerate_type_);
        dict->set(new_str("zip"), zip_type_);
        dict->set(new_str("map"), map_type_);
        dict->set(new_str("filter"), filter_type_);
        dict->set(new_str("property"), property_type_);
        dict->set(new_str("staticmethod"), staticmethod_type_);
        dict->set(new_str("classmethod"), classmethod_type_);
        dict->set(new_str("print"), new_native_function("print", builtin_print));
        dict->set(new_str("len"), new_native_function("len", builtin_len));
        dict->set(new_str("iter"), new_native_function("iter", builtin_iter));
        dict->set(new_str("next"), new_native_function("next", builtin_next));
        dict->set(new_str("repr"), new_native_function("repr", builtin_repr));
        dict->set(new_str("hash"), new_native_function("hash", builtin_hash));
        dict->set(new_str("all"), new_native_function("all", builtin_all));
        dict->set(new_str("any"), new_native_function("any", builtin_any));
        dict->set(new_str("sum"), new_native_function("sum", builtin_sum));
        dict->set(new_str("min"), new_native_function("min", builtin_min));
        dict->set(new_str("max"), new_native_function("max", builtin_max));
        dict->set(new_str("sorted"), new_native_function("sorted", builtin_sorted));
        dict->set(new_str("isinstance"), new_native_function("isinstance", builtin_isinstance));
        dict->set(new_str("issubclass"), new_native_function("issubclass", builtin_issubclass));
        dict->set(new_str("callable"), new_native_function("callable", builtin_callable));
        dict->set(new_str("id"), new_native_function("id", builtin_id));
        dict->set(new_str("globals"), new_native_function("globals", builtin_globals));
        dict->set(new_str("locals"), new_native_function("locals", builtin_locals));
        dict->set(new_str("dir"), new_native_function("dir", builtin_dir));
        dict->set(new_str("vars"), new_native_function("vars", builtin_vars));
        dict->set(new_str("format"), new_native_function("format", builtin_format));
        dict->set(new_str("compile"), new_native_function("compile", builtin_compile));
        dict->set(new_str("eval"), new_native_function("eval", builtin_eval));
        dict->set(new_str("exec"), new_native_function("exec", builtin_exec));
    }
}
