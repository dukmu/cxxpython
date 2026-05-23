#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"
#include "type_initialization.hpp"

#include <charconv>
#include <utility>

namespace cxxpython
{
    namespace
    {
        std::string none_repr(const Object &object)
        {
            (void)object;
            return "None";
        }

        std::int64_t none_hash(const Object &object)
        {
            (void)object;
            return 0;
        }

        bool none_equal(const Object &left, const Object &right)
        {
            return &left == &right;
        }

        bool none_truth(const Object &object)
        {
            (void)object;
            return false;
        }

        std::string not_implemented_repr(const Object &object)
        {
            (void)object;
            return "NotImplemented";
        }

        bool not_implemented_equal(const Object &left, const Object &right)
        {
            return &left == &right;
        }

        std::string int_repr(const Object &object)
        {
            return std::to_string(static_cast<const IntObject *>(&object)->value);
        }

        std::int64_t int_hash(const Object &object)
        {
            return static_cast<const IntObject *>(&object)->value;
        }

        bool int_equal(const Object &left, const Object &right)
        {
            auto *right_int = dynamic_cast<const IntObject *>(&right);
            return right_int && static_cast<const IntObject *>(&left)->value == right_int->value;
        }

        bool int_truth(const Object &object)
        {
            return static_cast<const IntObject *>(&object)->value != 0;
        }

        Object *bool_result(Runtime *runtime, bool value)
        {
            return value ? runtime->true_value() : runtime->false_value();
        }

        Object *int_rich_compare(const Object &left, const Object &right, RichCompareOp op)
        {
            auto *right_int = dynamic_cast<const IntObject *>(&right);
            if (!right_int)
                return left.runtime()->not_implemented();
            const std::int64_t left_value = static_cast<const IntObject *>(&left)->value;
            const std::int64_t right_value = right_int->value;
            switch (op)
            {
            case RichCompareOp::Equal:
                return bool_result(left.runtime(), left_value == right_value);
            case RichCompareOp::NotEqual:
                return bool_result(left.runtime(), left_value != right_value);
            case RichCompareOp::Less:
                return bool_result(left.runtime(), left_value < right_value);
            case RichCompareOp::LessEqual:
                return bool_result(left.runtime(), left_value <= right_value);
            case RichCompareOp::Greater:
                return bool_result(left.runtime(), left_value > right_value);
            case RichCompareOp::GreaterEqual:
                return bool_result(left.runtime(), left_value >= right_value);
            }
            return left.runtime()->not_implemented();
        }

        [[noreturn]] void raise_unsupported_unary_op(const Object &operand, UnaryOp op)
        {
            raise_type_error(operand.runtime(), unsupported_unary_op_message(op, &operand));
        }

        Object *int_binary_op(const Object &left, const Object &right, BinaryOp op)
        {
            auto *right_int = dynamic_cast<const IntObject *>(&right);
            if (!right_int)
                return left.runtime()->not_implemented();
            auto *left_int = static_cast<const IntObject *>(&left);
            switch (op)
            {
            case BinaryOp::Add:
                return left.runtime()->new_int(left_int->value + right_int->value);
            case BinaryOp::Subtract:
                return left.runtime()->new_int(left_int->value - right_int->value);
            case BinaryOp::Multiply:
                return left.runtime()->new_int(left_int->value * right_int->value);
            case BinaryOp::TrueDivide:
            case BinaryOp::FloorDivide:
            case BinaryOp::Modulo:
            case BinaryOp::Power:
                break;
            }
            return left.runtime()->not_implemented();
        }

        Object *int_unary_op(const Object &operand, UnaryOp op)
        {
            auto *value = static_cast<const IntObject *>(&operand);
            switch (op)
            {
            case UnaryOp::Positive:
                return operand.runtime()->new_int(value->value);
            case UnaryOp::Negative:
                return operand.runtime()->new_int(-value->value);
            case UnaryOp::Invert:
                return operand.runtime()->new_int(~value->value);
            case UnaryOp::Not:
                return is_true(&operand) ? operand.runtime()->false_value() : operand.runtime()->true_value();
            }
            raise_unsupported_unary_op(operand, op);
        }

        std::int64_t parse_int_literal(Runtime *runtime, const StrObject *text)
        {
            std::int64_t value = 0;
            const char *begin = text->value.data();
            const char *end = begin + text->value.size();
            const auto [position, error] = std::from_chars(begin, end, value);
            if (error == std::errc{} && position == end)
                return value;
            raise_value_error(runtime, "invalid literal for int() with base 10: '" + text->value + "'");
        }

