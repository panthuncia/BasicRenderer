#pragma once

#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodRayTracingSystem.h"
#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include "ShaderBuffers.h"
#include "../shaders/PerPassRootConstants/clodRayTracingSetupRootConstants.h"

struct RayTracedReflectionsFrameData {
    std::shared_ptr<br::render::CLodRayTracingSystem> service;
    std::shared_ptr<PixelBuffer> output;
    std::shared_ptr<Buffer> pageSources, buildInfos, clasData, clasAddresses;
    std::shared_ptr<Buffer> blasData, blasAddresses, tlasInstances;
    org::PreparedProgramBinding setup{}, tlasSetup{};
    rhi::DescriptorHeapHandle resourceHeap{}, samplerHeap{};
    uint32_t pageSourceCount = 0, buildClusterCapacity = 0;
};

class RayTracedReflectionsPass
    : public org::TypedRenderGraphPass<RayTracedReflectionsPass,
          RayTracedReflectionsFrameData> {
public:
    RayTracedReflectionsPass() {
        auto& manager = PSOManager::GetInstance();
        m_setupPso = manager.MakeComputePipeline(manager.GetComputeRootSignature().GetHandle(),
            L"Shaders/ClusterLOD/rayTracingSetup.hlsl", L"CLodRayTracingSetupCSMain", {},
            "CLod.RayTracing.Setup.PSO");
        m_tlasSetupPso = manager.MakeComputePipeline(manager.GetComputeRootSignature().GetHandle(),
            L"Shaders/ClusterLOD/rayTracingTlasSetup.hlsl", L"CLodRayTracingTlasSetupCSMain", {},
            "CLod.RayTracing.TLASSetup.PSO");
    }

    void Declare(org::PassBuilder& builder) {
        builder.WithShaderResource(Builtin::Color::HDRColorTarget,
            Builtin::PrimaryCamera::DepthTexture, Builtin::Surface::NormalRoughness,
            Builtin::Surface::SpecularAo, Builtin::CameraBuffer,
            Builtin::Environment::CurrentPrefilteredCubemap);
        builder.WithUnorderedAccess(Builtin::PostProcessing::ScreenSpaceReflections);
        builder.WithInternalTransition(
            ResourceIdentifierAndRange(Builtin::PostProcessing::ScreenSpaceReflections, {}),
            ResourceState{.access = rhi::ResourceAccessType::Common,
                .layout = rhi::ResourceLayout::Common,
                .sync = rhi::ResourceSyncState::All});
    }

    void Initialize() {
        m_output = m_resourceRegistryView->RequestSharedAs<PixelBuffer>(
            Builtin::PostProcessing::ScreenSpaceReflections);
    }

    RayTracedReflectionsFrameData Prepare(const org::PassPrepareContext& preparation) {
        RayTracedReflectionsFrameData frame{};
        const auto* context = preparation.preparationData
            ? preparation.preparationData->Get<UpdateContext>() : nullptr;
        if (!context || !m_output || !context->clodRayTracingSystem) return frame;
        auto service = context->clodRayTracingSystem;
        std::scoped_lock serviceLock(service->FrameOperationMutex());
        if (!service->HasGpuClasBuildInputs()) return frame;
        service->EnsureRayTracingPipeline(DeviceManager::GetInstance().GetDevice(),
            DeviceManager::GetInstance().GetRayTracingFeatures());
        frame.service = service;
        frame.output = m_output;
        frame.pageSources = service->GetPageSourceBuffer();
        frame.buildInfos = service->GetClasBuildInfoBuffer();
        frame.clasData = service->GetClasDataBuffer();
        frame.clasAddresses = service->GetClasAddressBuffer();
        frame.blasData = service->GetBlasDataBuffer();
        frame.blasAddresses = service->GetBlasAddressBuffer();
        frame.tlasInstances = service->GetTlasInstanceBuffer();
        frame.setup = preparation.CaptureProgramBinding(m_setupPso);
        frame.tlasSetup = preparation.CaptureProgramBinding(m_tlasSetupPso);
        frame.resourceHeap = context->textureDescriptorHeap.GetHandle();
        frame.samplerHeap = context->samplerDescriptorHeap.GetHandle();
        frame.pageSourceCount = service->GetGpuPageSourceCount();
        frame.buildClusterCapacity = service->GetStats().buildableClusters;
        preparation.Retain(frame.output);
        preparation.Retain(frame.pageSources); preparation.Retain(frame.buildInfos);
        preparation.Retain(frame.clasData); preparation.Retain(frame.clasAddresses);
        preparation.Retain(frame.blasData); preparation.Retain(frame.blasAddresses);
        preparation.Retain(frame.tlasInstances);
        return frame;
    }

    static void Record(const RayTracedReflectionsFrameData& frame,
        org::PassRecordContext& recording) {
        if (!frame.output) return;
        std::unique_lock<std::mutex> serviceLock;
        if (frame.service) serviceLock = std::unique_lock(frame.service->FrameOperationMutex());
        auto& commands = recording.Commands();
        commands.SetDescriptorHeaps(frame.resourceHeap, frame.samplerHeap);
        bool traced = false;
        auto bind = [&](const org::PreparedProgramBinding& program) {
            commands.BindLayout(recording.ResolveLayout(program.program));
            commands.BindPipeline(recording.Resolve(program.program));
            if (!program.descriptorIndices.empty()) commands.PushConstants(
                rhi::ShaderStage::Compute, 0,
                org::shaderapi::kResourceDescriptorIndicesRootParameter, 0,
                static_cast<uint32_t>(program.descriptorIndices.size()),
                program.descriptorIndices.data());
        };
        auto barrier = [&](const std::shared_ptr<Buffer>& buffer,
            rhi::ResourceAccessType before, rhi::ResourceAccessType after,
            rhi::ResourceSyncState beforeSync, rhi::ResourceSyncState afterSync) {
            if (!buffer) return;
            rhi::BufferBarrier value{};
            value.buffer = buffer->GetAPIResource().GetHandle();
            value.beforeAccess = before; value.afterAccess = after;
            value.beforeSync = beforeSync; value.afterSync = afterSync;
            rhi::BarrierBatch batch{}; batch.buffers = {&value}; commands.Barriers(batch);
        };
        if (frame.service && frame.pageSources && frame.buildInfos) {
            bind(frame.setup);
            uint32_t constants[NumMiscUintRootConstants]{};
            constants[CLOD_RT_SETUP_PAGE_SOURCES_DESCRIPTOR_INDEX] =
                frame.pageSources->GetSRVInfo(0).slot.index;
            constants[CLOD_RT_SETUP_BUILD_INFOS_DESCRIPTOR_INDEX] =
                frame.buildInfos->GetUAVShaderVisibleInfo(0).slot.index;
            constants[CLOD_RT_SETUP_PAGE_SOURCE_COUNT] = frame.pageSourceCount;
            constants[CLOD_RT_SETUP_BUILD_CLUSTER_CAPACITY] = frame.buildClusterCapacity;
            commands.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex,
                0, NumMiscUintRootConstants, constants);
            commands.Dispatch((frame.pageSourceCount + 63u) / 64u, 1u, 1u);
            barrier(frame.buildInfos, rhi::ResourceAccessType::UnorderedAccess,
                rhi::ResourceAccessType::ShaderResource, rhi::ResourceSyncState::ComputeShading,
                rhi::ResourceSyncState::BuildRaytracingAccelerationStructure);
            frame.service->ExecuteClasBuild(commands);
            if (frame.clasData && frame.clasAddresses && frame.service->HasGpuBlasBuildInputs()) {
                barrier(frame.clasData, rhi::ResourceAccessType::RaytracingAccelerationStructureWrite,
                    rhi::ResourceAccessType::RaytracingAccelerationStructureRead,
                    rhi::ResourceSyncState::BuildRaytracingAccelerationStructure,
                    rhi::ResourceSyncState::BuildRaytracingAccelerationStructure);
                barrier(frame.clasAddresses, rhi::ResourceAccessType::RaytracingAccelerationStructureWrite,
                    rhi::ResourceAccessType::RaytracingAccelerationStructureRead,
                    rhi::ResourceSyncState::BuildRaytracingAccelerationStructure,
                    rhi::ResourceSyncState::BuildRaytracingAccelerationStructure);
                frame.service->ExecuteBlasBuild(commands);
                if (frame.blasData && frame.blasAddresses && frame.tlasInstances &&
                    frame.service->HasGpuTlasBuildInputs()) {
                    barrier(frame.blasData, rhi::ResourceAccessType::RaytracingAccelerationStructureWrite,
                        rhi::ResourceAccessType::RaytracingAccelerationStructureRead,
                        rhi::ResourceSyncState::BuildRaytracingAccelerationStructure,
                        rhi::ResourceSyncState::BuildRaytracingAccelerationStructure);
                    barrier(frame.blasAddresses, rhi::ResourceAccessType::RaytracingAccelerationStructureWrite,
                        rhi::ResourceAccessType::ShaderResource,
                        rhi::ResourceSyncState::BuildRaytracingAccelerationStructure,
                        rhi::ResourceSyncState::ComputeShading);
                    bind(frame.tlasSetup);
                    uint32_t tlas[NumMiscUintRootConstants]{};
                    tlas[CLOD_RT_SETUP_BLAS_ADDRESSES_DESCRIPTOR_INDEX] =
                        frame.blasAddresses->GetSRVInfo(0).slot.index;
                    tlas[CLOD_RT_SETUP_TLAS_INSTANCES_DESCRIPTOR_INDEX] =
                        frame.tlasInstances->GetUAVShaderVisibleInfo(0).slot.index;
                    commands.PushConstants(rhi::ShaderStage::Compute, 0,
                        MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, tlas);
                    commands.Dispatch(1u, 1u, 1u);
                    barrier(frame.tlasInstances, rhi::ResourceAccessType::UnorderedAccess,
                        rhi::ResourceAccessType::RaytracingAccelerationStructureRead,
                        rhi::ResourceSyncState::ComputeShading,
                        rhi::ResourceSyncState::BuildRaytracingAccelerationStructure);
                    frame.service->ExecuteTlasBuild(commands);
                    if (frame.service->HasRayTracingPipeline()) {
                        frame.service->ExecuteTraceRays(DeviceManager::GetInstance().GetDevice(),
                            commands, *frame.output, frame.output->GetWidth(), frame.output->GetHeight());
                        traced = frame.service->GetStats().traceRaysSubmitted;
                    }
                }
            }
        }
        if (!traced) {
            rhi::UavClearInfo clear{};
            clear.cpuVisible = frame.output->GetUAVNonShaderVisibleInfo(0).slot;
            clear.shaderVisible = frame.output->GetUAVShaderVisibleInfo(0).slot;
            clear.resource = frame.output->GetAPIResource();
            commands.ClearUavFloat(clear, {});
        }
    }

private:
    PipelineState m_setupPso, m_tlasSetupPso;
    std::shared_ptr<PixelBuffer> m_output;
};
