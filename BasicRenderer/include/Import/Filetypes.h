#pragma once
#include <unordered_map>
#include <unordered_set>

enum class ImageFiletype {
	UNKNOWN,
	PNG,
	JPEG,
	BMP,
	DDS
};

enum class ImageLoader {
	UNKNOWN,
	STBImage,
	DirectXTex
};

static std::unordered_map<ImageFiletype, ImageLoader> imageFiletypeToLoader = {
	{ImageFiletype::PNG, ImageLoader::DirectXTex},
	{ImageFiletype::JPEG, ImageLoader::STBImage},
	{ImageFiletype::BMP, ImageLoader::STBImage},
	{ImageFiletype::DDS, ImageLoader::DirectXTex}
};

static std::unordered_map<std::string, ImageFiletype> extensionToFiletype = {
	{".png", ImageFiletype::PNG},
	{".jpg", ImageFiletype::JPEG},
	{".jpeg", ImageFiletype::JPEG},
	{".bmp", ImageFiletype::BMP},
	{".dds", ImageFiletype::DDS},
	{"png", ImageFiletype::PNG},
	{"jpg", ImageFiletype::JPEG},
	{"jpeg", ImageFiletype::JPEG},
	{"bmp", ImageFiletype::BMP},
	{"dds", ImageFiletype::DDS}
};

static std::unordered_set<ImageFiletype> DirectXTexSupportedFiletypes = {
	ImageFiletype::DDS
};

static std::unordered_set<ImageFiletype> STBImageSupportedFiletypes = {
	ImageFiletype::PNG,
	ImageFiletype::JPEG,
	ImageFiletype::BMP
};