        std::int64_t int_constructor_value(Runtime *runtime, Object *object)
        {
            if (auto *integer = dynamic_cast<IntObject *>(object))
                return integer->value;
            if (auto *text = dynamic_cast<StrObject *>(object))
                return parse_int_literal(runtime, text);
            raise_type_error(runtime,
                             "int() argument must be a string, a bytes-like object or a real number");
        }

        Object *int_new(TypeObject &type, const CallArguments &args)
        {
            require_no_keywords(type.runtime(), "int", args);
            require_positional_at_most(type.runtime(), "int", args, 1);
            const std::int64_t value = args.positional.empty()
                                           ? 0
                                           : int_constructor_value(type.runtime(), args.positional[0]);
            return type.runtime()->allocate<IntObject>(&type, value);
        }

        void int_init(Object &object, const CallArguments &args)
        {
            require_no_keywords(object.runtime(), "int", args);
            require_positional_at_most(object.runtime(), "int", args, 1);
        }

        std::string bool_repr(const Object &object)
        {
            return static_cast<const IntObject *>(&object)->value != 0 ? "True" : "False";
        }

        Object *bool_new(TypeObject &type, const CallArguments &args)
        {
            require_no_keywords(type.runtime(), "bool", args);
            require_positional_at_most(type.runtime(), "bool", args, 1);
            if (args.positional.empty())
                return type.runtime()->false_value();
            return is_true(args.positional[0])
                       ? type.runtime()->true_value()
                       : type.runtime()->false_value();
        }

        void bool_init(Object &object, const CallArguments &args)
        {
            require_no_keywords(object.runtime(), "bool", args);
            require_positional_at_most(object.runtime(), "bool", args, 1);
        }

        std::string str_repr(const Object &object)
        {
            return "'" + escape_single_quoted(static_cast<const StrObject *>(&object)->value) + "'";
        }

        std::int64_t str_hash(const Object &object)
        {
            return static_cast<std::int64_t>(std::hash<std::string>{}(static_cast<const StrObject *>(&object)->value));
        }

        bool str_equal(const Object &left, const Object &right)
        {
            auto *right_str = dynamic_cast<const StrObject *>(&right);
            return right_str && static_cast<const StrObject *>(&left)->value == right_str->value;
        }

        bool str_truth(const Object &object)
        {
            return !static_cast<const StrObject *>(&object)->value.empty();
        }

        Object *str_rich_compare(const Object &left, const Object &right, RichCompareOp op)
        {
            auto *right_str = dynamic_cast<const StrObject *>(&right);
            if (!right_str)
                return left.runtime()->not_implemented();
            const std::string &left_value = static_cast<const StrObject *>(&left)->value;
            const std::string &right_value = right_str->value;
            switch (op)
            {
            case RichCompareOp::Equal:
                return bool_result(left.runtime(), left_value == right_value);
            case RichCompareOp::NotEqual:
                return bool_result(left.runtime(), left_value != right_value);
            case RichCompareOp::Less:
                return bool_result(left.runtime(), left_value < right_value);
            case RichCompareOp::LessEqual:
                return bool_result(left.runtime(), left_value <= right_value);
            case RichCompareOp::Greater:
                return bool_result(left.runtime(), left_value > right_value);
            case RichCompareOp::GreaterEqual:
                return bool_result(left.runtime(), left_value >= right_value);
            }
            return left.runtime()->not_implemented();
        }

        Object *str_binary_op(const Object &left, const Object &right, BinaryOp op)
        {
            auto *right_str = dynamic_cast<const StrObject *>(&right);
            if (!right_str || op != BinaryOp::Add)
                return left.runtime()->not_implemented();
            auto *left_str = static_cast<const StrObject *>(&left);
            return left.runtime()->new_str(left_str->value + right_str->value);
        }

        std::size_t str_length(const Object &object)
        {
            return static_cast<const StrObject *>(&object)->value.size();
        }

        std::int64_t str_index(Object &object, Object *key)
        {
            auto *index = dynamic_cast<IntObject *>(key);
            if (!index)
                raise_type_error(object.runtime(), "string indices must be integers");
            return index->value;
        }

