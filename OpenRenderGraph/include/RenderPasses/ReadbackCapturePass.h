#pragma once

#include <boost/container_hash/hash.hpp>

#include "RenderPasses/Base/RenderPass.h"
#include "Render/Runtime/IReadbackService.h"
#include "Render/ResourceRequirements.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include "Resources/ResourceStateTracker.h"

struct ReadbackCaptureInputs {
    ResourceHandleAndRange target;
};

// Hash function
inline rg::Hash64 HashValue(const ReadbackCaptureInputs& i) {
    std::size_t seed = 0;
    boost::hash_combine(seed, i.target.resource.GetGlobalResourceID());
    boost::hash_combine(seed, i.target.range.mipLower.type);
    boost::hash_combine(seed, i.target.range.mipLower.value);
    boost::hash_combine(seed, i.target.range.mipUpper.type);
    boost::hash_combine(seed, i.target.range.mipUpper.value);
    boost::hash_combine(seed, i.target.range.sliceLower.type);
    boost::hash_combine(seed, i.target.range.sliceLower.value);
    boost::hash_combine(seed, i.target.range.sliceUpper.type);
    boost::hash_combine(seed, i.target.range.sliceUpper.value);
    return static_cast<rg::Hash64>(seed);
}

// Compare function
inline bool operator==(const ReadbackCaptureInputs& a, const ReadbackCaptureInputs& b) {
    return a.target.resource.GetGlobalResourceID() == b.target.resource.GetGlobalResourceID() &&
           a.target.range.mipLower == b.target.range.mipLower &&
           a.target.range.mipUpper == b.target.range.mipUpper &&
           a.target.range.sliceLower == b.target.range.sliceLower &&
           a.target.range.sliceUpper == b.target.range.sliceUpper;
}

class ReadbackCapturePass final : public RenderPass {
public:
    ReadbackCapturePass(
        ReadbackCaptureInputs inputs,
        ReadbackCaptureCallback callback,
        rg::runtime::IReadbackService* readbackService)
        : m_callback(std::move(callback)),
        m_readbackService(readbackService) {
        SetInputs(inputs);
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) override {
        const auto& inputs = Inputs<ReadbackCaptureInputs>();
        builder->WithCopySource(inputs.target);
    }

    void Setup() override {
    }

    void ExecuteImmediate(ImmediateExecutionContext& context) override {
        const auto& inputs = Inputs<ReadbackCaptureInputs>();
        auto* resource = m_resourceRegistryView->Resolve<Resource>(inputs.target.resource);
        if (!resource) {
            return;
        }

        ReadbackCaptureRequest request{};
        request.desc.range = inputs.target.range;
        request.desc.resourceId = resource->GetGlobalResourceID();

        if (resource->HasLayout()) {
            auto* texture = dynamic_cast<PixelBuffer*>(resource);
            if (!texture) {
                throw std::runtime_error("ReadbackCapturePass: texture resource type mismatch.");
            }

            const auto handle = inputs.target.resource;
            const SubresourceRange sr = ResolveRangeSpec(inputs.target.range, handle.GetNumMipLevels(), handle.GetArraySize());
            if (sr.isEmpty()) {
                return;
            }

            std::vector<rhi::CopyableFootprint> footprints(sr.mipCount * sr.sliceCount);
            rhi::FootprintRangeDesc fr{};
            fr.texture = texture->GetAPIResource().GetHandle();
            fr.firstMip = sr.firstMip;
            fr.mipCount = sr.mipCount;
            fr.firstArraySlice = sr.firstSlice;
            fr.arraySize = sr.sliceCount;
            fr.firstPlane = 0;
            fr.planeCount = 1;
            fr.baseOffset = 0;

            auto info = context.device.GetCopyableFootprints(fr, footprints.data(), static_cast<uint32_t>(footprints.size()));

            auto readbackBuffer = Buffer::CreateShared(rhi::HeapType::Readback, info.totalBytes);
            readbackBuffer->SetName("ReadbackCaptureBuffer");

            for (uint32_t slice = 0; slice < sr.sliceCount; ++slice) {
                for (uint32_t mip = 0; mip < sr.mipCount; ++mip) {
                    const uint32_t subresourceIndex = (slice * sr.mipCount) + mip;
                    const auto& fp = footprints[subresourceIndex];

                    context.list.CopyTextureToBuffer(
                        texture,
                        sr.firstMip + mip,
                        sr.firstSlice + slice,
                        readbackBuffer.get(),
                        fp,
                        0,
                        0,
                        0);
                }
            }

            request.desc.kind = ReadbackResourceKind::Texture;
            request.readbackBuffer = readbackBuffer;
            request.layouts = std::move(footprints);
            request.totalSize = info.totalBytes;
            request.format = texture->GetFormat();
            request.width = texture->GetWidth();
            request.height = texture->GetHeight();
            request.depth = 1;
        }
        else {
            uint64_t byteSize = 0;
            if (!resource->TryGetBufferByteSize(byteSize) || byteSize == 0) {
                throw std::runtime_error("ReadbackCapturePass: resource is not a texture (has no layout) and does not expose a buffer byte size for readback.");
            }
            auto readbackBuffer = Buffer::CreateShared(rhi::HeapType::Readback, byteSize);
            readbackBuffer->SetName("ReadbackCaptureBuffer");

            context.list.CopyBufferRegion(readbackBuffer.get(), 0, resource, 0, byteSize);

            request.desc.kind = ReadbackResourceKind::Buffer;
            request.readbackBuffer = readbackBuffer;
            request.totalSize = byteSize;
        }

        request.callback = m_callback;
        if (!m_readbackService) {
            return;
        }

        m_pendingToken = m_readbackService->EnqueueCapture(std::move(request));
        m_hasPendingToken = true;
    }

    PassReturn Execute(PassExecutionContext& context) override {
        if (!m_hasPendingToken) {
            return {};
        }

        if (!m_readbackService) {
            m_hasPendingToken = false;
            return {};
        }

        const uint64_t fenceValue = m_readbackService->GetNextReadbackFenceValue();
        m_readbackService->FinalizeCapture(m_pendingToken, fenceValue);
        m_hasPendingToken = false;
        return { m_readbackService->GetReadbackFence(), fenceValue };
    }

    void Cleanup() override {
    }

private:
    ReadbackCaptureCallback m_callback;
    rg::runtime::ReadbackCaptureToken m_pendingToken{};
    rg::runtime::IReadbackService* m_readbackService = nullptr; // non-owning
    bool m_hasPendingToken = false;
};