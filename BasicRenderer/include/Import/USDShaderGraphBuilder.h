#pragma once

#include <memory>
#include <vector>
#include <DirectXMath.h>

#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/material.h>

struct ConnectableNode {
    pxr::SdfPath path;
    pxr::UsdShadeConnectableAPI connectable;
    std::vector<pxr::UsdShadeInput> inputs;
    std::vector<pxr::UsdShadeOutput> outputs;
};

class USDShaderGraphBuilder {
public:
    USDShaderGraphBuilder(const pxr::UsdShadeMaterial& m)
        : material(m) {
    }

    void Build() {
        // Get the material's terminal OUTPUT for the universal context.
        if (pxr::UsdShadeOutput surfOut =
            material.GetSurfaceOutput(pxr::UsdShadeTokens->universalRenderContext)) // valid even if unconnected
        {
            // Walk the producers of that OUTPUT
            for (const auto& src : surfOut.GetConnectedSources()) {
                DiscoverConnectable(pxr::UsdShadeConnectableAPI(src.source.GetPrim()));
            }
            TopoSort();
        }
    }

    // All discovered nodes in topo order
    std::vector<ConnectableNode> GetTopologicalNodes() const {
        return topoSorted;
    }

private:
    pxr::UsdShadeMaterial           material;
    std::map<pxr::SdfPath, ConnectableNode> nodesByPath;
    std::vector<ConnectableNode> topoSorted;
    std::set<pxr::SdfPath> visited;
    pxr::SdfPath rootPath;

    void DiscoverConnectable(const pxr::UsdShadeConnectableAPI& c);

    // DFS topo sort
    void TopoSort();
};