        Object *str_getitem(Object &object, Object *key)
        {
            auto &text = static_cast<StrObject &>(object);
            if (auto *slice = dynamic_cast<SliceObject *>(key))
            {
                SliceRange range = normalize_slice(*object.runtime(), *slice, static_cast<std::int64_t>(text.value.size()));
                std::string result;
                result.reserve(range.length);
                std::int64_t index = range.start;
                for (std::size_t count = 0; count < range.length; ++count)
                {
                    result.push_back(text.value[static_cast<std::size_t>(index)]);
                    index += range.step;
                }
                return object.runtime()->new_str(std::move(result));
            }
            const std::int64_t size = static_cast<std::int64_t>(text.value.size());
            const std::int64_t index = str_index(object, key);
            const std::int64_t resolved = index < 0 ? size + index : index;
            if (resolved < 0 || resolved >= size)
                raise_index_error(object.runtime(), "string index out of range");
            return object.runtime()->new_str(std::string(1, text.value[static_cast<std::size_t>(resolved)]));
        }

        bool str_contains(Object &object, Object *key)
        {
            auto *needle = dynamic_cast<StrObject *>(key);
            if (!needle)
                raise_type_error(object.runtime(), "'in <string>' requires string as left operand, not " +
                                                   type_qualified_name(key ? key->type() : nullptr));
            const std::string &haystack = static_cast<StrObject *>(&object)->value;
            return haystack.find(needle->value) != std::string::npos;
        }

        Object *str_iter(Object &object)
        {
            return object.runtime()->new_sequence_iterator(&object);
        }

        void validate_str_constructor_args(Runtime *runtime, const CallArguments &args)
        {
            require_no_keywords(runtime, "str", args);
            require_positional_at_most(runtime, "str", args, 1);
        }

        Object *str_new(TypeObject &type, const CallArguments &args)
        {
            validate_str_constructor_args(type.runtime(), args);
            if (args.positional.empty())
                return type.runtime()->allocate<StrObject>(&type, "");
            return type.runtime()->allocate<StrObject>(&type, object_str(args.positional[0]));
        }

        void str_init(Object &object, const CallArguments &args)
        {
            validate_str_constructor_args(object.runtime(), args);
        }

        std::string bytes_repr_value(const std::string &value)
        {
            constexpr char hex[] = "0123456789abcdef";
            std::string result = "b'";
            for (unsigned char byte : value)
            {
                if (byte == '\'')
                    result += "\\'";
                else if (byte == '\\')
                    result += "\\\\";
                else if (byte == '\n')
                    result += "\\n";
                else if (byte == '\r')
                    result += "\\r";
                else if (byte == '\t')
                    result += "\\t";
                else if (byte >= 32 && byte < 127)
                    result.push_back(static_cast<char>(byte));
                else
                {
                    result += "\\x";
                    result.push_back(hex[byte >> 4]);
                    result.push_back(hex[byte & 0x0f]);
                }
            }
            result += "'";
            return result;
        }

        std::string bytes_repr(const Object &object)
        {
            return bytes_repr_value(static_cast<const BytesObject *>(&object)->value);
        }

        std::int64_t bytes_hash(const Object &object)
        {
            return static_cast<std::int64_t>(std::hash<std::string>{}(static_cast<const BytesObject *>(&object)->value));
        }

        bool bytes_equal(const Object &left, const Object &right)
        {
            auto *right_bytes = dynamic_cast<const BytesObject *>(&right);
            return right_bytes && static_cast<const BytesObject *>(&left)->value == right_bytes->value;
        }

        bool bytes_truth(const Object &object)
        {
            return !static_cast<const BytesObject *>(&object)->value.empty();
        }

        Object *bytes_rich_compare(const Object &left, const Object &right, RichCompareOp op)
        {
            auto *right_bytes = dynamic_cast<const BytesObject *>(&right);
            if (!right_bytes)
                return left.runtime()->not_implemented();
            const std::string &left_value = static_cast<const BytesObject *>(&left)->value;
            const std::string &right_value = right_bytes->value;
            switch (op)
            {
            case RichCompareOp::Equal:
                return bool_result(left.runtime(), left_value == right_value);
            case RichCompareOp::NotEqual:
                return bool_result(left.runtime(), left_value != right_value);
            case RichCompareOp::Less:
                return bool_result(left.runtime(), left_value < right_value);
            case RichCompareOp::LessEqual:
                return bool_result(left.runtime(), left_value <= right_value);
            case RichCompareOp::Greater:
                return bool_result(left.runtime(), left_value > right_value);
            case RichCompareOp::GreaterEqual:
                return bool_result(left.runtime(), left_value >= right_value);
            }
            return left.runtime()->not_implemented();
        }

