#include "Import/UsdShaderGraphBuilder.h"

void USDShaderGraphBuilder::TopoSort() {
    std::set<pxr::SdfPath> done;
    std::function<void(const ShaderNode&)> dfs = [&](auto const& node) {
        for (auto& in : node.inputs) {
            for (auto& src : in.GetConnectedSources()) {
                auto cp = src.source.GetPrim().GetPath();
                if (nodesByPath.count(cp) && !done.count(cp)) {
                    dfs(nodesByPath.at(cp));
                }
            }
        }
        done.insert(node.path);
        topoSorted.push_back(node);
        };

    if (!rootPath.IsEmpty() && nodesByPath.count(rootPath)) {
        dfs(nodesByPath.at(rootPath));
    }
    else {
        for (auto& [path, node] : nodesByPath) {
            if (!done.count(path))
                dfs(node);
        }
    }
}

// Recursively discover *all* upstream shaders
void USDShaderGraphBuilder::DiscoverShader(const pxr::UsdShadeShader& sh) {
    auto p = sh.GetPath();
    if (!visited.insert(p).second) return;  // already seen

    ShaderNode node;
    node.path = p;
    node.shader = sh;
    node.inputs = sh.GetInputs();
    node.outputs = sh.GetOutputs();
    nodesByPath[p] = node;

    // recurse on every upstream connection
    for (auto& in : node.inputs) {
        for (auto& src : in.GetConnectedSources()) {
            if (src.source.GetPrim().IsA<pxr::UsdShadeShader>()) {
                DiscoverShader(
                    pxr::UsdShadeShader(src.source.GetPrim()));
            }
        }
    }
}