#pragma once

namespace rg::util {

    inline std::wstring s2ws(const std::string_view& utf8)
    {
        if (utf8.empty()) return {};
        int needed = ::MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            utf8.data(),
            static_cast<int>(utf8.size()),
            nullptr,
            0
        );
        if (needed == 0)
            throw std::system_error(::GetLastError(), std::system_category(),
                "MultiByteToWideChar(size)");

        std::wstring out(needed, L'\0');

        int written = ::MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            utf8.data(),
            static_cast<int>(utf8.size()),
            out.data(),
            needed
        );
        if (written == 0)
            throw std::system_error(::GetLastError(), std::system_category(),
                "MultiByteToWideChar(data)");

        return out;
    }

    inline std::string ws2s(const std::wstring_view& wide)
    {
        if (wide.empty()) return {};

        int needed = ::WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wide.data(),
            static_cast<int>(wide.size()),
            nullptr,
            0,
            nullptr, nullptr
        );
        if (needed == 0)
            throw std::system_error(::GetLastError(), std::system_category(),
                "WideCharToMultiByte(size)");

        std::string out(needed, '\0');

        int written = ::WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wide.data(),
            static_cast<int>(wide.size()),
            out.data(),
            needed,
            nullptr, nullptr
        );
        if (written == 0)
            throw std::system_error(::GetLastError(), std::system_category(),
                "WideCharToMultiByte(data)");

        return out;
    }

    inline uint16_t CalculateMipLevels(uint16_t width, uint16_t height) {
        return static_cast<uint16_t>(std::floor(std::log2((std::max)(width, height)))) + 1;
    }
}