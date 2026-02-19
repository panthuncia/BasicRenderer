#pragma once

#include <cstddef>
#include <stdexcept>

#include "Render/Runtime/IUploadService.h"

namespace rg::runtime {

inline IUploadService*& UploadServiceSlot() {
    static IUploadService* service = nullptr;
    return service;
}

inline void SetActiveUploadService(IUploadService* service) {
    UploadServiceSlot() = service;
}

inline IUploadService* GetActiveUploadService() {
    return UploadServiceSlot();
}

template<typename UploadTargetT>
inline void UploadBufferDataDispatch(
    const void* data,
    size_t size,
    UploadTargetT resourceToUpdate,
    size_t dataBufferOffset,
    const char* file,
    int line) {
    if (auto* service = GetActiveUploadService()) {
        if (resourceToUpdate.kind == UploadTargetT::Kind::PinnedShared) {
            service->UploadDataToShared(data, size, std::move(resourceToUpdate.pinned), dataBufferOffset, file, line);
        }
        else {
            service->UploadDataToHandle(data, size, resourceToUpdate.h, dataBufferOffset, file, line);
        }
        return;
    }

    throw std::runtime_error("Upload service is not active for BUFFER_UPLOAD");
}

template<typename UploadTargetT>
inline void UploadTextureSubresourcesDispatch(
    UploadTargetT target,
    rhi::Format fmt,
    uint32_t baseWidth,
    uint32_t baseHeight,
    uint32_t depthOrLayers,
    uint32_t mipLevels,
    uint32_t arraySize,
    const rhi::helpers::SubresourceData* srcSubresources,
    uint32_t srcCount,
    const char* file,
    int line) {
    if (auto* service = GetActiveUploadService()) {
        if (target.kind == UploadTargetT::Kind::PinnedShared) {
            service->UploadTextureSubresourcesToShared(
                std::move(target.pinned),
                fmt,
                baseWidth,
                baseHeight,
                depthOrLayers,
                mipLevels,
                arraySize,
                srcSubresources,
                srcCount,
                file,
                line);
        }
        else {
            service->UploadTextureSubresourcesToHandle(
                target.h,
                fmt,
                baseWidth,
                baseHeight,
                depthOrLayers,
                mipLevels,
                arraySize,
                srcSubresources,
                srcCount,
                file,
                line);
        }
        return;
    }

    throw std::runtime_error("Upload service is not active for TEXTURE_UPLOAD_SUBRESOURCES");
}

}

#if BUILD_TYPE == BUILD_TYPE_DEBUG
#define BUFFER_UPLOAD(data,size,res,offset) \
    rg::runtime::UploadBufferDataDispatch((data),(size),(res),(offset),__FILE__,__LINE__)
#define TEXTURE_UPLOAD_SUBRESOURCES(dstTexture,fmt,baseWidth,baseHeight,depthOrLayers,mipLevels,arraySize,srcSubresources,srcCount) \
	rg::runtime::UploadTextureSubresourcesDispatch((dstTexture),(fmt),(baseWidth),(baseHeight),(depthOrLayers),(mipLevels),(arraySize),(srcSubresources),(srcCount),__FILE__,__LINE__)
#else
#define BUFFER_UPLOAD(data,size,res,offset) \
    rg::runtime::UploadBufferDataDispatch((data),(size),(res),(offset),nullptr,0)
#define TEXTURE_UPLOAD_SUBRESOURCES(dstTexture,fmt,baseWidth,baseHeight,depthOrLayers,mipLevels,arraySize,srcSubresources,srcCount) \
	rg::runtime::UploadTextureSubresourcesDispatch((dstTexture),(fmt),(baseWidth),(baseHeight),(depthOrLayers),(mipLevels),(arraySize),(srcSubresources),(srcCount),nullptr,0)
#endif
