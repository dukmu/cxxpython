#include "cxxpython/compiler.hpp"
#include "cxxpython/import.hpp"

#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;

class Runpy : public testing::Test {
  protected:
    Interpreter interpreter{{.collect_every_allocation = true}};
    Attachment context{interpreter};
    RootScope roots{context};

    void SetUp() override
    {
        context.bootstrap_core();
        ReturnSlot out(context);
        initialize_imports(context, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
    }

    void source(std::u32string name, std::string body, bool package = false)
    {
        register_source_module(context, std::move(name),
                               std::make_shared<SourceUnit>(SourceBuffer(std::move(body))),
                               package);
    }

    void check(std::string body)
    {
        ReturnSlot out(context);
        import_module(context, U"__main__", out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        const auto globals = roots.root(module_dict(context, out.value()));
        SourceUnit unit{SourceBuffer(std::move(body))};
        run(context, {&unit, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
    }
};

TEST_F(Runpy, ModuleExecutesInExistingMainNamespace)
{
    source(U"runpy_target", R"(import sys
observed = (__name__, __spec__.name, __package__, __cached__, __file__,
            __loader__, sys.argv[0], sentinel)
)");
    check(R"(import sys
import runpy
main = sys.modules['__main__']
namespace = main.__dict__
sentinel = 42
sys.argv = ['original', 'argument']
result = runpy._run_module_as_main('runpy_target')
assert result is namespace
assert sys.modules['__main__'] is main
assert observed[0] == '__main__'
assert observed[1] == 'runpy_target'
assert observed[2] == ''
assert observed[3] is __spec__.cached
assert observed[4] == __spec__.origin
assert observed[5] is __spec__.loader
assert observed[6] == __spec__.origin
assert observed[7] == 42
assert sys.argv[1] == 'argument'
assert 'runpy_target' not in sys.modules
)");
}

TEST_F(Runpy, PackageExecutesItsMain)
{
    source(U"runpy_package", "initialized = 42", true);
    source(U"runpy_package.__main__", R"(import sys
package_observed = (__name__, __spec__.name, __package__, __cached__,
                    __file__, sys.argv[0])
)");
    check(R"(import sys
import runpy
sys.argv = ['original']
main = sys.modules['__main__']
result = runpy._run_module_as_main('runpy_package')
assert result is main.__dict__
assert package_observed[0] == '__main__'
assert package_observed[1] == 'runpy_package.__main__'
assert package_observed[2] == 'runpy_package'
assert package_observed[3] is __spec__.cached
assert package_observed[4] == __spec__.origin
assert package_observed[5] == __spec__.origin
assert sys.modules['runpy_package'].initialized == 42
assert 'runpy_package.__main__' not in sys.modules
)");
}

TEST_F(Runpy, MissingAndUnexecutableModulesPreserveErrors)
{
    source(U"runpy_empty_package", "pass", true);
    check(R"(import sys
import runpy
from importlib.machinery import ModuleSpec
try:
    runpy._get_module_details('runpy_missing')
except ImportError as exc:
    assert str(exc) == 'No module named runpy_missing'
else:
    raise AssertionError('missing module accepted')
try:
    runpy._get_module_details('runpy_empty_package')
except ImportError as exc:
    assert 'is a package and cannot be directly executed' in str(exc)
else:
    raise AssertionError('package without __main__ accepted')
class Loader:
    def get_code(self, name):
        if name == 'runpy_broken':
            raise ImportError('loader failure')
        return None
class Finder:
    def find_spec(self, name, path, target=None):
        if name == 'runpy_broken' or name == 'runpy_no_code':
            return ModuleSpec(name, Loader())
        return None
finder = Finder()
sys.meta_path.insert(0, finder)
try:
    runpy._get_module_details('runpy_broken')
except ImportError as exc:
    assert str(exc) == 'loader failure'
    assert isinstance(exc.__cause__, ImportError)
else:
    raise AssertionError('loader error swallowed')
try:
    runpy._get_module_details('runpy_no_code')
except ImportError as exc:
    assert str(exc) == 'No code object available for runpy_no_code'
else:
    raise AssertionError('missing code accepted')
sys.meta_path.remove(finder)
try:
    runpy._run_module_as_main('runpy_missing')
except SystemExit as exc:
    assert 'No module named runpy_missing' in str(exc.code)
else:
    raise AssertionError('missing main did not exit')
)");
}
} // namespace