        Object *bytes_binary_op(const Object &left, const Object &right, BinaryOp op)
        {
            auto *right_bytes = dynamic_cast<const BytesObject *>(&right);
            if (!right_bytes || op != BinaryOp::Add)
                return left.runtime()->not_implemented();
            auto *left_bytes = static_cast<const BytesObject *>(&left);
            return left.runtime()->new_bytes(left_bytes->value + right_bytes->value);
        }

        std::size_t bytes_length(const Object &object)
        {
            return static_cast<const BytesObject *>(&object)->value.size();
        }

        Object *bytes_getitem(Object &object, Object *key)
        {
            auto &bytes = static_cast<BytesObject &>(object);
            if (auto *slice = dynamic_cast<SliceObject *>(key))
            {
                SliceRange range = normalize_slice(*object.runtime(), *slice, static_cast<std::int64_t>(bytes.value.size()));
                std::string result;
                result.reserve(range.length);
                std::int64_t index = range.start;
                for (std::size_t count = 0; count < range.length; ++count)
                {
                    result.push_back(bytes.value[static_cast<std::size_t>(index)]);
                    index += range.step;
                }
                return object.runtime()->new_bytes(std::move(result));
            }
            auto *index = dynamic_cast<IntObject *>(key);
            if (!index)
                raise_type_error(object.runtime(), "bytes indices must be integers or slices");
            const std::int64_t size = static_cast<std::int64_t>(bytes.value.size());
            const std::int64_t resolved = index->value < 0 ? size + index->value : index->value;
            if (resolved < 0 || resolved >= size)
                raise_index_error(object.runtime(), "index out of range");
            const auto byte = static_cast<unsigned char>(bytes.value[static_cast<std::size_t>(resolved)]);
            return object.runtime()->new_int(static_cast<std::int64_t>(byte));
        }

        bool bytes_contains(Object &object, Object *key)
        {
            const std::string &haystack = static_cast<BytesObject *>(&object)->value;
            if (auto *integer = dynamic_cast<IntObject *>(key))
            {
                if (integer->value < 0 || integer->value > 255)
                    raise_value_error(object.runtime(), "byte must be in range(0, 256)");
                const char needle = static_cast<char>(integer->value);
                return haystack.find(needle) != std::string::npos;
            }
            auto *needle = dynamic_cast<BytesObject *>(key);
            if (!needle)
                raise_type_error(object.runtime(), "'in <bytes>' requires int or bytes as left operand");
            return haystack.find(needle->value) != std::string::npos;
        }

        Object *bytes_iter(Object &object)
        {
            return object.runtime()->new_sequence_iterator(&object);
        }

        void append_byte_from_int(Runtime *runtime, std::string &result, Object *object)
        {
            auto *integer = dynamic_cast<IntObject *>(object);
            if (!integer)
                raise_type_error(runtime, "'bytes' object cannot interpret item as an integer");
            if (integer->value < 0 || integer->value > 255)
                raise_value_error(runtime, "bytes must be in range(0, 256)");
            result.push_back(static_cast<char>(integer->value));
        }

        std::string bytes_from_iterable(Runtime *runtime, Object *iterable)
        {
            std::string result;
            for (Object *item : collect_iterable_items(runtime, iterable))
                append_byte_from_int(runtime, result, item);
            return result;
        }

        std::string bytes_from_str(Runtime *runtime, const StrObject *text, const StrObject *encoding)
        {
            if (!encoding)
                raise_type_error(runtime, "string argument without an encoding");
            if (encoding->value != "utf-8" && encoding->value != "UTF-8" && encoding->value != "ascii")
                raise_value_error(runtime, "unknown encoding: " + encoding->value);
            if (encoding->value == "ascii")
            {
                for (unsigned char byte : text->value)
                {
                    if (byte >= 128)
                        raise_value_error(runtime, "'ascii' codec can't encode character");
                }
            }
            return text->value;
        }

