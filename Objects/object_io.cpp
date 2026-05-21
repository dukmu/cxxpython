#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace cxxpython
{
    namespace
    {
        std::string text_io_repr(const Object &object)
        {
            auto *stream = static_cast<const TextIOObject *>(&object);
            return "<_io.TextIOWrapper name='" + stream->name() + "'>";
        }

        void ensure_open(Runtime *runtime, const TextIOObject *stream)
        {
            if (!stream || stream->closed())
                raise_value_error(runtime, "I/O operation on closed file");
        }

        TextIOObject *as_text_io(Runtime &runtime, Object *self)
        {
            auto *stream = dynamic_cast<TextIOObject *>(self);
            if (!stream)
                raise_type_error(&runtime, "TextIOWrapper receiver does not have text IO layout");
            return stream;
        }

        Object *text_io_read_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "TextIOWrapper.read() takes exactly zero arguments");
            return runtime.new_str(as_text_io(runtime, self)->read_all());
        }

        Object *text_io_readline_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "TextIOWrapper.readline() takes exactly zero arguments");
            return runtime.new_str(as_text_io(runtime, self)->readline());
        }

        Object *text_io_write_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 1)
                raise_type_error(&runtime, "TextIOWrapper.write() takes exactly one argument");
            auto *text = dynamic_cast<StrObject *>(args[0]);
            if (!text)
                raise_type_error(&runtime, "TextIOWrapper.write() argument must be str");
            as_text_io(runtime, self)->write(text->value);
            return runtime.new_int(static_cast<std::int64_t>(text->value.size()));
        }

        Object *text_io_flush_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "TextIOWrapper.flush() takes exactly zero arguments");
            as_text_io(runtime, self)->flush();
            return runtime.none();
        }

        Object *text_io_close_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "TextIOWrapper.close() takes exactly zero arguments");
            as_text_io(runtime, self)->close();
            return runtime.none();
        }

        Object *text_io_closed_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "TextIOWrapper.closed() takes exactly zero arguments");
            return as_text_io(runtime, self)->closed() ? runtime.true_value() : runtime.false_value();
        }
    }

    TextIOObject::TextIOObject(TypeObject *type,
                               std::string name,
                               std::istream *input,
                               std::ostream *output,
                               bool owns_streams,
                               std::unique_ptr<std::iostream> owned_stream)
        : Object(type),
          name_(std::move(name)),
          input_(input),
          output_(output),
          owns_streams_(owns_streams),
          owned_stream_(std::move(owned_stream))
    {
        if (owned_stream_)
        {
            input_ = owned_stream_.get();
            output_ = owned_stream_.get();
        }
    }

    TextIOObject::~TextIOObject()
    {
        if (owns_streams_)
            close();
    }

    const std::string &TextIOObject::name() const noexcept
    {
        return name_;
    }

    bool TextIOObject::readable() const noexcept
    {
        return input_ != nullptr;
    }

    bool TextIOObject::writable() const noexcept
    {
        return output_ != nullptr;
    }

    bool TextIOObject::closed() const noexcept
    {
        return closed_;
    }

    std::string TextIOObject::read_all()
    {
        ensure_open(runtime(), this);
        if (!input_)
            throw PythonError(runtime()->new_os_error("not readable"));
        std::ostringstream buffer;
        buffer << input_->rdbuf();
        if (input_->bad())
            throw PythonError(runtime()->new_os_error("read failed"));
        return buffer.str();
    }

    std::string TextIOObject::readline()
    {
        ensure_open(runtime(), this);
        if (!input_)
            throw PythonError(runtime()->new_os_error("not readable"));
        std::string line;
        if (!std::getline(*input_, line))
            return "";
        line.push_back('\n');
        return line;
    }

    void TextIOObject::write(std::string_view text)
    {
        ensure_open(runtime(), this);
        if (!output_)
            throw PythonError(runtime()->new_os_error("not writable"));
        (*output_) << text;
        if (!*output_)
            throw PythonError(runtime()->new_os_error("write failed"));
    }

    void TextIOObject::flush()
    {
        ensure_open(runtime(), this);
        if (output_)
            output_->flush();
    }

    void TextIOObject::close()
    {
        if (closed_)
            return;
        flush();
        if (owned_stream_)
        {
            auto *file = dynamic_cast<std::fstream *>(owned_stream_.get());
            if (file)
                file->close();
        }
        closed_ = true;
    }

    TypeSlots make_text_io_slots()
    {
        return TypeSlots{
            .repr = text_io_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
        };
    }

    void initialize_text_io_methods(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("read"),
                        runtime.new_method_descriptor("read", type, text_io_read_method));
        type->dict->set(runtime.new_str("readline"),
                        runtime.new_method_descriptor("readline", type, text_io_readline_method));
        type->dict->set(runtime.new_str("write"),
                        runtime.new_method_descriptor("write", type, text_io_write_method));
        type->dict->set(runtime.new_str("flush"),
                        runtime.new_method_descriptor("flush", type, text_io_flush_method));
        type->dict->set(runtime.new_str("close"),
                        runtime.new_method_descriptor("close", type, text_io_close_method));
        type->dict->set(runtime.new_str("closed"),
                        runtime.new_method_descriptor("closed", type, text_io_closed_method));
    }
}
