#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"

#include "object_utils.hpp"
#include "type_initialization.hpp"

#include <sstream>

namespace cxxpython
{
    namespace
    {
        std::string traceback_repr(const Object &object)
        {
            std::ostringstream out;
            out << "<traceback object at " << &object << ">";
            return out.str();
        }

        void traceback_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *traceback = static_cast<const TracebackObject *>(&object);
            if (traceback->next())
                visitor(traceback->next());
            if (traceback->frame())
                visitor(traceback->frame());
        }

        TracebackObject *require_traceback(Runtime &runtime, Object *instance, const char *name)
        {
            auto *traceback = dynamic_cast<TracebackObject *>(instance);
            if (!traceback)
                raise_type_error(&runtime, std::string(name) + " descriptor receiver does not have traceback layout");
            return traceback;
        }

        Object *traceback_next_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            TracebackObject *traceback = require_traceback(runtime, instance, "tb_next");
            return traceback->next() ? static_cast<Object *>(traceback->next()) : static_cast<Object *>(runtime.none());
        }

        Object *traceback_frame_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            TracebackObject *traceback = require_traceback(runtime, instance, "tb_frame");
            if (!traceback->frame())
                return runtime.none();
            return traceback->frame();
        }

        Object *traceback_lasti_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            TracebackObject *traceback = require_traceback(runtime, instance, "tb_lasti");
            return runtime.new_int(static_cast<std::int64_t>(traceback->last_instruction_index()));
        }

        Object *traceback_lineno_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            TracebackObject *traceback = require_traceback(runtime, instance, "tb_lineno");
            return runtime.new_int(static_cast<std::int64_t>(traceback->line()));
        }
    }

    TracebackObject::TracebackObject(TypeObject *type,
                                     TracebackObject *next,
                                     Frame *frame,
                                     std::size_t last_instruction_index,
                                     std::uint32_t line)
        : Object(type),
          next_(next),
          frame_(frame),
          last_instruction_index_(last_instruction_index),
          line_(line)
    {
    }

    TracebackObject *TracebackObject::next() const noexcept
    {
        return next_;
    }

    Frame *TracebackObject::frame() const noexcept
    {
        return frame_;
    }

    std::size_t TracebackObject::last_instruction_index() const noexcept
    {
        return last_instruction_index_;
    }

    std::uint32_t TracebackObject::line() const noexcept
    {
        return line_;
    }

    TypeSlots make_traceback_slots()
    {
        return TypeSlots{
            .repr = traceback_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
            .traverse = traceback_traverse,
        };
    }

    void initialize_traceback_type_descriptors(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("tb_next"),
                        runtime.new_getset_descriptor("tb_next",
                                                      type,
                                                      traceback_next_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("tb_frame"),
                        runtime.new_getset_descriptor("tb_frame",
                                                      type,
                                                      traceback_frame_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("tb_lasti"),
                        runtime.new_getset_descriptor("tb_lasti",
                                                      type,
                                                      traceback_lasti_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("tb_lineno"),
                        runtime.new_getset_descriptor("tb_lineno",
                                                      type,
                                                      traceback_lineno_getter,
                                                      nullptr));
    }
}
