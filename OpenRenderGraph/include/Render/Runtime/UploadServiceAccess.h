#pragma once

#include <cstddef>
#include <stdexcept>
#include <utility>

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

inline void UploadBufferDataDispatch(
    const void* data,
    size_t size,
    UploadTarget resourceToUpdate,
    size_t dataBufferOffset,
    const char* file,
    int line) {
    if (auto* service = GetActiveUploadService()) {
#if BUILD_TYPE == BUILD_TYPE_DEBUG
        service->UploadData(data, size, std::move(resourceToUpdate), dataBufferOffset, file, line);
#else
        service->UploadData(data, size, std::move(resourceToUpdate), dataBufferOffset);
#endif
        return;
    }

    throw std::runtime_error("Upload service is not active for BUFFER_UPLOAD");
}

inline void UploadTextureSubresourcesDispatch(
    UploadTarget target,
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
#if BUILD_TYPE == BUILD_TYPE_DEBUG
        service->UploadTextureSubresources(
            std::move(target),
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
#else
        service->UploadTextureSubresources(
            std::move(target),
            fmt,
            baseWidth,
            baseHeight,
            depthOrLayers,
            mipLevels,
            arraySize,
            srcSubresources,
            srcCount);
#endif
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
