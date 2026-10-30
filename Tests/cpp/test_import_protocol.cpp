#include "cxxpython/compiler.hpp"
#include "cxxpython/import.hpp"
#include "internal/compiler.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace {
using namespace cxxpython::core;

class ImportProtocol : public testing::Test {
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

    void check(std::string text)
    {
        ReturnSlot out(context);
        import_module(context, U"__main__", out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
        const auto globals = roots.root(module_dict(context, out.value()));
        SourceUnit unit{SourceBuffer(std::move(text))};
        run(context, {&unit, globals}, out);
        ASSERT_EQ(out.tag(), ResultTag::Ok);
    }
};

TEST_F(ImportProtocol, CustomFinderLoaderReloadAndPackagePath)
{
    check(R"(import sys
import importlib
from importlib.machinery import ModuleSpec

calls = []
class CustomModule:
    pass
class Loader:
    def create_module(self, spec):
        calls.append(('create', spec.name))
        return CustomModule()
    def exec_module(self, module):
        calls.append(('exec', module.__spec__.name))
        assert sys.modules[module.__spec__.name] is module
        if not hasattr(module, 'executions'):
            assert module.__spec__._initializing is True
        module.executions = getattr(module, 'executions', 0) + 1
loader = Loader()
class Finder:
    def find_spec(self, name, path, target=None):
        if name == 'protocol_pkg':
            calls.append(('find', name, path, target))
            return ModuleSpec(name, loader, is_package=True)
        if name == 'protocol_pkg.child':
            calls.append(('find', name, path, target))
            return ModuleSpec(name, loader)
        return None
finder = Finder()
sys.meta_path.insert(0, finder)
import protocol_pkg
assert type(protocol_pkg) is CustomModule
assert calls[0] == ('find', 'protocol_pkg', None, None)
assert calls[1] == ('create', 'protocol_pkg')
assert protocol_pkg.__spec__._initializing is False
import protocol_pkg.child
child = protocol_pkg.child
assert calls[3][0] == 'find'
assert calls[3][1] == 'protocol_pkg.child'
assert calls[3][2] is protocol_pkg.__path__
assert calls[3][3] is None
assert sys.modules['protocol_pkg.child'] is child
before = len(calls)
assert importlib.reload(child) is child
assert calls[before][0] == 'find'
assert calls[before][2] is protocol_pkg.__path__
assert calls[before][3] is child
assert calls[before + 1] == ('exec', 'protocol_pkg.child')
assert len(calls) == before + 2
assert child.executions == 2
sys.meta_path.remove(finder)
)");
}

TEST_F(ImportProtocol, CopiedErrorPathsUseNativeImportErrorFields)
{
    check(R"(from importlib import util
e = ModuleNotFoundError('missing', name='child', path='source', name_from='parent')
assert e.args == ('missing',)
assert e.msg == 'missing'
assert e.name == 'child'
assert e.path == 'source'
assert e.name_from == 'parent'
e.args = ('changed',)
assert str(e) == 'missing'
e.msg = 7
assert str(e) == 'changed'
ImportError.__init__(e, 'reset')
assert e.name is None and e.path is None and e.name_from is None
assert e.msg == 'reset'
assert isinstance(FileNotFoundError('file'), OSError)
import sys
sys.modules['nonpackage'] = type(sys)('nonpackage')
try:
    util.find_spec('nonpackage.child')
except ModuleNotFoundError as error:
    assert error.name == 'nonpackage.child'
    assert isinstance(error.__cause__, AttributeError)
else:
    raise AssertionError('nonpackage accepted')
try:
    ImportError('message', unknown=1)
except TypeError:
    pass
else:
    raise AssertionError('unknown keyword accepted')
)");
}

TEST_F(ImportProtocol, FailuresRollbackWithoutDiscardingReplacement)
{
    check(R"(import sys
import importlib
from importlib.machinery import ModuleSpec
class Replacement:
    pass
replacement = Replacement()
class Loader:
    def create_module(self, spec):
        return None
    def exec_module(self, module):
        assert sys.modules[module.__name__] is module
        assert module.__spec__._initializing is True
        if module.__name__ == 'protocol_replaced':
            sys.modules[module.__name__] = replacement
        raise ValueError('loader failure')
class Finder:
    def find_spec(self, name, path, target=None):
        if name == 'protocol_broken' or name == 'protocol_replaced':
            return ModuleSpec(name, Loader())
        if name == 'protocol_finder_error':
            raise ValueError('finder failure')
        return None
finder = Finder()
sys.meta_path.insert(0, finder)
for name in ('protocol_broken', 'protocol_replaced', 'protocol_finder_error'):
    try:
        importlib.import_module(name)
    except ValueError:
        pass
    else:
        raise AssertionError('import should fail')
assert 'protocol_broken' not in sys.modules
assert 'protocol_finder_error' not in sys.modules
assert sys.modules['protocol_replaced'] is replacement
sys.meta_path = [finder]
try:
    importlib.import_module('protocol_missing')
except ModuleNotFoundError:
    pass
else:
    raise AssertionError('unexpected fallback')
)");
}

TEST_F(ImportProtocol, PathHooksCacheAndImportlibUtilities)
{
    check(R"(import sys
import importlib
from importlib import util
from importlib.machinery import ModuleSpec
events = []
class Loader:
    def create_module(self, spec):
        return None
    def exec_module(self, module):
        module.value = 42
loader = Loader()
class PathFinder:
    def find_spec(self, name, target=None):
        events.append(('find', name, target))
        if name == 'hook_one' or name == 'hook_two':
            return ModuleSpec(name, loader)
        return None
    def invalidate_caches(self):
        events.append(('invalidate',))
path_finder = PathFinder()
def hook(path):
    events.append(('hook', path))
    if path == '/protocol-path':
        return path_finder
    raise ImportError('not handled')
sys.path_hooks.insert(0, hook)
sys.path.insert(0, '/protocol-path')
sys.path_importer_cache.pop('/protocol-path', None)
import hook_one
import hook_two
assert hook_one.value == 42
assert hook_two.value == 42
assert sys.path_importer_cache['/protocol-path'] is path_finder
assert events.count(('hook', '/protocol-path')) == 1
sys.path_importer_cache['missing-protocol-path'] = None
importlib.invalidate_caches()
assert 'missing-protocol-path' not in sys.path_importer_cache
assert ('invalidate',) in events
spec = util.find_spec('hook_one')
assert spec is hook_one.__spec__
spec = util.spec_from_loader('utility_module', loader)
assert spec.name == 'utility_module'
assert spec.loader is loader
file_spec = util.spec_from_file_location('utility_file', '/tmp/utility_file.py')
assert file_spec.name == 'utility_file'
assert file_spec.origin == '/tmp/utility_file.py'
assert file_spec.loader is not None
)");
}

TEST_F(ImportProtocol, EmptyMetaPathDoesNotImportRegisteredSource)
{
    register_source_module(context, U"protocol_registered_unloaded",
                           std::make_shared<const SourceUnit>(SourceBuffer("value = 42\n")));
    register_source_module(context, U"protocol_registry_control",
                           std::make_shared<const SourceUnit>(SourceBuffer("ready = True\n")));
    ReturnSlot out(context);
    import_module(context, U"protocol_registry_control", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    check(R"(import sys
import importlib
assert sys.modules['protocol_registry_control'].ready is True
assert 'protocol_registered_unloaded' not in sys.modules
sys.meta_path = []
try:
    importlib.import_module('protocol_registered_unloaded')
except ModuleNotFoundError:
    pass
else:
    raise AssertionError('registered source bypassed empty meta_path')
assert 'protocol_registered_unloaded' not in sys.modules
)");
}

TEST_F(ImportProtocol, FileFinderUsesCustomLoaderDetailsAndPathHook)
{
    auto pattern = (std::filesystem::temp_directory_path() / "cxxpython-finder-XXXXXX").string();
    ASSERT_NE(mkdtemp(pattern.data()), nullptr);
    const std::filesystem::path directory = pattern;
    {
        std::ofstream file(directory / "custom_file.special");
        file << "fixture\n";
    }
    ReturnSlot out(context);
    import_module(context, U"__main__", out);
    ASSERT_EQ(out.tag(), ResultTag::Ok);
    const auto globals = roots.root(module_dict(context, out.value()));
    ReturnSlot key(context), value(context);
    make_str(context, U"finder_directory", key);
    make_str(context, detail::decode_filename(SourceBuffer("", directory.string())), value);
    detail::name_store(context, globals.get(context), key.value(), value.value());
    SourceUnit unit{SourceBuffer(R"(import sys
from importlib.machinery import FileFinder
events = []
class CustomFileLoader:
    def __init__(self, name, path):
        events.append(('init', name, path))
        self.name = name
        self.path = path
    def create_module(self, spec):
        events.append(('create', spec.name))
        return None
    def exec_module(self, module):
        events.append(('exec', module.__name__))
        module.value = 42
details = (CustomFileLoader, ['.special'])
finder = FileFinder(finder_directory, details)
spec = finder.find_spec('custom_file')
assert spec.name == 'custom_file'
assert type(spec.loader) is CustomFileLoader
assert spec.loader.name == 'custom_file'
assert spec.loader.path == finder_directory + '/custom_file.special'
assert spec.origin == spec.loader.path
hook = FileFinder.path_hook(details)
sys.path_hooks.insert(0, hook)
sys.path.insert(0, finder_directory)
sys.path_importer_cache.pop(finder_directory, None)
import custom_file
assert custom_file.value == 42
assert type(custom_file.__loader__) is CustomFileLoader
assert custom_file.__loader__.path == finder_directory + '/custom_file.special'
assert ('create', 'custom_file') in events
assert ('exec', 'custom_file') in events
assert type(sys.path_importer_cache[finder_directory]) is FileFinder
)")};
    run(context, {&unit, globals}, out);
    EXPECT_EQ(out.tag(), ResultTag::Ok);
    std::filesystem::remove_all(directory);
}
} // namespace
