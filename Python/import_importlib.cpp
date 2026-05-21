#include "cxxpython/runtime.hpp"

#include <string_view>

namespace cxxpython
{
    TypeObject *Runtime::importlib_type_for_loader(std::string_view loader_name)
    {
        TypeObject **slot = nullptr;
        if (loader_name == "BuiltinImporter")
            slot = &builtin_importer_type_;
        else if (loader_name == "SourceFileLoader")
            slot = &source_file_loader_type_;
        else if (loader_name == "ExtensionFileLoader")
            slot = &extension_file_loader_type_;
        else if (loader_name == "ModuleSpec")
            slot = &module_spec_type_;
        else
            raise_import_error(this, "unknown importlib object type '" + std::string(loader_name) + "'");

        if (*slot)
            return *slot;

        DictObject *namespace_dict = new_dict();
        auto namespace_root = root(namespace_dict);
        namespace_root->set(new_str("__module__"), new_str("_frozen_importlib"));
        namespace_root->set(new_str("__qualname__"), new_str(std::string(loader_name)));
        *slot = new_class(std::string(loader_name), object_type(), namespace_root.get(), "_frozen_importlib");
        add_root(*slot);
        return *slot;
    }

    Object *Runtime::new_importlib_object(
        std::string name,
        std::string module,
        std::initializer_list<std::pair<std::string_view, Object *>> attributes)
    {
        (void)module;
        TypeObject *type = importlib_type_for_loader(name);
        auto type_root = root(type);
        Object *instance = new_plain_instance(type_root.get());
        auto instance_root = root(instance);
        DictObject *dict = new_dict();
        auto dict_root = root(dict);
        for (const auto &[key, value] : attributes)
            dict_root->set(new_str(std::string(key)), value ? value : none());
        instance_root->set_instance_dict(dict_root.get());
        return instance_root.get();
    }

    Object *Runtime::new_module_loader_object(const ModuleLoadSpec &spec)
    {
        return new_importlib_object(
            spec.loader_name,
            "_frozen_importlib",
            {
                {"name", new_str(spec.loader_name)},
                {"module_name", new_str(spec.name)},
                {"path", new_str(spec.filename)},
            });
    }

    Object *Runtime::new_module_spec_object(const ModuleLoadSpec &spec, Object *loader)
    {
        return new_importlib_object(
            "ModuleSpec",
            "_frozen_importlib",
            {
                {"name", new_str(spec.name)},
                {"loader", loader},
                {"origin", new_str(spec.origin)},
                {"has_location", spec.kind == ModuleLoadKind::Source || spec.kind == ModuleLoadKind::Extension
                                     ? static_cast<Object *>(true_value())
                                     : static_cast<Object *>(false_value())},
            });
    }

    void Runtime::initialize_module_import_metadata(ModuleObject *module,
                                                    const ModuleLoadSpec &spec,
                                                    Object *loader,
                                                    Object *module_spec)
    {
        if (!module)
            raise_import_error(this, "loader returned null for module '" + spec.name + "'");
        module->dict()->set(new_str("__loader__"), loader);
        module->dict()->set(new_str("__spec__"), module_spec);
        module->dict()->set(new_str("__package__"), new_str(spec.package));
        module->dict()->set(new_str("__cached__"), none());
    }
}
