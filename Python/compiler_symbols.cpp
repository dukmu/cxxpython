#include "compiler_symbols.hpp"

#include <algorithm>

namespace cxxpython
{
    namespace
    {
        class SymbolCollector final : public compiler_internal::ExprVisitor,
                                      public compiler_internal::TargetVisitor,
                                      public compiler_internal::StmtVisitor
        {
        public:
            SymbolCollector(SymbolTable &symbols)
                : symbols_(symbols)
            {
            }

            void visit(const compiler_internal::IntegerExpr &expression) override
            {
                (void)expression;
            }

            void visit(const compiler_internal::StringExpr &expression) override
            {
                (void)expression;
            }

            void visit(const compiler_internal::FStringExpr &expression) override
            {
                for (const compiler_internal::FStringPart &part : expression.parts)
                {
                    if (part.is_expression())
                        part.expression->accept(*this);
                }
            }

            void visit(const compiler_internal::UnaryExpr &expression) override
            {
                expression.operand->accept(*this);
            }

            void visit(const compiler_internal::BinaryExpr &expression) override
            {
                expression.left->accept(*this);
                expression.right->accept(*this);
            }

            void visit(const compiler_internal::BoolExpr &expression) override
            {
                for (const auto &value : expression.values)
                    value->accept(*this);
            }

            void visit(const compiler_internal::CompareExpr &expression) override
            {
                expression.left->accept(*this);
                for (const compiler_internal::Comparison &comparison : expression.comparisons)
                    comparison.right->accept(*this);
            }

            void visit(const compiler_internal::ConditionalExpr &expression) override
            {
                expression.body->accept(*this);
                expression.condition->accept(*this);
                expression.else_body->accept(*this);
            }

            void visit(const compiler_internal::SingletonExpr &expression) override
            {
                (void)expression;
            }

            void visit(const compiler_internal::NameExpr &expression) override
            {
                (void)symbols_.add_name(expression.value);
            }

            void visit(const compiler_internal::AttributeExpr &expression) override
            {
                expression.object->accept(*this);
                (void)symbols_.add_name(expression.name);
            }

            void visit(const compiler_internal::SubscriptExpr &expression) override
            {
                expression.object->accept(*this);
                expression.key->accept(*this);
            }

            void visit(const compiler_internal::SliceExpr &expression) override
            {
                if (expression.start)
                    expression.start->accept(*this);
                if (expression.stop)
                    expression.stop->accept(*this);
                if (expression.step)
                    expression.step->accept(*this);
            }

            void visit(const compiler_internal::ListExpr &expression) override
            {
                for (const auto &item : expression.items)
                    item->accept(*this);
            }

            void visit(const compiler_internal::TupleExpr &expression) override
            {
                for (const auto &item : expression.items)
                    item->accept(*this);
            }

            void visit(const compiler_internal::DictExpr &expression) override
            {
                for (const compiler_internal::DictItem &item : expression.items)
                {
                    item.key->accept(*this);
                    item.value->accept(*this);
                }
            }

            void visit(const compiler_internal::CallExpr &expression) override
            {
                expression.callee->accept(*this);
                for (const auto &arg : expression.args)
                    arg.value->accept(*this);
            }

            void visit(const compiler_internal::NameTarget &target) override
            {
                (void)symbols_.add_name(target.name);
            }

            void visit(const compiler_internal::AttributeTarget &target) override
            {
                target.object->accept(*this);
                (void)symbols_.add_name(target.name);
            }

            void visit(const compiler_internal::SubscriptTarget &target) override
            {
                target.object->accept(*this);
                target.key->accept(*this);
            }

            void visit(const compiler_internal::ExprStmt &statement) override
            {
                statement.expression->accept(*this);
            }

            void visit(const compiler_internal::AssignStmt &statement) override
            {
                statement.target->accept(*this);
                statement.value->accept(*this);
            }

            void visit(const compiler_internal::AugAssignStmt &statement) override
            {
                statement.target->accept(*this);
                statement.value->accept(*this);
            }

            void visit(const compiler_internal::DeleteStmt &statement) override
            {
                statement.target->accept(*this);
            }

            void visit(const compiler_internal::IfStmt &statement) override
            {
                statement.condition->accept(*this);
                for (const auto &item : statement.then_body)
                    item->accept(*this);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::ForStmt &statement) override
            {
                (void)symbols_.add_name(statement.target);
                statement.iterable->accept(*this);
                for (const auto &item : statement.body)
                    item->accept(*this);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::WhileStmt &statement) override
            {
                statement.condition->accept(*this);
                for (const auto &item : statement.body)
                    item->accept(*this);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::PassStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::BreakStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::ContinueStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::ReturnStmt &statement) override
            {
                if (statement.value)
                    statement.value->accept(*this);
            }

            void visit(const compiler_internal::RaiseStmt &statement) override
            {
                statement.value->accept(*this);
            }

            void visit(const compiler_internal::GlobalStmt &statement) override
            {
                for (const std::string &name : statement.names)
                    (void)symbols_.add_name(name);
            }

            void visit(const compiler_internal::NonlocalStmt &statement) override
            {
                for (const std::string &name : statement.names)
                    (void)symbols_.add_name(name);
            }

            void visit(const compiler_internal::ImportStmt &statement) override
            {
                (void)symbols_.add_name("__import__");
                for (const compiler_internal::ImportAlias &alias : statement.aliases)
                    (void)symbols_.add_name(alias.binding);
            }

            void visit(const compiler_internal::FromImportStmt &statement) override
            {
                (void)symbols_.add_name("__import__");
                if (statement.import_star)
                    return;
                for (const compiler_internal::ImportAlias &alias : statement.aliases)
                {
                    (void)symbols_.add_name(alias.name);
                    (void)symbols_.add_name(alias.binding);
                }
            }

            void visit(const compiler_internal::TryStmt &statement) override
            {
                for (const auto &item : statement.body)
                    item->accept(*this);
                for (const compiler_internal::ExceptHandler &handler : statement.handlers)
                {
                    if (handler.type)
                        handler.type->accept(*this);
                    if (handler.name)
                        (void)symbols_.add_name(*handler.name);
                    for (const auto &item : handler.body)
                        item->accept(*this);
                }
                for (const auto &item : statement.finally_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::FunctionDefStmt &statement) override
            {
                for (const auto &decorator : statement.decorators)
                    decorator->accept(*this);
                for (const auto &default_value : statement.params.defaults)
                    default_value->accept(*this);
                (void)symbols_.add_name(statement.name);
            }

            void visit(const compiler_internal::ClassDefStmt &statement) override
            {
                for (const auto &decorator : statement.decorators)
                    decorator->accept(*this);
                (void)symbols_.add_name("__build_class__");
                for (const auto &base : statement.bases)
                    base->accept(*this);
                for (const auto &keyword : statement.keywords)
                    keyword.value->accept(*this);
                (void)symbols_.add_name(statement.name);
            }

        private:
            SymbolTable &symbols_;
        };

        class FunctionLocalCollector final : public compiler_internal::TargetVisitor,
                                             public compiler_internal::StmtVisitor
        {
        public:
            explicit FunctionLocalCollector(SymbolTable &symbols)
                : symbols_(symbols)
            {
            }

            void visit(const compiler_internal::NameTarget &target) override
            {
                if (symbols_.is_global(target.name) || symbols_.is_nonlocal(target.name))
                    return;
                (void)symbols_.add_local(target.name);
            }

            void visit(const compiler_internal::AttributeTarget &target) override
            {
                (void)target;
            }

            void visit(const compiler_internal::SubscriptTarget &target) override
            {
                (void)target;
            }

            void visit(const compiler_internal::ExprStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::AssignStmt &statement) override
            {
                statement.target->accept(*this);
            }

            void visit(const compiler_internal::AugAssignStmt &statement) override
            {
                statement.target->accept(*this);
            }

            void visit(const compiler_internal::DeleteStmt &statement) override
            {
                statement.target->accept(*this);
            }

            void visit(const compiler_internal::IfStmt &statement) override
            {
                for (const auto &item : statement.then_body)
                    item->accept(*this);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::ForStmt &statement) override
            {
                if (!symbols_.is_global(statement.target) && !symbols_.is_nonlocal(statement.target))
                    (void)symbols_.add_local(statement.target);
                for (const auto &item : statement.body)
                    item->accept(*this);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::WhileStmt &statement) override
            {
                for (const auto &item : statement.body)
                    item->accept(*this);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::PassStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::BreakStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::ContinueStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::ReturnStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::RaiseStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::GlobalStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::NonlocalStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::ImportStmt &statement) override
            {
                for (const compiler_internal::ImportAlias &alias : statement.aliases)
                {
                    if (symbols_.is_global(alias.binding) || symbols_.is_nonlocal(alias.binding))
                        continue;
                    (void)symbols_.add_local(alias.binding);
                }
            }

            void visit(const compiler_internal::FromImportStmt &statement) override
            {
                if (statement.import_star)
                    return;
                for (const compiler_internal::ImportAlias &alias : statement.aliases)
                {
                    if (symbols_.is_global(alias.binding) || symbols_.is_nonlocal(alias.binding))
                        continue;
                    (void)symbols_.add_local(alias.binding);
                }
            }

            void visit(const compiler_internal::TryStmt &statement) override
            {
                for (const auto &item : statement.body)
                    item->accept(*this);
                for (const compiler_internal::ExceptHandler &handler : statement.handlers)
                {
                    if (handler.name)
                        (void)symbols_.add_local(*handler.name);
                    for (const auto &item : handler.body)
                        item->accept(*this);
                }
                for (const auto &item : statement.finally_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::FunctionDefStmt &statement) override
            {
                if (symbols_.is_global(statement.name) || symbols_.is_nonlocal(statement.name))
                    return;
                (void)symbols_.add_local(statement.name);
            }

            void visit(const compiler_internal::ClassDefStmt &statement) override
            {
                if (symbols_.is_global(statement.name) || symbols_.is_nonlocal(statement.name))
                    return;
                (void)symbols_.add_local(statement.name);
            }

        private:
            SymbolTable &symbols_;
        };

        class DirectiveCollector final : public compiler_internal::StmtVisitor
        {
        public:
            DirectiveCollector(Runtime &runtime, SymbolTable &symbols)
                : runtime_(runtime), symbols_(symbols)
            {
            }

            void visit(const compiler_internal::ExprStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::AssignStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::AugAssignStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::DeleteStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::IfStmt &statement) override
            {
                for (const auto &item : statement.then_body)
                    item->accept(*this);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::ForStmt &statement) override
            {
                for (const auto &item : statement.body)
                    item->accept(*this);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::WhileStmt &statement) override
            {
                for (const auto &item : statement.body)
                    item->accept(*this);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::PassStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::BreakStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::ContinueStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::ReturnStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::RaiseStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::GlobalStmt &statement) override
            {
                for (const std::string &name : statement.names)
                {
                    if (symbols_.is_nonlocal(name))
                        raise_syntax_error(&runtime_, "name '" + name + "' is nonlocal and global");
                    symbols_.add_global(name);
                }
            }

            void visit(const compiler_internal::NonlocalStmt &statement) override
            {
                for (const std::string &name : statement.names)
                {
                    if (symbols_.is_global(name))
                        raise_syntax_error(&runtime_, "name '" + name + "' is nonlocal and global");
                    symbols_.add_nonlocal(name);
                }
            }

            void visit(const compiler_internal::ImportStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::FromImportStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::TryStmt &statement) override
            {
                for (const auto &item : statement.body)
                    item->accept(*this);
                for (const compiler_internal::ExceptHandler &handler : statement.handlers)
                {
                    for (const auto &item : handler.body)
                        item->accept(*this);
                }
                for (const auto &item : statement.finally_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::FunctionDefStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::ClassDefStmt &statement) override
            {
                (void)statement;
            }

        private:
            Runtime &runtime_;
            SymbolTable &symbols_;
        };

        class LexicalReferenceCollector final : public compiler_internal::ExprVisitor,
                                                public compiler_internal::TargetVisitor,
                                                public compiler_internal::StmtVisitor
        {
        public:
            const std::vector<std::string> &references() const noexcept
            {
                return references_;
            }

            void visit(const compiler_internal::IntegerExpr &expression) override
            {
                (void)expression;
            }

            void visit(const compiler_internal::StringExpr &expression) override
            {
                (void)expression;
            }

            void visit(const compiler_internal::FStringExpr &expression) override
            {
                for (const compiler_internal::FStringPart &part : expression.parts)
                {
                    if (part.is_expression())
                        part.expression->accept(*this);
                }
            }

            void visit(const compiler_internal::UnaryExpr &expression) override
            {
                expression.operand->accept(*this);
            }

            void visit(const compiler_internal::BinaryExpr &expression) override
            {
                expression.left->accept(*this);
                expression.right->accept(*this);
            }

            void visit(const compiler_internal::BoolExpr &expression) override
            {
                for (const auto &value : expression.values)
                    value->accept(*this);
            }

            void visit(const compiler_internal::CompareExpr &expression) override
            {
                expression.left->accept(*this);
                for (const compiler_internal::Comparison &comparison : expression.comparisons)
                    comparison.right->accept(*this);
            }

            void visit(const compiler_internal::ConditionalExpr &expression) override
            {
                expression.body->accept(*this);
                expression.condition->accept(*this);
                expression.else_body->accept(*this);
            }

            void visit(const compiler_internal::SingletonExpr &expression) override
            {
                (void)expression;
            }

            void visit(const compiler_internal::NameExpr &expression) override
            {
                add_reference(expression.value);
            }

            void visit(const compiler_internal::AttributeExpr &expression) override
            {
                expression.object->accept(*this);
            }

            void visit(const compiler_internal::SubscriptExpr &expression) override
            {
                expression.object->accept(*this);
                expression.key->accept(*this);
            }

            void visit(const compiler_internal::SliceExpr &expression) override
            {
                if (expression.start)
                    expression.start->accept(*this);
                if (expression.stop)
                    expression.stop->accept(*this);
                if (expression.step)
                    expression.step->accept(*this);
            }

            void visit(const compiler_internal::ListExpr &expression) override
            {
                for (const auto &item : expression.items)
                    item->accept(*this);
            }

            void visit(const compiler_internal::TupleExpr &expression) override
            {
                for (const auto &item : expression.items)
                    item->accept(*this);
            }

            void visit(const compiler_internal::DictExpr &expression) override
            {
                for (const compiler_internal::DictItem &item : expression.items)
                {
                    item.key->accept(*this);
                    item.value->accept(*this);
                }
            }

            void visit(const compiler_internal::CallExpr &expression) override
            {
                if (auto *name = dynamic_cast<compiler_internal::NameExpr *>(expression.callee.get()))
                {
                    if (name->value == "super" && expression.args.empty())
                        add_reference("__class__");
                }
                expression.callee->accept(*this);
                for (const auto &arg : expression.args)
                    arg.value->accept(*this);
            }

            void visit(const compiler_internal::NameTarget &target) override
            {
                if (read_target_)
                    add_reference(target.name);
            }

            void visit(const compiler_internal::AttributeTarget &target) override
            {
                target.object->accept(*this);
            }

            void visit(const compiler_internal::SubscriptTarget &target) override
            {
                target.object->accept(*this);
                target.key->accept(*this);
            }

            void visit(const compiler_internal::ExprStmt &statement) override
            {
                statement.expression->accept(*this);
            }

            void visit(const compiler_internal::AssignStmt &statement) override
            {
                statement.value->accept(*this);
                statement.target->accept(*this);
            }

            void visit(const compiler_internal::AugAssignStmt &statement) override
            {
                const ScopedReadTarget read_target(*this);
                statement.target->accept(*this);
                statement.value->accept(*this);
            }

            void visit(const compiler_internal::DeleteStmt &statement) override
            {
                statement.target->accept(*this);
            }

            void visit(const compiler_internal::IfStmt &statement) override
            {
                statement.condition->accept(*this);
                for (const auto &item : statement.then_body)
                    item->accept(*this);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::ForStmt &statement) override
            {
                statement.iterable->accept(*this);
                for (const auto &item : statement.body)
                    item->accept(*this);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::WhileStmt &statement) override
            {
                statement.condition->accept(*this);
                for (const auto &item : statement.body)
                    item->accept(*this);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::PassStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::BreakStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::ContinueStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::ReturnStmt &statement) override
            {
                if (statement.value)
                    statement.value->accept(*this);
            }

            void visit(const compiler_internal::RaiseStmt &statement) override
            {
                statement.value->accept(*this);
            }

            void visit(const compiler_internal::GlobalStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::NonlocalStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::ImportStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::FromImportStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::TryStmt &statement) override
            {
                for (const auto &item : statement.body)
                    item->accept(*this);
                for (const compiler_internal::ExceptHandler &handler : statement.handlers)
                {
                    if (handler.type)
                        handler.type->accept(*this);
                    for (const auto &item : handler.body)
                        item->accept(*this);
                }
                for (const auto &item : statement.finally_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::FunctionDefStmt &statement) override
            {
                for (const auto &decorator : statement.decorators)
                    decorator->accept(*this);
                for (const auto &default_value : statement.params.defaults)
                    default_value->accept(*this);
            }

            void visit(const compiler_internal::ClassDefStmt &statement) override
            {
                for (const auto &decorator : statement.decorators)
                    decorator->accept(*this);
                for (const auto &base : statement.bases)
                    base->accept(*this);
                for (const auto &keyword : statement.keywords)
                    keyword.value->accept(*this);
            }

        private:
            class ScopedReadTarget final
            {
            public:
                explicit ScopedReadTarget(LexicalReferenceCollector &collector)
                    : collector_(collector), previous_(collector.read_target_)
                {
                    collector_.read_target_ = true;
                }

                ~ScopedReadTarget()
                {
                    collector_.read_target_ = previous_;
                }

                ScopedReadTarget(const ScopedReadTarget &) = delete;
                ScopedReadTarget &operator=(const ScopedReadTarget &) = delete;

            private:
                LexicalReferenceCollector &collector_;
                bool previous_;
            };

            void add_reference(const std::string &name)
            {
                if (std::find(references_.begin(), references_.end(), name) == references_.end())
                    references_.push_back(name);
            }

            std::vector<std::string> references_;
            bool read_target_ = false;
        };

        class NestedFunctionCollector final : public compiler_internal::StmtVisitor
        {
        public:
            void visit(const compiler_internal::ExprStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::AssignStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::AugAssignStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::DeleteStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::IfStmt &statement) override
            {
                for (const auto &item : statement.then_body)
                    item->accept(*this);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::ForStmt &statement) override
            {
                for (const auto &item : statement.body)
                    item->accept(*this);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::WhileStmt &statement) override
            {
                for (const auto &item : statement.body)
                    item->accept(*this);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::PassStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::BreakStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::ContinueStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::ReturnStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::RaiseStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::GlobalStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::NonlocalStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::ImportStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::FromImportStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::TryStmt &statement) override
            {
                for (const auto &item : statement.body)
                    item->accept(*this);
                for (const compiler_internal::ExceptHandler &handler : statement.handlers)
                {
                    for (const auto &item : handler.body)
                        item->accept(*this);
                }
                for (const auto &item : statement.finally_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::FunctionDefStmt &statement) override
            {
                functions.push_back(&statement);
            }

            void visit(const compiler_internal::ClassDefStmt &statement) override
            {
                (void)statement;
            }

            std::vector<const compiler_internal::FunctionDefStmt *> functions;
        };

        const SymbolTable *find_enclosing_scope(const std::vector<const SymbolTable *> &enclosing,
                                                const std::string &name)
        {
            for (const SymbolTable *symbols : enclosing)
            {
                if (symbols && symbols->resolves_lexically(name))
                    return symbols;
            }
            return nullptr;
        }
    }

    SymbolTable::SymbolTable(SymbolScope scope)
        : scope_(scope)
    {
    }

    SymbolScope SymbolTable::scope() const noexcept
    {
        return scope_;
    }

    std::size_t SymbolTable::add_name(const std::string &name)
    {
        for (std::size_t index = 0; index < names_.size(); ++index)
        {
            if (names_[index] == name)
                return index;
        }
        names_.push_back(name);
        return names_.size() - 1;
    }

    std::vector<Object *> SymbolTable::make_name_objects(Runtime &runtime) const
    {
        std::vector<Object *> names;
        names.reserve(names_.size());
        for (const std::string &name : names_)
            names.push_back(runtime.new_str(name));
        return names;
    }

    const std::vector<std::string> &SymbolTable::names() const noexcept
    {
        return names_;
    }

    std::size_t SymbolTable::add_local(const std::string &name)
    {
        for (std::size_t index = 0; index < varnames_.size(); ++index)
        {
            if (varnames_[index] == name)
                return index;
        }
        varnames_.push_back(name);
        return varnames_.size() - 1;
    }

    bool SymbolTable::is_local(const std::string &name) const
    {
        for (const std::string &entry : varnames_)
        {
            if (entry == name)
                return true;
        }
        return false;
    }

    std::size_t SymbolTable::local_index(const std::string &name) const
    {
        for (std::size_t index = 0; index < varnames_.size(); ++index)
        {
            if (varnames_[index] == name)
                return index;
        }
        return static_cast<std::size_t>(-1);
    }

    std::size_t SymbolTable::add_cellvar(const std::string &name)
    {
        for (std::size_t index = 0; index < cellvars_.size(); ++index)
        {
            if (cellvars_[index] == name)
                return index;
        }
        cellvars_.push_back(name);
        return cellvars_.size() - 1;
    }

    bool SymbolTable::is_cellvar(const std::string &name) const
    {
        for (const std::string &entry : cellvars_)
        {
            if (entry == name)
                return true;
        }
        return false;
    }

    std::size_t SymbolTable::cell_index(const std::string &name) const
    {
        for (std::size_t index = 0; index < cellvars_.size(); ++index)
        {
            if (cellvars_[index] == name)
                return index;
        }
        return static_cast<std::size_t>(-1);
    }

    std::size_t SymbolTable::add_freevar(const std::string &name)
    {
        for (std::size_t index = 0; index < freevars_.size(); ++index)
        {
            if (freevars_[index] == name)
                return index;
        }
        freevars_.push_back(name);
        return freevars_.size() - 1;
    }

    bool SymbolTable::is_freevar(const std::string &name) const
    {
        for (const std::string &entry : freevars_)
        {
            if (entry == name)
                return true;
        }
        return false;
    }

    std::size_t SymbolTable::free_index(const std::string &name) const
    {
        for (std::size_t index = 0; index < freevars_.size(); ++index)
        {
            if (freevars_[index] == name)
                return index;
        }
        return static_cast<std::size_t>(-1);
    }

    bool SymbolTable::resolves_lexically(const std::string &name) const
    {
        return !is_global(name) && (is_local(name) || is_cellvar(name) || is_freevar(name));
    }

    std::size_t SymbolTable::deref_index(const std::string &name) const
    {
        const std::size_t cell = cell_index(name);
        if (cell != static_cast<std::size_t>(-1))
            return cell;
        const std::size_t free = free_index(name);
        if (free != static_cast<std::size_t>(-1))
            return cellvars_.size() + free;
        return static_cast<std::size_t>(-1);
    }

    const std::vector<std::string> &SymbolTable::freevars() const noexcept
    {
        return freevars_;
    }

    const std::vector<std::string> &SymbolTable::cellvars() const noexcept
    {
        return cellvars_;
    }

    void SymbolTable::add_global(const std::string &name)
    {
        if (!is_global(name))
            globals_.push_back(name);
    }

    bool SymbolTable::is_global(const std::string &name) const
    {
        for (const std::string &entry : globals_)
        {
            if (entry == name)
                return true;
        }
        return false;
    }

    void SymbolTable::add_nonlocal(const std::string &name)
    {
        if (!is_nonlocal(name))
            nonlocals_.push_back(name);
    }

    bool SymbolTable::is_nonlocal(const std::string &name) const
    {
        for (const std::string &entry : nonlocals_)
        {
            if (entry == name)
                return true;
        }
        return false;
    }

    const std::vector<std::string> &SymbolTable::nonlocals() const noexcept
    {
        return nonlocals_;
    }

    std::vector<Object *> SymbolTable::make_varname_objects(Runtime &runtime) const
    {
        std::vector<Object *> varnames;
        varnames.reserve(varnames_.size());
        for (const std::string &name : varnames_)
            varnames.push_back(runtime.new_str(name));
        return varnames;
    }

    std::vector<Object *> SymbolTable::make_cellvar_objects(Runtime &runtime) const
    {
        std::vector<Object *> cellvars;
        cellvars.reserve(cellvars_.size());
        for (const std::string &name : cellvars_)
            cellvars.push_back(runtime.new_str(name));
        return cellvars;
    }

    std::vector<Object *> SymbolTable::make_freevar_objects(Runtime &runtime) const
    {
        std::vector<Object *> freevars;
        freevars.reserve(freevars_.size());
        for (const std::string &name : freevars_)
            freevars.push_back(runtime.new_str(name));
        return freevars;
    }

    SymbolTable analyze_function_symbols(Runtime &runtime,
                                         const compiler_internal::FunctionDefStmt &function,
                                         const std::vector<const SymbolTable *> &enclosing)
    {
        SymbolTable symbols(SymbolScope::Function);
        DirectiveCollector directives(runtime, symbols);
        for (const auto &statement : function.body)
            statement->accept(directives);

        for (const std::string &param : function.params.positional)
        {
            if (symbols.is_global(param) || symbols.is_nonlocal(param))
                raise_syntax_error(&runtime, "name '" + param + "' is parameter and nonlocal/global");
            (void)symbols.add_local(param);
        }
        if (function.params.varargs)
        {
            if (symbols.is_global(*function.params.varargs) || symbols.is_nonlocal(*function.params.varargs))
                raise_syntax_error(&runtime, "name '" + *function.params.varargs + "' is parameter and nonlocal/global");
            (void)symbols.add_local(*function.params.varargs);
        }
        if (function.params.varkeywords)
        {
            if (symbols.is_global(*function.params.varkeywords) || symbols.is_nonlocal(*function.params.varkeywords))
                raise_syntax_error(&runtime, "name '" + *function.params.varkeywords + "' is parameter and nonlocal/global");
            (void)symbols.add_local(*function.params.varkeywords);
        }

        FunctionLocalCollector locals(symbols);
        for (const auto &statement : function.body)
            statement->accept(locals);

        SymbolCollector collector(symbols);
        for (const auto &statement : function.body)
            statement->accept(collector);

        LexicalReferenceCollector references;
        for (const auto &statement : function.body)
            statement->accept(references);
        if (!enclosing.empty())
        {
            for (const std::string &name : references.references())
            {
                if (!symbols.is_local(name) &&
                    !symbols.is_global(name) &&
                    find_enclosing_scope(enclosing, name))
                    (void)symbols.add_freevar(name);
            }
        }

        for (const std::string &name : symbols.nonlocals())
        {
            if (!find_enclosing_scope(enclosing, name))
                raise_syntax_error(&runtime, "no binding for nonlocal '" + name + "' found");
            (void)symbols.add_freevar(name);
        }

        NestedFunctionCollector nested_functions;
        for (const auto &statement : function.body)
            statement->accept(nested_functions);
        std::vector<const SymbolTable *> nested_enclosing;
        nested_enclosing.reserve(enclosing.size() + 1);
        nested_enclosing.push_back(&symbols);
        nested_enclosing.insert(nested_enclosing.end(), enclosing.begin(), enclosing.end());
        for (const compiler_internal::FunctionDefStmt *nested : nested_functions.functions)
        {
            SymbolTable nested_symbols = analyze_function_symbols(runtime, *nested, nested_enclosing);
            for (const std::string &freevar : nested_symbols.freevars())
            {
                if (symbols.is_local(freevar))
                    (void)symbols.add_cellvar(freevar);
                else if (!symbols.is_freevar(freevar) && find_enclosing_scope(enclosing, freevar))
                    (void)symbols.add_freevar(freevar);
            }
        }

        return symbols;
    }

    SymbolTable analyze_class_symbols(Runtime &runtime,
                                      const compiler_internal::ClassDefStmt &klass)
    {
        SymbolTable symbols(SymbolScope::Class);
        SymbolCollector collector(symbols);
        for (const auto &statement : klass.body)
            statement->accept(collector);

        SymbolTable symbols_with_class = symbols;
        (void)symbols_with_class.add_cellvar("__class__");
        bool needs_class_cell = false;
        NestedFunctionCollector nested_functions;
        for (const auto &statement : klass.body)
            statement->accept(nested_functions);
        for (const compiler_internal::FunctionDefStmt *function : nested_functions.functions)
        {
            SymbolTable function_symbols = analyze_function_symbols(runtime, *function, {&symbols_with_class});
            if (function_symbols.is_freevar("__class__"))
            {
                needs_class_cell = true;
                break;
            }
        }
        if (!needs_class_cell)
            return symbols;
        (void)symbols_with_class.add_name("__classcell__");
        return symbols_with_class;
    }

    SymbolTable collect_symbols(const compiler_internal::Expr &expression)
    {
        SymbolTable symbols(SymbolScope::Module);
        SymbolCollector collector(symbols);
        expression.accept(collector);
        return symbols;
    }

    SymbolTable collect_symbols(const compiler_internal::Module &module)
    {
        SymbolTable symbols(SymbolScope::Module);
        SymbolCollector collector(symbols);
        for (const auto &statement : module.statements)
            statement->accept(collector);
        return symbols;
    }
}
