#include "module_registry.hpp"

#include "cxxpython/runtime.hpp"

#include "../Objects/object_utils.hpp"

#include <fstream>
#include <ios>
#include <iostream>
#include <memory>
#include <sstream>

namespace cxxpython
{
    namespace
    {
        Object *io_stdin(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "_stdin", args);
            require_positional_count(&runtime, "_stdin", args, 0);
            return runtime.stdin_object();
        }

        Object *io_stdout(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "_stdout", args);
            require_positional_count(&runtime, "_stdout", args, 0);
            return runtime.stdout_object();
        }

        Object *io_stderr(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "_stderr", args);
            require_positional_count(&runtime, "_stderr", args, 0);
            return runtime.stderr_object();
        }

        Object *io_open(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "open", args);
            if (args.positional.empty() || args.positional.size() > 2)
                raise_type_error(&runtime, "open() expected 1 or 2 positional arguments");
            auto *path = dynamic_cast<StrObject *>(args.positional[0]);
            if (!path)
                raise_type_error(&runtime, "open() path must be str");
            std::string mode = "r";
            if (args.positional.size() == 2)
            {
                auto *mode_text = dynamic_cast<StrObject *>(args.positional[1]);
                if (!mode_text)
                    raise_type_error(&runtime, "open() mode must be str");
                mode = mode_text->value;
            }
            if (mode != "r" && mode != "w" && mode != "a")
                raise_value_error(&runtime, "open() only supports text modes 'r', 'w', and 'a'");

            std::ios::openmode open_mode = std::ios::in;
            if (mode == "w")
                open_mode = std::ios::out | std::ios::trunc;
            else if (mode == "a")
                open_mode = std::ios::out | std::ios::app;

            auto stream = std::make_unique<std::fstream>(path->value, open_mode);
            if (!stream->is_open())
                throw PythonError(runtime.new_os_error("open() failed for '" + path->value + "'"));
            std::fstream *raw = stream.get();
            std::istream *input = mode == "r" ? raw : nullptr;
            std::ostream *output = mode == "r" ? nullptr : raw;
            return runtime.new_text_io(path->value, input, output, true, std::move(stream));
        }

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

        struct NativeMethodDef
        {
            const char *name;
            MethodDescriptorCallback callback;
            const char *doc;
            const char *text_signature;
        };

        const NativeMethodDef text_io_methods[] = {
            {
                "read",
                text_io_read_method,
                "Read all data from the stream.",
                "($self, /)",
            },
            {
                "readline",
                text_io_readline_method,
                "Read one line from the stream.",
                "($self, /)",
            },
            {
                "write",
                text_io_write_method,
                "Write a string to the stream and return the number of characters written.",
                "($self, text, /)",
            },
            {
                "flush",
                text_io_flush_method,
                "Flush the stream.",
                "($self, /)",
            },
            {
                "close",
                text_io_close_method,
                "Close the stream.",
                "($self, /)",
            },
            {
                "closed",
                text_io_closed_method,
                "Return True if the stream is closed.",
                "($self, /)",
            },
        };

        constexpr NativeModuleFunctionDef io_methods[] = {
            {
                "_stdin",
                io_stdin,
                "Return the interpreter standard input stream.",
                "($module, /)",
            },
            {
                "_stdout",
                io_stdout,
                "Return the interpreter standard output stream.",
                "($module, /)",
            },
            {
                "_stderr",
                io_stderr,
                "Return the interpreter standard error stream.",
                "($module, /)",
            },
            {
                "open",
                io_open,
                "Open file and return a stream.",
                "($module, file, mode='r', /)",
            },
        };
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
        for (const NativeMethodDef &method : text_io_methods)
        {
            type->dict->set(runtime.new_str(method.name),
                            runtime.new_method_descriptor(method.name,
                                                          type,
                                                          method.callback,
                                                          method.doc,
                                                          method.text_signature));
        }
    }

    void initialize_io_module(Runtime &runtime, ModuleObject &module)
    {
        DictObject *dict = module.dict();
        dict->set(runtime.new_str("TextIOWrapper"), runtime.text_io_type());
        add_module_functions(runtime, module, io_methods);
    }
}