        Object *bytes_new(TypeObject &type, const CallArguments &args)
        {
            Runtime *runtime = type.runtime();
            require_no_keywords(runtime, "bytes", args);
            require_positional_at_most(runtime, "bytes", args, 2);
            if (args.positional.empty())
                return runtime->allocate<BytesObject>(&type, "");

            Object *source = args.positional[0];
            if (auto *bytes = dynamic_cast<BytesObject *>(source))
            {
                if (args.positional.size() != 1)
                    raise_type_error(runtime, "encoding without a string argument");
                return runtime->allocate<BytesObject>(&type, bytes->value);
            }
            if (auto *text = dynamic_cast<StrObject *>(source))
            {
                auto *encoding = args.positional.size() == 2 ? dynamic_cast<StrObject *>(args.positional[1]) : nullptr;
                if (args.positional.size() == 2 && !encoding)
                    raise_type_error(runtime, "bytes() encoding must be str");
                return runtime->allocate<BytesObject>(&type, bytes_from_str(runtime, text, encoding));
            }
            if (args.positional.size() == 2)
                raise_type_error(runtime, "encoding without a string argument");
            if (auto *size = dynamic_cast<IntObject *>(source))
            {
                if (size->value < 0)
                    raise_value_error(runtime, "negative count");
                return runtime->allocate<BytesObject>(&type, std::string(static_cast<std::size_t>(size->value), '\0'));
            }
            return runtime->allocate<BytesObject>(&type, bytes_from_iterable(runtime, source));
        }

        void bytes_init(Object &object, const CallArguments &args)
        {
            require_no_keywords(object.runtime(), "bytes", args);
            require_positional_at_most(object.runtime(), "bytes", args, 2);
        }

        BytesObject *require_bytes_receiver(Runtime &runtime, Object *self, const char *method_name)
        {
            auto *bytes = dynamic_cast<BytesObject *>(self);
            if (!bytes)
                raise_type_error(&runtime, std::string(method_name) + " receiver does not have bytes layout");
            return bytes;
        }

