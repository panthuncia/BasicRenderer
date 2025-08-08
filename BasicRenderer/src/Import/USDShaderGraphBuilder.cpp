#include "Import/UsdShaderGraphBuilder.h"

void USDShaderGraphBuilder::TopoSort() {
    std::set<pxr::SdfPath> done;
    std::function<void(const ConnectableNode&)> dfs = [&](auto const& node) {
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
void USDShaderGraphBuilder::DiscoverConnectable(const pxr::UsdShadeConnectableAPI& c) {
    auto p = c.GetPrim().GetPath();
    if (!visited.insert(p).second) return;

    ConnectableNode n;
    n.path = p;
    n.connectable = c;
    n.inputs = c.GetInputs();
    n.outputs = c.GetOutputs();
    nodesByPath[p] = n;

    // Recurse upstream through *inputs* (for topo sort we still chase producers)
    for (auto& in : n.inputs) {
        for (auto& s : in.GetConnectedSources()) {
            DiscoverConnectable(pxr::UsdShadeConnectableAPI(s.source.GetPrim()));
        }
    }
}