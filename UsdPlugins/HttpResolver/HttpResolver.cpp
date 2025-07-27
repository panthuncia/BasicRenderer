#include "HttpResolver.h"

#include <pxr/usd/ar/defineResolver.h> 
#include <spdlog/spdlog.h>
#include <curl/curl.h>

PXR_NAMESPACE_OPEN_SCOPE

size_t HttpResolver::_WriteData(void* ptr, size_t size, size_t nmemb, void* userp) {
    auto* stream = static_cast<std::ofstream*>(userp);
    stream->write(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

bool HttpResolver::_IsHttpURL(const std::string& path) {
    return TfStringStartsWith(path, "http://") ||
        TfStringStartsWith(path, "https://");
}

void HttpResolver::_FetchURLToFile(const std::string& url, const fs::path& outPath) const {
    CURL* curl = curl_easy_init();
    if (!curl) return;

    std::ofstream ofs(outPath, std::ios::binary);
    if (!ofs) {
        curl_easy_cleanup(curl);
        return;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _WriteData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ofs);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
}

ArResolvedPath HttpResolver::_Resolve(const std::string& assetPath) const {
    spdlog::debug("USDHttpResolver resolving: {}", assetPath);
    if (_IsHttpURL(assetPath)) {
        fs::path tmpDir = fs::temp_directory_path();
        auto ext = fs::path(assetPath).extension().string();
        auto hashValue = std::to_string(TfHash{}(assetPath));
        fs::path cacheFile = tmpDir / (hashValue + ext);

        static std::mutex s_mutex;
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!fs::exists(cacheFile)) {
            _FetchURLToFile(assetPath, cacheFile);
        }
        return ArResolvedPath(cacheFile.string());
    }
    return ArDefaultResolver().Resolve(assetPath);
}

AR_DEFINE_RESOLVER(HttpResolver, ArResolver);

PXR_NAMESPACE_CLOSE_SCOPE