#pragma once

#include <memory>
#include <vector>
#include <DirectXMath.h>

#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/material.h>

struct ShaderNode {
    pxr::SdfPath                          path;
    pxr::UsdShadeShader                   shader;
    std::vector<pxr::UsdShadeInput>      inputs;
    std::vector<pxr::UsdShadeOutput>     outputs;
};

class USDShaderGraphBuilder {
public:
    USDShaderGraphBuilder(const pxr::UsdShadeMaterial& m)
        : material(m) {
    }

    void Build() {
        // find the surface shader
        if (auto surf = material.ComputeSurfaceSource()) {
            rootPath = surf.GetPath();
            DiscoverShader(surf);
        }
        // Ordering
        TopoSort();
    }

    // All discovered nodes in topo order
    std::vector<ShaderNode> GetTopologicalNodes() const {
        return topoSorted;
    }

private:
    pxr::UsdShadeMaterial           material;
    std::map<pxr::SdfPath, ShaderNode> nodesByPath;
    std::vector<ShaderNode> topoSorted;
    std::set<pxr::SdfPath> visited;
    pxr::SdfPath rootPath;

    void DiscoverShader(const pxr::UsdShadeShader& sh);

    // DFS topo sort
    void TopoSort();
};
