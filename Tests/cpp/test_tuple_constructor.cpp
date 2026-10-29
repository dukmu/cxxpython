#include "cxxpython/compiler.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;
class TupleConstructors : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};
    void SetUp() override
    {
        context.bootstrap_core();
    }
    Handle globals()
    {
        ReturnSlot out(context);
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              out);
        return roots.root(out.value());
    }
    void exec(std::string source, Handle globals, ReturnSlot &out)
    {
        SourceUnit unit(SourceBuffer(std::move(source), "tuple.py"));
        run(context, {&unit, globals}, out);
    }
    std::string eval(std::string source, Handle globals)
    {
        SourceUnit unit(SourceBuffer(std::move(source), "tuple.py"), InputMode::Eval);
        ReturnSlot out(context), text(context);
        run(context, {&unit, globals}, out);
        if (out.tag() != ResultTag::Ok)
            return "<raised>";
        repr(context, roots.root(out.value()), text);
        return source_utf8(context.payload<StrObject>(text.value()).value);
    }
};

TEST_F(TupleConstructors, IterableIdentityAndSubclassConstructionUseOrdinaryObjects)
{
    const auto mapping = globals();
    ReturnSlot out(context);
    exec(R"(original = (1, 2)
class Child(tuple):
    pass
class Changed(tuple):
    def __iter__(self):
        return iter((9, 8))
child = Child(original)
changed = Changed(original)
)",
         mapping, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(eval("tuple(), tuple(original) is original, tuple(child), type(child) is Child, "
                   "tuple(changed), tuple(range(3)), tuple('a\\U0001f600')",
                   mapping),
              "((), True, (1, 2), True, (9, 8), (0, 1, 2), ('a', '\xf0\x9f\x98\x80'))");
}

TEST_F(TupleConstructors, LengthAndIndexCallbacksRunAfterIterAndBeforeConsumption)
{
    const auto mapping = globals();
    ReturnSlot out(context);
    exec(R"(events = 0
class Count:
    def __index__(self):
        global events
        events = events * 10 + 3
        return 2
class Source:
    def __iter__(self):
        global events
        events = events * 10 + 1
        return iter((4, 5))
    def __len__(self):
        global events
        events = events * 10 + 2
        return Count()
value = tuple(Source())
)",
         mapping, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(eval("value, events", mapping), "((4, 5), 123)");
}

TEST_F(TupleConstructors, HintFallbackAndIteratorExceptionsRemainDistinct)
{
    const auto mapping = globals();
    ReturnSlot out(context);
    exec(R"(class Source:
    def __iter__(self):
        return iter((1, 2))
    def __len__(self):
        raise TypeError('no length')
    def __length_hint__(self):
        return NotImplemented
value = tuple(Source())
)",
         mapping, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(eval("value", mapping), "(1, 2)");
    exec("def declined(self): raise TypeError('no hint')\n"
         "Source.__length_hint__ = declined\nvalue = tuple(Source())",
         mapping, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(eval("value", mapping), "(1, 2)");
    exec("Source.__length_hint__ = None\nvalue = tuple(Source())", mapping, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(eval("value", mapping), "(1, 2)");
    for (const auto &[expression, kind] :
         {std::pair{"-1", CoreObject::ValueErrorType},
          {"None", CoreObject::TypeErrorType},
          {"18446744073709551616", CoreObject::OverflowErrorType}}) {
        exec("def hint(self): return " + std::string(expression) +
                 "\nSource.__length_hint__ = hint\nvalue = tuple(Source())",
             mapping, out);
        ASSERT_EQ(out.tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.exception()), context.builtin(kind));
    }
    exec(R"(class Descriptor:
    def __get__(self, obj, owner):
        raise TypeError('descriptor failed')
Source.__length_hint__ = Descriptor()
value = tuple(Source())
)",
         mapping, out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::TypeErrorType));
    exec(R"(class After:
    def __length_hint__(self): raise ValueError('new type')
class Before:
    def __iter__(self): return iter((1, 2))
    def __len__(self):
        self.__class__ = After
        raise TypeError('no length')
value = tuple(Before())
)",
         mapping, out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::ValueErrorType));
    exec(R"(class Broken:
    def __iter__(self):
        return self
    def __next__(self):
        raise ValueError('not exhaustion')
value = tuple(Broken())
)",
         mapping, out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::ValueErrorType));
    for (auto source : {"tuple(None)", "tuple(1, 2)", "tuple(iterable=())"}) {
        exec(source, mapping, out);
        ASSERT_EQ(out.tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::TypeErrorType));
    }
}
} // namespace
