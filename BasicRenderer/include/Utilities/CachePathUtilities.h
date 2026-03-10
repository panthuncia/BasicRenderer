#pragma once

// Minimal utility functions needed by CLod cache I/O code.

#include <string>
#include <string_view>

std::string ws2s(const std::wstring_view& wstr);
std::wstring s2ws(const std::string_view& str);
std::wstring GetCacheFilePath(const std::wstring& fileName, const std::wstring& directory);
