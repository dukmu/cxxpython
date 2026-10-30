#include "cxxpython/compiler.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;

TEST(BuiltinFunctions, IterationAbsoluteAndCharactersSurviveCollection)
{
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    context.bootstrap_core();
    RootScope roots(context);
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)), out);
    const auto globals = roots.root(out.value());
    SourceUnit source(SourceBuffer(R"(class Absolute:
    def __abs__(self):
        return 42
class Broken:
    def __bool__(self):
        raise ValueError('truth')
class Indexed:
    def __index__(self):
        return 55296
class DivBase:
    def __divmod__(self, other):
        return 'normal'
class DivChild(DivBase):
    def __rdivmod__(self, other):
        return 'reflected'
class Callable:
    def __call__(self):
        return 1
class InstanceCall:
    pass
class BadKey:
    def __call__(self, item):
        raise ValueError('key')
)"));
    run(context, {&source, globals}, out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    for (const auto *expression : {"abs(-123) == 123",
                                   "abs(-1.5) == 1.5",
                                   "abs(Absolute()) == 42",
                                   "all([])",
                                   "not any([])",
                                   "all([1, 2, 3])",
                                   "any([0, 1, Broken()])",
                                   "not all([0, Broken()])",
                                   "ord(chr(Indexed())) == 55296",
                                   "ord(chr(1114111)) == 1114111",
                                   "ord(b'\\xff') == 255",
                                   "callable(Callable())",
                                   "not callable(InstanceCall())",
                                   "callable(abs)",
                                   "divmod(DivBase(), DivChild()) == 'reflected'",
                                   "divmod(-7, 3) == (-3, 2)",
                                   "divmod(-7.0, 3.0) == (-3.0, 2.0)",
                                   "min([3, 1, 2]) == 1",
                                   "max(3, 1, 2) == 3",
                                   "min([], default=7) == 7",
                                   "max([1, 2], key=lambda x: -x) == 1",
                                   "sum([1, 2, 3]) == 6",
                                   "sum([], start=4) == 4",
                                   "sum([1e100, 1.0, -1e100]) == 1.0"}) {
        SCOPED_TRACE(expression);
        SourceUnit unit(SourceBuffer(expression), InputMode::Eval);
        run(context, {&unit, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        EXPECT_EQ(out.value(), context.builtin(CoreObject::True));
    }
    for (const auto &[expression, error] : {
             std::pair{"all([Broken()])", CoreObject::ValueErrorType},
             {"any([Broken()])", CoreObject::ValueErrorType},
             {"abs('x')", CoreObject::TypeErrorType},
             {"chr(1114112)", CoreObject::ValueErrorType},
             {"chr(999999999999999999999999)", CoreObject::OverflowErrorType},
             {"ord('ab')", CoreObject::TypeErrorType},
             {"min([])", CoreObject::ValueErrorType},
             {"max(1, 2, default=3)", CoreObject::TypeErrorType},
             {"min([1], key=BadKey())", CoreObject::ValueErrorType},
             {"sum([], 'x')", CoreObject::TypeErrorType},
             {"sum([], b'x')", CoreObject::TypeErrorType},
         }) {
        SCOPED_TRACE(expression);
        SourceUnit unit(SourceBuffer(expression), InputMode::Eval);
        run(context, {&unit, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Raised);
        EXPECT_EQ(context.type_of(out.exception()), context.builtin(error));
    }
}
} // namespace