        Object *bytes_decode_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() > 1)
                raise_type_error(&runtime, "bytes.decode() takes at most one argument");
            const BytesObject *bytes = require_bytes_receiver(runtime, self, "bytes.decode()");
            const StrObject *encoding = nullptr;
            if (!args.empty())
            {
                encoding = dynamic_cast<StrObject *>(args[0]);
                if (!encoding)
                    raise_type_error(&runtime, "bytes.decode() encoding must be str");
            }
            StrObject default_encoding(runtime.str_type(), "utf-8");
            return runtime.new_str(bytes_from_str(&runtime,
                                                  runtime.new_str(bytes->value),
                                                  encoding ? encoding : &default_encoding));
        }

        StrObject *require_str_receiver(Runtime &runtime, Object *self, const char *method_name)
        {
            auto *text = dynamic_cast<StrObject *>(self);
            if (!text)
                raise_type_error(&runtime, std::string(method_name) + " receiver does not have str layout");
            return text;
        }

        StrObject *require_str_argument(Runtime &runtime, Object *object, const char *message)
        {
            auto *text = dynamic_cast<StrObject *>(object);
            if (!text)
                raise_type_error(&runtime, message);
            return text;
        }

        bool is_default_strip_character(char character)
        {
            return character == ' ' ||
                   character == '\t' ||
                   character == '\n' ||
                   character == '\r' ||
                   character == '\v' ||
                   character == '\f';
        }

        bool should_strip_character(char character, const StrObject *chars)
        {
            if (!chars)
                return is_default_strip_character(character);
            return chars->value.find(character) != std::string::npos;
        }

        const StrObject *strip_chars_argument(Runtime &runtime, ObjectSpan args, const char *method_name)
        {
            if (args.size() > 1)
                raise_type_error(&runtime, std::string(method_name) + "() takes at most one argument");
            if (args.empty() || args[0] == runtime.none())
                return nullptr;
            auto *chars = dynamic_cast<StrObject *>(args[0]);
            if (!chars)
                raise_type_error(&runtime, std::string(method_name) + "() argument must be str or None");
            return chars;
        }

        Object *strip_impl(Runtime &runtime,
                           Object *self,
                           ObjectSpan args,
                           const char *method_name,
                           bool left,
                           bool right)
        {
            const std::string &value = require_str_receiver(runtime, self, method_name)->value;
            const StrObject *chars = strip_chars_argument(runtime, args, method_name);
            std::size_t start = 0;
            std::size_t stop = value.size();
            if (left)
            {
                while (start < stop && should_strip_character(value[start], chars))
                    ++start;
            }
            if (right)
            {
                while (stop > start && should_strip_character(value[stop - 1], chars))
                    --stop;
            }
            return runtime.new_str(value.substr(start, stop - start));
        }

        Object *str_join_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 1)
                raise_type_error(&runtime, "str.join() takes exactly one argument");
            const std::string &separator = require_str_receiver(runtime, self, "str.join()")->value;
            Object *iterator = object_iter(args[0]);
            auto iterator_root = runtime.root(iterator);
            std::string result;
            bool first = true;
            while (true)
            {
                try
                {
                    Object *item = object_next(iterator_root.get());
                    auto item_root = runtime.root(item);
                    auto *text = dynamic_cast<StrObject *>(item_root.get());
                    if (!text)
                        raise_type_error(&runtime, "sequence item is not str");
                    if (!first)
                        result += separator;
                    first = false;
                    result += text->value;
                }
                catch (const PythonError &error)
                {
                    if (error.type() == runtime.stop_iteration_type())
                        break;
                    throw;
                }
            }
            return runtime.new_str(std::move(result));
        }

        Object *str_startswith_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 1)
                raise_type_error(&runtime, "str.startswith() takes exactly one argument");
            const std::string &value = require_str_receiver(runtime, self, "str.startswith()")->value;
            const std::string &prefix =
                require_str_argument(runtime, args[0], "startswith first arg must be str")->value;
            return value.rfind(prefix, 0) == 0 ? runtime.true_value() : runtime.false_value();
        }

        Object *str_endswith_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 1)
                raise_type_error(&runtime, "str.endswith() takes exactly one argument");
            const std::string &value = require_str_receiver(runtime, self, "str.endswith()")->value;
            const std::string &suffix =
                require_str_argument(runtime, args[0], "endswith first arg must be str")->value;
            if (suffix.size() > value.size())
                return runtime.false_value();
            return value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0
                       ? runtime.true_value()
                       : runtime.false_value();
        }

        Object *str_strip_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            return strip_impl(runtime, self, args, "str.strip", true, true);
        }

        Object *str_lstrip_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            return strip_impl(runtime, self, args, "str.lstrip", true, false);
        }

        Object *str_rstrip_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            return strip_impl(runtime, self, args, "str.rstrip", false, true);
        }

        Object *int_bool_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "int.__bool__() takes exactly zero arguments");
            return is_true(self) ? runtime.true_value() : runtime.false_value();
        }

        Object *int_identity_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "int conversion method takes exactly zero arguments");
            auto *integer = dynamic_cast<IntObject *>(self);
            if (!integer)
                raise_type_error(&runtime, "int conversion receiver does not have int layout");
            return integer;
        }
    }

    NoneObject::NoneObject(TypeObject *type)
        : Object(type)
    {
    }

    NotImplementedObject::NotImplementedObject(TypeObject *type)
        : Object(type)
    {
    }

    IntObject::IntObject(TypeObject *type, std::int64_t value)
        : Object(type), value(value)
    {
    }

    BoolObject::BoolObject(TypeObject *type, bool value)
        : IntObject(type, value ? 1 : 0)
    {
    }

    StrObject::StrObject(TypeObject *type, std::string value)
        : Object(type), value(std::move(value))
    {
    }

    BytesObject::BytesObject(TypeObject *type, std::string value)
        : Object(type), value(std::move(value))
    {
    }

    TypeSlots make_none_slots()
    {
        return TypeSlots{
            .repr = none_repr,
            .hash = none_hash,
            .equal = none_equal,
            .truth = none_truth,
            .traverse = default_traverse,
        };
    }

    TypeSlots make_not_implemented_slots()
    {
        return TypeSlots{
            .repr = not_implemented_repr,
            .hash = default_hash,
            .equal = not_implemented_equal,
            .truth = default_truth,
            .traverse = default_traverse,
        };
    }

    TypeSlots make_int_slots()
    {
        return TypeSlots{
            .repr = int_repr,
            .hash = int_hash,
            .equal = int_equal,
            .rich_compare = int_rich_compare,
            .truth = int_truth,
            .traverse = default_traverse,
            .binary_op = int_binary_op,
            .unary_op = int_unary_op,
            .new_object = int_new,
            .init = int_init,
        };
    }

    TypeSlots make_bool_slots()
    {
        return TypeSlots{
            .repr = bool_repr,
            .hash = int_hash,
            .equal = int_equal,
            .rich_compare = int_rich_compare,
            .truth = int_truth,
            .traverse = default_traverse,
            .binary_op = int_binary_op,
            .unary_op = int_unary_op,
            .new_object = bool_new,
            .init = bool_init,
        };
    }

    TypeSlots make_str_slots()
    {
        return TypeSlots{
            .repr = str_repr,
            .hash = str_hash,
            .equal = str_equal,
            .rich_compare = str_rich_compare,
            .truth = str_truth,
            .traverse = default_traverse,
            .binary_op = str_binary_op,
            .length = str_length,
            .getitem = str_getitem,
            .contains = str_contains,
            .iter = str_iter,
            .new_object = str_new,
            .init = str_init,
        };
    }

    TypeSlots make_bytes_slots()
    {
        return TypeSlots{
            .repr = bytes_repr,
            .hash = bytes_hash,
            .equal = bytes_equal,
            .rich_compare = bytes_rich_compare,
            .truth = bytes_truth,
            .traverse = default_traverse,
            .binary_op = bytes_binary_op,
            .length = bytes_length,
            .getitem = bytes_getitem,
            .contains = bytes_contains,
            .iter = bytes_iter,
            .new_object = bytes_new,
            .init = bytes_init,
        };
    }

    void initialize_none_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, true);
    }

    void initialize_not_implemented_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, true);
    }

    void initialize_int_type_methods(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("__bool__"),
                        runtime.new_method_descriptor("__bool__", type, int_bool_method));
        type->dict->set(runtime.new_str("__int__"),
                        runtime.new_method_descriptor("__int__", type, int_identity_method));
        type->dict->set(runtime.new_str("__index__"),
                        runtime.new_method_descriptor("__index__", type, int_identity_method));
    }

    void initialize_str_type_methods(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("join"),
                        runtime.new_method_descriptor("join",
                                                      type,
                                                      str_join_method,
                                                      "Concatenate an iterable of strings.",
                                                      "($self, iterable, /)"));
        type->dict->set(runtime.new_str("startswith"),
                        runtime.new_method_descriptor("startswith",
                                                      type,
                                                      str_startswith_method,
                                                      "Return True if the string starts with the prefix.",
                                                      "($self, prefix, /)"));
        type->dict->set(runtime.new_str("endswith"),
                        runtime.new_method_descriptor("endswith",
                                                      type,
                                                      str_endswith_method,
                                                      "Return True if the string ends with the suffix.",
                                                      "($self, suffix, /)"));
        type->dict->set(runtime.new_str("strip"),
                        runtime.new_method_descriptor("strip",
                                                      type,
                                                      str_strip_method,
                                                      "Return a copy with leading and trailing characters removed.",
                                                      "($self, chars=None, /)"));
        type->dict->set(runtime.new_str("lstrip"),
                        runtime.new_method_descriptor("lstrip",
                                                      type,
                                                      str_lstrip_method,
                                                      "Return a copy with leading characters removed.",
                                                      "($self, chars=None, /)"));
        type->dict->set(runtime.new_str("rstrip"),
                        runtime.new_method_descriptor("rstrip",
                                                      type,
                                                      str_rstrip_method,
                                                      "Return a copy with trailing characters removed.",
                                                      "($self, chars=None, /)"));
    }

    void initialize_bytes_type_methods(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("decode"),
                        runtime.new_method_descriptor("decode",
                                                      type,
                                                      bytes_decode_method,
                                                      "Decode the bytes using the given encoding.",
                                                      "($self, encoding='utf-8', /)"));
    }

    void initialize_int_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, true);
        add_order_slot_wrappers(type);
        add_numeric_slot_wrappers(type);
    }

    void initialize_bool_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, true);
        add_order_slot_wrappers(type);
        add_numeric_slot_wrappers(type);
    }

    void initialize_str_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, true);
        add_order_slot_wrappers(type);
        add_slot_wrapper(type, "__add__", SlotWrapperKind::BinaryOp, BinaryOp::Add);
        add_sequence_slot_wrappers(type, false);
    }

    void initialize_bytes_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, true);
        add_order_slot_wrappers(type);
        add_slot_wrapper(type, "__add__", SlotWrapperKind::BinaryOp, BinaryOp::Add);
        add_sequence_slot_wrappers(type, false);
    }
}
