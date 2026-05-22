#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"
#include "type_initialization.hpp"

#include <sstream>

namespace cxxpython
{
    namespace
    {
        std::string slot_wrapper_repr(const Object &object)
        {
            auto *wrapper = static_cast<const SlotWrapperObject *>(&object);
            return "<slot wrapper '" + wrapper->name() + "' of '" +
                   type_qualified_name(wrapper->owner()) + "' objects>";
        }

        std::string bound_slot_wrapper_repr(const Object &object)
        {
            auto *bound = static_cast<const BoundSlotWrapperObject *>(&object);
            return "<method-wrapper '" + bound->wrapper()->name() +
                   "' of " + type_qualified_name(bound->self()->type()) +
                   " object at " + pointer_hex(bound->self()) + ">";
        }

        std::int64_t identity_hash(const Object &object)
        {
            return reinterpret_cast<std::intptr_t>(&object);
        }

        bool identity_equal(const Object &left, const Object &right)
        {
            return &left == &right;
        }

        bool always_true(const Object &object)
        {
            (void)object;
            return true;
        }

        void slot_wrapper_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *wrapper = static_cast<const SlotWrapperObject *>(&object);
            if (wrapper->owner())
                visitor(wrapper->owner());
        }

        void bound_slot_wrapper_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *bound = static_cast<const BoundSlotWrapperObject *>(&object);
            if (bound->wrapper())
                visitor(bound->wrapper());
            if (bound->self())
                visitor(bound->self());
        }

        void require_slot_argument_count(Runtime *runtime,
                                         const SlotWrapperObject &wrapper,
                                         ObjectSpan args,
                                         std::size_t expected)
        {
            if (args.size() == expected)
                return;
            std::ostringstream message;
            message << type_qualified_name(wrapper.owner()) << "." << wrapper.name()
                    << "() takes exactly " << expected << " argument"
                    << (expected == 1 ? "" : "s")
                    << " (" << args.size() << " given)";
            raise_type_error(runtime, message.str());
        }

        Object *call_length_slot(Runtime *runtime, const SlotWrapperObject &wrapper, Object *self, ObjectSpan args)
        {
            require_slot_argument_count(runtime, wrapper, args, 0);
            if (!wrapper.owner()->slots.length)
                raise_type_error(runtime, wrapper.name() + " wrapper has no length slot");
            return runtime->new_int(static_cast<std::int64_t>(wrapper.owner()->slots.length(*self)));
        }

        Object *call_getitem_slot(Runtime *runtime, const SlotWrapperObject &wrapper, Object *self, ObjectSpan args)
        {
            require_slot_argument_count(runtime, wrapper, args, 1);
            if (!wrapper.owner()->slots.getitem)
                raise_type_error(runtime, wrapper.name() + " wrapper has no getitem slot");
            Object *result = wrapper.owner()->slots.getitem(*self, args[0]);
            if (!result)
                raise_type_error(runtime, wrapper.name() + " wrapper returned null");
            return result;
        }

        Object *call_setitem_slot(Runtime *runtime, const SlotWrapperObject &wrapper, Object *self, ObjectSpan args)
        {
            require_slot_argument_count(runtime, wrapper, args, 2);
            if (!wrapper.owner()->slots.setitem)
                raise_type_error(runtime, wrapper.name() + " wrapper has no setitem slot");
            wrapper.owner()->slots.setitem(*self, args[0], args[1]);
            return runtime->none();
        }

        Object *call_delitem_slot(Runtime *runtime, const SlotWrapperObject &wrapper, Object *self, ObjectSpan args)
        {
            require_slot_argument_count(runtime, wrapper, args, 1);
            if (!wrapper.owner()->slots.delitem)
                raise_type_error(runtime, wrapper.name() + " wrapper has no delitem slot");
            wrapper.owner()->slots.delitem(*self, args[0]);
            return runtime->none();
        }

        Object *call_contains_slot(Runtime *runtime, const SlotWrapperObject &wrapper, Object *self, ObjectSpan args)
        {
            require_slot_argument_count(runtime, wrapper, args, 1);
            if (!wrapper.owner()->slots.contains)
                raise_type_error(runtime, wrapper.name() + " wrapper has no contains slot");
            return wrapper.owner()->slots.contains(*self, args[0])
                       ? runtime->true_value()
                       : runtime->false_value();
        }

        Object *call_iter_slot(Runtime *runtime, const SlotWrapperObject &wrapper, Object *self, ObjectSpan args)
        {
            require_slot_argument_count(runtime, wrapper, args, 0);
            if (!wrapper.owner()->slots.iter)
                raise_type_error(runtime, wrapper.name() + " wrapper has no iter slot");
            Object *result = wrapper.owner()->slots.iter(*self);
            if (!result)
                raise_type_error(runtime, wrapper.name() + " wrapper returned null");
            return result;
        }

        Object *call_next_slot(Runtime *runtime, const SlotWrapperObject &wrapper, Object *self, ObjectSpan args)
        {
            require_slot_argument_count(runtime, wrapper, args, 0);
            if (!wrapper.owner()->slots.iternext)
                raise_type_error(runtime, wrapper.name() + " wrapper has no iternext slot");
            Object *result = wrapper.owner()->slots.iternext(*self);
            if (!result)
                raise_type_error(runtime, wrapper.name() + " wrapper returned null");
            return result;
        }

        Object *call_binary_slot(Runtime *runtime, const SlotWrapperObject &wrapper, Object *self, ObjectSpan args)
        {
            require_slot_argument_count(runtime, wrapper, args, 1);
            if (!wrapper.owner()->slots.binary_op)
                raise_type_error(runtime, wrapper.name() + " wrapper has no binary slot");
            Object *result = wrapper.owner()->slots.binary_op(*self, *args[0], wrapper.binary_op());
            if (!result)
                raise_type_error(runtime, wrapper.name() + " wrapper returned null");
            return result;
        }

        Object *call_unary_slot(Runtime *runtime, const SlotWrapperObject &wrapper, Object *self, ObjectSpan args)
        {
            require_slot_argument_count(runtime, wrapper, args, 0);
            if (!wrapper.owner()->slots.unary_op)
                raise_type_error(runtime, wrapper.name() + " wrapper has no unary slot");
            Object *result = wrapper.owner()->slots.unary_op(*self, wrapper.unary_op());
            if (!result)
                raise_type_error(runtime, wrapper.name() + " wrapper returned null");
            return result;
        }

        Object *call_rich_compare_slot(Runtime *runtime, const SlotWrapperObject &wrapper, Object *self, ObjectSpan args)
        {
            require_slot_argument_count(runtime, wrapper, args, 1);
            if (wrapper.owner()->slots.rich_compare)
            {
                Object *result = wrapper.owner()->slots.rich_compare(*self, *args[0], wrapper.rich_compare_op());
                if (!result)
                    raise_type_error(runtime, wrapper.name() + " wrapper returned null");
                return result;
            }
            if (wrapper.rich_compare_op() == RichCompareOp::Equal ||
                wrapper.rich_compare_op() == RichCompareOp::NotEqual)
            {
                const bool equal = object_equal(self, args[0]);
                return equal == (wrapper.rich_compare_op() == RichCompareOp::Equal)
                           ? runtime->true_value()
                           : runtime->false_value();
            }
            return runtime->not_implemented();
        }

        Object *slot_wrapper_call(Object &object, const CallArguments &args)
        {
            auto *wrapper = static_cast<SlotWrapperObject *>(&object);
            return wrapper->call_unbound(args);
        }

        Object *bound_slot_wrapper_call(Object &object, const CallArguments &args)
        {
            auto *bound = static_cast<BoundSlotWrapperObject *>(&object);
            return bound->wrapper()->call_bound(bound->self(), args);
        }

        Object *slot_wrapper_get(Object &object, Object *instance, TypeObject *owner)
        {
            (void)owner;
            auto *wrapper = static_cast<SlotWrapperObject *>(&object);
            if (!instance)
                return wrapper;
            return wrapper->runtime()->new_bound_slot_wrapper(wrapper, instance);
        }

    }

    SlotWrapperObject::SlotWrapperObject(TypeObject *type,
                                         std::string name,
                                         TypeObject *owner,
                                         SlotWrapperKind kind,
                                         BinaryOp binary_op,
                                         UnaryOp unary_op,
                                         RichCompareOp rich_compare_op)
        : Object(type),
          name_(std::move(name)),
          owner_(owner),
          kind_(kind),
          binary_op_(binary_op),
          unary_op_(unary_op),
          rich_compare_op_(rich_compare_op)
    {
    }

    const std::string &SlotWrapperObject::name() const noexcept
    {
        return name_;
    }

    TypeObject *SlotWrapperObject::owner() const noexcept
    {
        return owner_;
    }

    SlotWrapperKind SlotWrapperObject::kind() const noexcept
    {
        return kind_;
    }

    BinaryOp SlotWrapperObject::binary_op() const noexcept
    {
        return binary_op_;
    }

    UnaryOp SlotWrapperObject::unary_op() const noexcept
    {
        return unary_op_;
    }

    RichCompareOp SlotWrapperObject::rich_compare_op() const noexcept
    {
        return rich_compare_op_;
    }

    Object *SlotWrapperObject::call_unbound(const CallArguments &args)
    {
        require_no_keywords(runtime(), name_, args);
        ObjectSpan all_args(args.positional.data(), args.positional.size());
        if (all_args.empty())
        {
            raise_type_error(runtime(),
                             "descriptor '" + name_ + "' of '" +
                                 type_qualified_name(owner_) +
                                 "' object needs an argument");
        }
        return call_bound(all_args.front(), all_args.subspan(1));
    }

    Object *SlotWrapperObject::call_bound(Object *self, const CallArguments &args)
    {
        require_no_keywords(runtime(), name_, args);
        return call_bound(self, ObjectSpan(args.positional.data(), args.positional.size()));
    }

    Object *SlotWrapperObject::call_bound(Object *self, ObjectSpan args)
    {
        if (!is_instance(self, owner_))
        {
            std::ostringstream message;
            message << "descriptor '" << name_ << "' for '"
                    << type_qualified_name(owner_)
                    << "' objects doesn't apply to a '"
                    << type_qualified_name(self ? self->type() : nullptr)
                    << "' object";
            raise_type_error(runtime(), message.str());
        }

        switch (kind_)
        {
        case SlotWrapperKind::Repr:
            require_slot_argument_count(runtime(), *this, args, 0);
            if (!owner_->slots.repr)
                raise_type_error(runtime(), name_ + " wrapper has no repr slot");
            return runtime()->new_str(owner_->slots.repr(*self));
        case SlotWrapperKind::Hash:
            require_slot_argument_count(runtime(), *this, args, 0);
            if (!owner_->slots.hash)
                raise_type_error(runtime(), name_ + " wrapper has no hash slot");
            return runtime()->new_int(owner_->slots.hash(*self));
        case SlotWrapperKind::Length:
            return call_length_slot(runtime(), *this, self, args);
        case SlotWrapperKind::GetItem:
            return call_getitem_slot(runtime(), *this, self, args);
        case SlotWrapperKind::SetItem:
            return call_setitem_slot(runtime(), *this, self, args);
        case SlotWrapperKind::DeleteItem:
            return call_delitem_slot(runtime(), *this, self, args);
        case SlotWrapperKind::Contains:
            return call_contains_slot(runtime(), *this, self, args);
        case SlotWrapperKind::Iter:
            return call_iter_slot(runtime(), *this, self, args);
        case SlotWrapperKind::Next:
            return call_next_slot(runtime(), *this, self, args);
        case SlotWrapperKind::BinaryOp:
            return call_binary_slot(runtime(), *this, self, args);
        case SlotWrapperKind::UnaryOp:
            return call_unary_slot(runtime(), *this, self, args);
        case SlotWrapperKind::RichCompare:
            return call_rich_compare_slot(runtime(), *this, self, args);
        }
        raise_type_error(runtime(), "unknown slot wrapper kind");
    }

    BoundSlotWrapperObject::BoundSlotWrapperObject(TypeObject *type, SlotWrapperObject *wrapper, Object *self)
        : Object(type), wrapper_(wrapper), self_(self)
    {
    }

    SlotWrapperObject *BoundSlotWrapperObject::wrapper() const noexcept
    {
        return wrapper_;
    }

    Object *BoundSlotWrapperObject::self() const noexcept
    {
        return self_;
    }

    TypeSlots make_slot_wrapper_slots()
    {
        return TypeSlots{
            .repr = slot_wrapper_repr,
            .hash = identity_hash,
            .equal = identity_equal,
            .truth = always_true,
            .traverse = slot_wrapper_traverse,
            .call = slot_wrapper_call,
            .descr_get = slot_wrapper_get,
        };
    }

    TypeSlots make_bound_slot_wrapper_slots()
    {
        return TypeSlots{
            .repr = bound_slot_wrapper_repr,
            .hash = identity_hash,
            .equal = identity_equal,
            .truth = always_true,
            .traverse = bound_slot_wrapper_traverse,
            .call = bound_slot_wrapper_call,
        };
    }

    void add_slot_wrapper(TypeObject *type,
                          const char *name,
                          SlotWrapperKind kind,
                          BinaryOp binary_op,
                          UnaryOp unary_op,
                          RichCompareOp rich_compare_op)
    {
        type->dict->set(type->runtime()->new_str(name),
                        type->runtime()->new_slot_wrapper(name,
                                                          type,
                                                          kind,
                                                          binary_op,
                                                          unary_op,
                                                          rich_compare_op));
    }

    void add_common_object_slot_wrappers(TypeObject *type, bool hashable)
    {
        add_slot_wrapper(type, "__repr__", SlotWrapperKind::Repr);
        add_slot_wrapper(type, "__eq__", SlotWrapperKind::RichCompare, BinaryOp::Add, UnaryOp::Positive, RichCompareOp::Equal);
        add_slot_wrapper(type, "__ne__", SlotWrapperKind::RichCompare, BinaryOp::Add, UnaryOp::Positive, RichCompareOp::NotEqual);
        if (hashable)
            add_slot_wrapper(type, "__hash__", SlotWrapperKind::Hash);
    }

    void add_order_slot_wrappers(TypeObject *type)
    {
        add_slot_wrapper(type, "__lt__", SlotWrapperKind::RichCompare, BinaryOp::Add, UnaryOp::Positive, RichCompareOp::Less);
        add_slot_wrapper(type, "__le__", SlotWrapperKind::RichCompare, BinaryOp::Add, UnaryOp::Positive, RichCompareOp::LessEqual);
        add_slot_wrapper(type, "__gt__", SlotWrapperKind::RichCompare, BinaryOp::Add, UnaryOp::Positive, RichCompareOp::Greater);
        add_slot_wrapper(type, "__ge__", SlotWrapperKind::RichCompare, BinaryOp::Add, UnaryOp::Positive, RichCompareOp::GreaterEqual);
    }

    void add_numeric_slot_wrappers(TypeObject *type)
    {
        add_slot_wrapper(type, "__add__", SlotWrapperKind::BinaryOp, BinaryOp::Add);
        add_slot_wrapper(type, "__sub__", SlotWrapperKind::BinaryOp, BinaryOp::Subtract);
        add_slot_wrapper(type, "__mul__", SlotWrapperKind::BinaryOp, BinaryOp::Multiply);
        add_slot_wrapper(type, "__pos__", SlotWrapperKind::UnaryOp, BinaryOp::Add, UnaryOp::Positive);
        add_slot_wrapper(type, "__neg__", SlotWrapperKind::UnaryOp, BinaryOp::Add, UnaryOp::Negative);
        add_slot_wrapper(type, "__invert__", SlotWrapperKind::UnaryOp, BinaryOp::Add, UnaryOp::Invert);
    }

    void add_sequence_slot_wrappers(TypeObject *type, bool is_mutable)
    {
        add_slot_wrapper(type, "__len__", SlotWrapperKind::Length);
        add_slot_wrapper(type, "__getitem__", SlotWrapperKind::GetItem);
        if (is_mutable)
        {
            add_slot_wrapper(type, "__setitem__", SlotWrapperKind::SetItem);
            add_slot_wrapper(type, "__delitem__", SlotWrapperKind::DeleteItem);
        }
        add_slot_wrapper(type, "__contains__", SlotWrapperKind::Contains);
        add_slot_wrapper(type, "__iter__", SlotWrapperKind::Iter);
    }

    void Runtime::initialize_slot_wrappers()
    {
        initialize_object_slot_wrappers(*this, object_type_);
        initialize_none_slot_wrappers(*this, none_type_);
        initialize_not_implemented_slot_wrappers(*this, not_implemented_type_);
        initialize_int_slot_wrappers(*this, int_type_);
        initialize_bool_slot_wrappers(*this, bool_type_);
        initialize_str_slot_wrappers(*this, str_type_);
        initialize_list_slot_wrappers(*this, list_type_);
        initialize_tuple_slot_wrappers(*this, tuple_type_);
        initialize_slice_slot_wrappers(*this, slice_type_);
        initialize_dict_slot_wrappers(*this, dict_type_);
        initialize_mappingproxy_slot_wrappers(*this, mappingproxy_type_);
        initialize_iterator_slot_wrappers(*this, sequence_iterator_type_);
        initialize_iterator_slot_wrappers(*this, dict_key_iterator_type_);
        initialize_range_slot_wrappers(*this, range_type_);
        initialize_range_iterator_slot_wrappers(*this, range_iterator_type_);
        initialize_enumerate_slot_wrappers(*this, enumerate_type_);
        initialize_zip_slot_wrappers(*this, zip_type_);
        initialize_map_slot_wrappers(*this, map_type_);
        initialize_filter_slot_wrappers(*this, filter_type_);
    }
}
