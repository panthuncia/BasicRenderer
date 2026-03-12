#include "Utilities/CachePathUtilities.h"

#include <filesystem>
#include <mutex>
#include <system_error>
#include <unordered_set>

#include <windows.h>

std::wstring s2ws(const std::string_view& utf8)
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

std::string ws2s(const std::wstring_view& wide)
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

std::wstring GetCacheFilePath(const std::wstring& fileName, const std::wstring& directory) {
	std::filesystem::path workingDir = std::filesystem::current_path();
	std::filesystem::path cacheDir = workingDir / L"cache" / directory;

	// Avoid repeated OS syscalls: only call create_directories once per unique
	// directory path.  The set persists for the lifetime of the process.
	{
		static std::mutex s_ensuredMutex;
		static std::unordered_set<std::wstring> s_ensuredDirs;
		std::wstring cacheDirStr = cacheDir.wstring();
		std::lock_guard<std::mutex> lock(s_ensuredMutex);
		if (s_ensuredDirs.find(cacheDirStr) == s_ensuredDirs.end()) {
			std::filesystem::create_directories(cacheDir);
			s_ensuredDirs.insert(std::move(cacheDirStr));
		}
	}

	std::filesystem::path filePath = cacheDir / fileName;
	return filePath.wstring();
}

std::string NormalizeCacheSourcePath(const std::string& path) {
	if (path.empty()) return path;
	std::error_code ec;
	auto canonical = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
	if (ec) return path; // if canonicalisation fails, use original
	return canonical.generic_string(); // forward-slash, absolute
}
