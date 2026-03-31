#pragma once

#include <functional>
#include <unordered_set>

#include "Materials/Material.h"
#include "Materials/TechniqueDescriptor.h"
#include "Mesh/Mesh.h"
#include "../generated/BuiltinRenderPasses.h"

inline bool IsAlphaBlendTechnique(const TechniqueDescriptor& technique) {
    return (technique.compileFlags & MaterialCompileBlend) != 0;
}

inline bool IsAlphaTestTechnique(const TechniqueDescriptor& technique) {
    return (technique.compileFlags & MaterialCompileAlphaTest) != 0;
}

inline bool ShouldSkipSourcePassForCLodAlphaBlend(const RenderPhase& pass) {
    return pass == Engine::Primary::OITAccumulationPass
        || pass == Engine::Primary::GBufferPass;
}

inline bool ShouldAddSupplementalCLodShadowWorkload(const Mesh& mesh, const RenderPhase& pass) {
    return mesh.IsCLodMesh() && pass == Engine::Primary::ShadowMapsPass;
}

template<class F>
void ForEachMeshDrawWorkload(const Mesh& mesh, F&& callback) {
    const auto& technique = mesh.material->Technique();
    const bool isClodMesh = mesh.IsCLodMesh();
    const bool alphaBlend = IsAlphaBlendTechnique(technique);
    const bool alphaTest = IsAlphaTestTechnique(technique);
    const bool clodAlphaBlend = isClodMesh && alphaBlend && !alphaTest;

    for (const auto& pass : technique.passes) {
        if (clodAlphaBlend && ShouldSkipSourcePassForCLodAlphaBlend(pass)) {
            continue;
        }

        const bool clodOnly =
            isClodMesh
            && pass == Engine::Primary::GBufferPass
            && (!alphaBlend || alphaTest);
        callback(DrawWorkloadKey {
            technique.compileFlags,
            pass,
            clodOnly
        });

        // Keep the legacy shadow path alive while the CLod shadow variant is under construction.
        // CLod shadow work is mirrored into a dedicated clodOnly workload so the future CLod VSM
        // path can come online without changing mesh pass classification again.
        if (ShouldAddSupplementalCLodShadowWorkload(mesh, pass)) {
            callback(DrawWorkloadKey {
                technique.compileFlags,
                pass,
                true
            });
        }
    }

    if (clodAlphaBlend) {
        callback(DrawWorkloadKey {
            technique.compileFlags,
            Engine::Primary::CLodTransparentPass,
            true
        });
    }
}

template<class F>
void ForEachMeshRenderPhase(const Mesh& mesh, F&& callback) {
    std::unordered_set<RenderPhase, RenderPhase::Hasher> uniquePhases;
    ForEachMeshDrawWorkload(mesh, [&](const DrawWorkloadKey& workloadKey) {
        if (uniquePhases.insert(workloadKey.renderPhase).second) {
            callback(workloadKey.renderPhase);
        }
    });
}
