#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cxxpython/runtime.hpp"

namespace
{
    int failures = 0;

    void check(bool condition, const char *expr, const char *file, int line)
    {
        if (condition)
            return;
        ++failures;
        std::cerr << file << ":" << line << ": check failed: " << expr << "\n";
    }

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)

    std::string custom_repr(const cxxpython::Object &object)
    {
        (void)object;
        return "<custom>";
    }

    std::int64_t custom_hash(const cxxpython::Object &object)
    {
        (void)object;
        return 42;
    }

    bool custom_equal(const cxxpython::Object &left, const cxxpython::Object &right)
    {
        return left.type() == right.type();
    }

    bool custom_false(const cxxpython::Object &object)
    {
        (void)object;
        return false;
    }
}

int main()
{
    using namespace cxxpython;

    Runtime runtime;

    CHECK(runtime.object_type()->type() == runtime.type_type());
    CHECK(runtime.type_type()->type() == runtime.type_type());
    CHECK(runtime.none()->type() == runtime.none_type());
    CHECK(runtime.true_value()->type() == runtime.bool_type());
    CHECK(runtime.false_value()->type() == runtime.bool_type());
    CHECK(runtime.list_type()->base == runtime.object_type());

    CHECK(is_subclass(runtime.bool_type(), runtime.int_type()));
    CHECK(is_subclass(runtime.bool_type(), runtime.object_type()));
    CHECK(is_instance(runtime.true_value(), runtime.int_type()));
    CHECK(runtime.bool_type()->mro.size() == 3);
    CHECK(runtime.bool_type()->mro[0] == runtime.bool_type());
    CHECK(runtime.bool_type()->mro[1] == runtime.int_type());
    CHECK(runtime.bool_type()->mro[2] == runtime.object_type());
    CHECK(runtime.bool_type()->base == runtime.int_type());
    CHECK(runtime.bool_type()->bases.size() == 1);
    CHECK(runtime.bool_type()->bases[0] == runtime.int_type());
    CHECK(runtime.base_exception_type()->base == runtime.object_type());
    CHECK(runtime.exception_type()->base == runtime.base_exception_type());
    CHECK(runtime.type_error_type()->base == runtime.exception_type());
    CHECK(runtime.index_error_type()->base == runtime.exception_type());
    CHECK(is_subclass(runtime.type_error_type(), runtime.base_exception_type()));
    CHECK(is_subclass(runtime.index_error_type(), runtime.base_exception_type()));

    IntObject *one = runtime.new_int(1);
    IntObject *zero = runtime.new_int(0);
    CHECK(object_equal(runtime.true_value(), one));
    CHECK(object_equal(runtime.false_value(), zero));
    CHECK(!object_equal(runtime.true_value(), runtime.new_int(2)));
    CHECK(object_hash(runtime.true_value()) == object_hash(one));
    CHECK(object_hash(runtime.false_value()) == object_hash(zero));
    CHECK(is_true(runtime.true_value()));
    CHECK(!is_true(runtime.false_value()));
    CHECK(!is_true(runtime.none()));

    ListObject *empty_list = runtime.new_list({});
    CHECK(object_repr(empty_list) == "[]");
    CHECK(!is_true(empty_list));
    ListObject *list = runtime.new_list({one, runtime.new_str("two")});
    CHECK(list->size() == 2);
    CHECK(list->get(0) == one);
    CHECK(object_repr(list) == "[1, 'two']");
    CHECK(is_true(list));
    list->append(runtime.new_int(3));
    CHECK(list->size() == 3);
    CHECK(static_cast<IntObject *>(list->get(-1))->value == 3);
    CHECK(object_equal(list, runtime.new_list({one, runtime.new_str("two"), runtime.new_int(3)})));

    bool saw_index_error = false;
    try
    {
        (void)list->get(99);
    }
    catch (const PythonError &error)
    {
        saw_index_error = error.type() == runtime.index_error_type() &&
                          error.value()->message() == "list index out of range";
    }
    CHECK(saw_index_error);

    bool saw_unhashable_list = false;
    try
    {
        (void)object_hash(list);
    }
    catch (const PythonError &error)
    {
        saw_unhashable_list = error.type() == runtime.type_error_type() &&
                              error.value()->message() == "unhashable type: 'list'";
    }
    CHECK(saw_unhashable_list);

    DictObject *dict = runtime.new_dict();
    StrObject *bool_value = runtime.new_str("bool");
    StrObject *int_value = runtime.new_str("int");
    dict->set(runtime.true_value(), bool_value);
    dict->set(runtime.new_int(1), int_value);
    CHECK(dict->size() == 1);
    CHECK(dict->get(runtime.true_value()) == int_value);
    CHECK(dict->get(runtime.new_int(1)) == int_value);

    TupleObject *tuple_key = runtime.new_tuple({runtime.new_int(4), runtime.new_str("x")});
    StrObject *tuple_value = runtime.new_str("tuple");
    dict->set(tuple_key, tuple_value);
    CHECK(dict->size() == 2);
    CHECK(dict->get(runtime.new_tuple({runtime.new_int(4), runtime.new_str("x")})) == tuple_value);
    CHECK(dict->entries()[0].key == runtime.true_value());
    CHECK(dict->entries()[0].value == int_value);
    CHECK(dict->entries()[1].key == tuple_key);
    CHECK(dict->entries()[1].value == tuple_value);

    DictObject *large_dict = runtime.new_dict();
    std::vector<StrObject *> large_keys;
    for (int i = 0; i < 96; ++i)
    {
        StrObject *key = runtime.new_str("key-" + std::to_string(i));
        large_keys.push_back(key);
        large_dict->set(key, runtime.new_int(i));
    }
    CHECK(large_dict->size() == large_keys.size());
    for (std::size_t i = 0; i < large_keys.size(); ++i)
    {
        auto *value = dynamic_cast<IntObject *>(large_dict->get(large_keys[i]));
        CHECK(value && value->value == static_cast<std::int64_t>(i));
        CHECK(large_dict->entries()[i].key == large_keys[i]);
    }
    large_dict->set(large_keys[42], runtime.new_int(4200));
    CHECK(large_dict->size() == large_keys.size());
    CHECK(static_cast<IntObject *>(large_dict->get(large_keys[42]))->value == 4200);

    TypeSlots collision_slots;
    collision_slots.hash = custom_hash;
    TypeObject *collision_type = runtime.new_heap_type("CollisionKey", runtime.object_type(), collision_slots);
    Object *collision_a = runtime.allocate<Object>(collision_type);
    Object *collision_b = runtime.allocate<Object>(collision_type);
    large_dict->set(collision_a, runtime.new_str("a"));
    large_dict->set(collision_b, runtime.new_str("b"));
    CHECK(large_dict->size() == large_keys.size() + 2);
    CHECK(static_cast<StrObject *>(large_dict->get(collision_a))->value == "a");
    CHECK(static_cast<StrObject *>(large_dict->get(collision_b))->value == "b");

    CHECK(object_repr(runtime.none()) == "None");
    CHECK(object_repr(runtime.true_value()) == "True");
    CHECK(object_repr(one) == "1");
    CHECK(object_repr(runtime.new_str("hello")) == "'hello'");
    CHECK(object_repr(runtime.type_type()) == "<class 'type'>");
    ExceptionObject *manual_error = runtime.new_exception(runtime.type_error_type(), "bad operand");
    CHECK(manual_error->type() == runtime.type_error_type());
    CHECK(manual_error->message() == "bad operand");
    CHECK(is_instance(manual_error, runtime.base_exception_type()));
    CHECK(object_repr(manual_error) == "TypeError('bad operand')");

    TypeSlots custom_slots;
    custom_slots.repr = custom_repr;
    custom_slots.hash = custom_hash;
    custom_slots.equal = custom_equal;
    custom_slots.truth = custom_false;
    TypeObject *custom_type = runtime.new_heap_type("Custom", runtime.object_type(), custom_slots);

    Object *custom_a = runtime.allocate<Object>(custom_type);
    Object *custom_b = runtime.allocate<Object>(custom_type);
    CHECK(object_repr(custom_a) == "<custom>");
    CHECK(object_hash(custom_a) == 42);
    CHECK(object_equal(custom_a, custom_b));
    CHECK(!is_true(custom_a));
    CHECK(is_instance(custom_a, custom_type));
    CHECK(is_instance(custom_a, runtime.object_type()));

    TypeObject *repr_only_type = runtime.new_heap_type("ReprOnly", custom_type, TypeSlots{.repr = custom_repr});
    Object *repr_only = runtime.allocate<Object>(repr_only_type);
    CHECK(object_repr(repr_only) == "<custom>");
    CHECK(object_hash(repr_only) == 42);
    CHECK(!is_true(repr_only));

    bool saw_python_error = false;
    try
    {
        dict->set(nullptr, one);
    }
    catch (const PythonError &error)
    {
        saw_python_error = error.type() == runtime.type_error_type() &&
                           error.value()->type() == runtime.type_error_type() &&
                           error.value()->message() == "dict key cannot be null" &&
                           std::string(error.what()) == "dict key cannot be null";
    }
    CHECK(saw_python_error);

    saw_python_error = false;
    try
    {
        (void)object_hash(dict);
    }
    catch (const PythonError &error)
    {
        saw_python_error = error.type() == runtime.type_error_type() &&
                           error.value()->type() == runtime.type_error_type() &&
                           error.value()->message() == "unhashable type: 'dict'" &&
                           std::string(error.what()) == "unhashable type: 'dict'";
    }
    CHECK(saw_python_error);

    CHECK(runtime.heap_size() >= 11);
    CHECK(runtime.root_count() == 16);

    std::size_t visited_roots = 0;
    runtime.visit_roots([&](Object *object)
                        {
                            if (object)
                                ++visited_roots;
                        });
    CHECK(visited_roots == runtime.root_count());

    std::size_t tuple_children = 0;
    tuple_key->visit_children([&](Object *object)
                              {
                                  if (object)
                                      ++tuple_children;
                              });
    CHECK(tuple_children == 2);

    std::size_t list_children = 0;
    list->visit_children([&](Object *object)
                         {
                             if (object)
                                 ++list_children;
                         });
    CHECK(list_children == list->size());

    std::size_t dict_children = 0;
    dict->visit_children([&](Object *object)
                         {
                             if (object)
                                 ++dict_children;
                         });
    CHECK(dict_children == dict->size() * 2);

    if (failures != 0)
    {
        std::cerr << failures << " checks failed\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
