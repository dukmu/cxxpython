#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"
#include "type_initialization.hpp"

#include <sstream>

namespace cxxpython
{
    namespace
    {
        std::string slice_repr(const Object &object)
        {
            auto *slice = static_cast<const SliceObject *>(&object);
            return "slice(" + object_repr(slice->start()) + ", " +
                   object_repr(slice->stop()) + ", " +
                   object_repr(slice->step()) + ")";
        }

        void slice_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *slice = static_cast<const SliceObject *>(&object);
            if (slice->start())
                visitor(slice->start());
            if (slice->stop())
                visitor(slice->stop());
            if (slice->step())
                visitor(slice->step());
        }

        void require_slice_argument_count(Runtime *runtime, const CallArguments &args)
        {
            const std::size_t count = args.positional.size();
            if (count >= 1 && count <= 3)
                return;
            std::ostringstream message;
            message << "slice expected 1 to 3 arguments, got " << count;
            raise_type_error(runtime, message.str());
        }

        Object *slice_new(TypeObject &type, const CallArguments &args)
        {
            Runtime *runtime = type.runtime();
            require_no_keywords(runtime, "slice", args);
            require_slice_argument_count(runtime, args);

            Object *start = runtime->none();
            Object *stop = runtime->none();
            Object *step = runtime->none();
            if (args.positional.size() == 1)
            {
                stop = args.positional[0];
            }
            else
            {
                start = args.positional[0];
                stop = args.positional[1];
                if (args.positional.size() == 3)
                    step = args.positional[2];
            }
            return runtime->allocate<SliceObject>(&type, start, stop, step);
        }

        void slice_init(Object &object, const CallArguments &args)
        {
            require_no_keywords(object.runtime(), "slice", args);
            require_slice_argument_count(object.runtime(), args);
        }

        Object *slice_indices_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (args.size() != 1)
            {
                std::ostringstream message;
                message << "slice.indices() takes exactly one argument ("
                        << args.size() << " given)";
                raise_type_error(&runtime, message.str());
            }
            auto *slice = dynamic_cast<SliceObject *>(self);
            if (!slice)
                raise_type_error(&runtime, "slice.indices() receiver does not have slice layout");
            auto *length = dynamic_cast<IntObject *>(args[0]);
            if (!length)
                raise_type_error(&runtime, "slice.indices() argument must be int");

            SliceRange range = normalize_slice(runtime, *slice, length->value);
            return runtime.new_tuple({
                runtime.new_int(range.start),
                runtime.new_int(range.stop),
                runtime.new_int(range.step),
            });
        }

        std::int64_t slice_component(Runtime &runtime,
                                     Object *value,
                                     std::int64_t default_value,
                                     const char *name)
        {
            if (!value || value == runtime.none())
                return default_value;
            auto *integer = dynamic_cast<IntObject *>(value);
            if (!integer)
            {
                std::ostringstream message;
                message << "slice " << name << " must be int or None";
                raise_type_error(&runtime, message.str());
            }
            return integer->value;
        }

        std::int64_t clamp_positive(std::int64_t value, std::int64_t length)
        {
            if (value < 0)
                value += length;
            if (value < 0)
                return 0;
            if (value > length)
                return length;
            return value;
        }

        std::int64_t clamp_negative(std::int64_t value, std::int64_t length)
        {
            if (value < 0)
                value += length;
            if (value < 0)
                return -1;
            if (value >= length)
                return length - 1;
            return value;
        }
    }

    SliceObject::SliceObject(TypeObject *type, Object *start, Object *stop, Object *step)
        : Object(type), start_(start), stop_(stop), step_(step)
    {
        if (start_)
            start_->incref();
        if (stop_)
            stop_->incref();
        if (step_)
            step_->incref();
    }

    SliceObject::~SliceObject()
    {
        clear_references();
    }

    void SliceObject::clear_references()
    {
        CXXPYTHON_CLEAR_REF(step_);
        CXXPYTHON_CLEAR_REF(stop_);
        CXXPYTHON_CLEAR_REF(start_);
        Object::clear_references();
    }

    Object *SliceObject::start() const noexcept
    {
        return start_;
    }

    Object *SliceObject::stop() const noexcept
    {
        return stop_;
    }

    Object *SliceObject::step() const noexcept
    {
        return step_;
    }

    SliceRange normalize_slice(Runtime &runtime, const SliceObject &slice, std::int64_t sequence_length)
    {
        if (sequence_length < 0)
            raise_value_error(&runtime, "length should not be negative");

        const std::int64_t step = slice_component(runtime, slice.step(), 1, "step");
        if (step == 0)
            raise_value_error(&runtime, "slice step cannot be zero");

        std::int64_t start = 0;
        std::int64_t stop = 0;
        std::size_t length = 0;
        if (step > 0)
        {
            start = slice_component(runtime, slice.start(), 0, "start");
            stop = slice_component(runtime, slice.stop(), sequence_length, "stop");
            start = clamp_positive(start, sequence_length);
            stop = clamp_positive(stop, sequence_length);
            if (stop > start)
                length = static_cast<std::size_t>(1 + (stop - 1 - start) / step);
        }
        else
        {
            if (!slice.start() || slice.start() == runtime.none())
                start = sequence_length - 1;
            else
                start = clamp_negative(slice_component(runtime, slice.start(), 0, "start"), sequence_length);

            if (!slice.stop() || slice.stop() == runtime.none())
                stop = -1;
            else
                stop = clamp_negative(slice_component(runtime, slice.stop(), 0, "stop"), sequence_length);
            if (stop < start)
                length = static_cast<std::size_t>(1 + (start - 1 - stop) / (-step));
        }

        return SliceRange{
            .start = start,
            .stop = stop,
            .step = step,
            .length = length,
        };
    }

    TypeSlots make_slice_slots()
    {
        return TypeSlots{
            .repr = slice_repr,
            .hash = default_hash,
            .equal = default_equal,
            .truth = default_truth,
            .traverse = slice_traverse,
            .new_object = slice_new,
            .init = slice_init,
        };
    }

    void initialize_slice_type_methods(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("indices"),
                        runtime.new_method_descriptor("indices", type, slice_indices_method));
    }

    void initialize_slice_slot_wrappers(Runtime &, TypeObject *type)
    {
        add_common_object_slot_wrappers(type, true);
    }
}
