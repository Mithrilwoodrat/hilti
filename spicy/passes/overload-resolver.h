
#ifndef SPICY_PASSES_OVERLOAD_RESOLVER_H
#define SPICY_PASSES_OVERLOAD_RESOLVER_H

#include <ast/pass.h>

#include "../ast-info.h"
#include "../common.h"

namespace spicy {
namespace passes {

/// Resolves overloaded function calls. It replaces nodes of type ID with the
/// function that it's referencing.
class OverloadResolver : public ast::Pass<AstInfo> {
public:
    OverloadResolver(shared_ptr<Module> module);

    virtual ~OverloadResolver();

    /// Resolves function overloads.
    ///
    /// module: The AST to resolve.
    ///
    /// Returns: True if no errors were encountered.
    bool run(shared_ptr<ast::NodeBase> ast) override;

protected:
    void visit(expression::UnresolvedOperator* o) override;

private:
    shared_ptr<Module> _module;
};
}
}

#endif
