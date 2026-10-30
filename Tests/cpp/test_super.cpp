#include "cxxpython/compiler.hpp"
#include "cxxpython/superobject.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;
class CoreSuper : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};
    void SetUp() override
    {
        context.bootstrap_core();
    }
    Handle dictionary()
    {
        ReturnSlot out(context);
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              out);
        return roots.root(out.value());
    }
    void exec(const char *source, Handle globals, ReturnSlot &out)
    {
        SourceUnit unit(SourceBuffer(source, "super_test.py"));
        run(context, {&unit, globals}, out);
    }
    void eval(const char *source, Handle globals, ReturnSlot &out)
    {
        SourceUnit unit(SourceBuffer(source, "super_test.py"), InputMode::Eval);
        run(context, {&unit, globals}, out);
    }
    std::string result(ReturnSlot &out)
    {
        ReturnSlot text(context);
        repr(context, roots.root(out.value()), text);
        return source_utf8(context.payload<StrObject>(text.value()).value);
    }
};

TEST_F(CoreSuper, ExplicitBindingsAndDescriptorLookupSurviveCollection)
{
    auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(class A:
    def method(self): return self
    @classmethod
    def cls(cls): return cls
    @staticmethod
    def static(): return 42
    @property
    def prop(self): return 17
class B(A): pass
class C(B): pass
obj = C()
s = super(B, obj)
obj.cycle = s
unbound = super(B)
bound = unbound.__get__(obj, C)
classbound = super(B, C)
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    for (int i = 0; i != 3; ++i) {
        context.collect();
        eval("s.method() is obj, s.cls() is C, s.static(), s.prop, "
             "bound.method() is obj, classbound.cls() is C, "
             "s.__thisclass__ is B, s.__self__ is obj, s.__self_class__ is C, "
             "unbound.__self__ is None",
             globals, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(result(out), "(True, True, 42, 17, True, True, True, True, True, True)");
    }
}

TEST_F(CoreSuper, CustomDescriptorAndSubclassBinding)
{
    auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(class Descriptor:
    def __get__(self, obj, owner): return (obj, owner)
class A:
    item = Descriptor()
class B(A): pass
class Extended(super): pass
obj = B()
unbound = Extended(B)
bound = unbound.__get__(obj, B)
classbound = super(B, B)
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    context.collect();
    eval("bound.item == (obj, B), classbound.item == (None, B), "
         "type(bound) is Extended, bound.__self__ is obj, "
         "unbound.__get__(None, B) is unbound",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(result(out), "(True, True, True, True, True)");
}

TEST_F(CoreSuper, ZeroArgumentUsesCallingFrameAndRejectsMisuse)
{
    auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(class A:
    def f(self): return 13
    @classmethod
    def c(cls): return cls
class B(A):
    def f(self): return super().f()
    @classmethod
    def c(cls): return super().c()
    def captured(self):
        def inner(): return self
        return super().f()
def missing(x): return super()
def noargs(): return super()
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("B().f(), B.c() is B, B().captured()", globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(result(out), "(13, True, 13)");
    for (auto source : {"missing(1)", "noargs()"}) {
        eval(source, globals, out);
        ASSERT_EQ(out.tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::RuntimeErrorType));
        context.collect();
        EXPECT_NO_THROW(validate_exception(context, out.exception()));
    }
}

TEST_F(CoreSuper, FailedReinitializationPreservesPreviousBinding)
{
    auto globals = dictionary();
    ReturnSlot out(context);
    exec(R"(class A: pass
class B(A): pass
obj = B()
s = super(B, obj)
)",
         globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    eval("s.__init__(B, 1)", globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Raised);
    EXPECT_EQ(context.type_of(out.exception()), context.builtin(CoreObject::TypeErrorType));
    context.collect();
    eval("s.__thisclass__ is B, s.__self__ is obj, s.__self_class__ is B", globals, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    EXPECT_EQ(result(out), "(True, True, True)");
}
} // namespace
