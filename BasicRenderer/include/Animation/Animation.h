#pragma once

#include <unordered_map>
#include <string>

#include "Animation/AnimationClip.h"

class Animation {
public:
	Animation(std::string name) : name(name) {
	}
	std::string name;
	std::unordered_map<std::string, std::shared_ptr<AnimationClip>> nodesMap;

};