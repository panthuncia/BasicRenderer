#pragma once
#include <unordered_map>
#include <unordered_set>

enum class ImageFiletype {
	UNKNOWN,
	HDR,
	DDS,
	TGA,
	WIC
};

enum class ImageLoader {
	UNKNOWN,
	STBImage,
	DirectXTex
};

static std::unordered_map<ImageFiletype, ImageLoader> imageFiletypeToLoader = {
	{ImageFiletype::WIC, ImageLoader::DirectXTex},
	{ImageFiletype::UNKNOWN, ImageLoader::STBImage},
	{ImageFiletype::DDS, ImageLoader::DirectXTex}
};

static std::unordered_map<std::string, ImageFiletype> extensionToFiletype = {
	{".png", ImageFiletype::WIC},
	{".jpg", ImageFiletype::WIC},
	{".jpeg", ImageFiletype::WIC},
	{".bmp", ImageFiletype::WIC},
	{".dds", ImageFiletype::DDS},
	{".hdr", ImageFiletype::HDR},
	{".tga", ImageFiletype::TGA},
	{"png", ImageFiletype::WIC},
	{"jpg", ImageFiletype::WIC},
	{"jpeg", ImageFiletype::WIC},
	{"bmp", ImageFiletype::WIC},
	{"dds", ImageFiletype::DDS},
	{"hdr", ImageFiletype::HDR},
	{"tga", ImageFiletype::TGA }
};

static std::unordered_set<ImageFiletype> DirectXTexSupportedFiletypes = {
	ImageFiletype::DDS
};

static std::unordered_set<ImageFiletype> STBImageSupportedFiletypes = {
	ImageFiletype::UNKNOWN
};

enum class SceneFiletype {
	OTHER,
	USD,
};

enum class SceneLoader {
	UNKNOWN,
	Assimp,
	OpenUSD
};

static std::unordered_map<SceneFiletype, SceneLoader> sceneFiletypeToLoader = {
	{SceneFiletype::USD, SceneLoader::OpenUSD},
	{SceneFiletype::OTHER, SceneLoader::Assimp} // default loader
};

static std::unordered_set<std::string> usdFileExtensions = {
	".usd", ".usda", ".usdc", ".usdz",
	"usd", "usda", "usdc", "usdz"
};

inline bool isUsdExt(const std::string& ext) {
	return usdFileExtensions.contains(ext);
}

inline SceneFiletype GetSceneFiletype(const std::string& ext) {
	if (isUsdExt(ext)) {
		return SceneFiletype::USD;
	}
	return SceneFiletype::OTHER;
}

inline SceneLoader GetSceneLoader(const std::string& ext) {
	SceneFiletype filetype = GetSceneFiletype(ext);
	if (sceneFiletypeToLoader.contains(filetype)) {
		return sceneFiletypeToLoader[filetype];
	}
	return SceneLoader::Assimp; // default
}