#pragma once

#include <pxr/pxr.h>
#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/base/tf/stringUtils.h>

#include <string>
#include <mutex>
#include <filesystem>
#include <fstream>

PXR_NAMESPACE_OPEN_SCOPE

namespace fs = std::filesystem;

class HttpResolver : public ArResolver {
public:
    HttpResolver() = default;
    ~HttpResolver() override = default;

protected:
    // Write callback for libcurl, writes directly into ofstream
    static size_t _WriteData(void* ptr, size_t size, size_t nmemb, void* userp);

    static bool _IsHttpURL(const std::string& path);

    void _FetchURLToFile(const std::string& url, const fs::path& outPath) const;

    std::string _CreateIdentifier(
        const std::string& assetPath,
        const ArResolvedPath& anchor) const override
    {
        return ArDefaultResolver().CreateIdentifier(assetPath, anchor);
    }

    std::string _CreateIdentifierForNewAsset(
        const std::string& assetPath,
        const ArResolvedPath& anchor) const override
    {
        return ArDefaultResolver().CreateIdentifierForNewAsset(assetPath, anchor);
    }

    ArResolvedPath _Resolve(const std::string& assetPath) const override;

    ArResolvedPath _ResolveForNewAsset(
        const std::string& assetPath) const override
    {
        return ArDefaultResolver().ResolveForNewAsset(assetPath);
    }

    std::shared_ptr<ArAsset> _OpenAsset(
        const ArResolvedPath& resolvedPath) const override
    {
        return ArDefaultResolver().OpenAsset(resolvedPath);
    }

    std::shared_ptr<ArWritableAsset> _OpenAssetForWrite(
        const ArResolvedPath& resolvedPath,
        WriteMode mode) const override
    {
        return ArDefaultResolver().OpenAssetForWrite(resolvedPath, mode);
    }
};

PXR_NAMESPACE_CLOSE_SCOPE