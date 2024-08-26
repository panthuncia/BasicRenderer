#pragma once

#include <vector>
#include <string>
#include <DirectXMath.h>

#include "SceneNode.h"

class Camera : public SceneNode {
public:
	Camera(std::string name);
private:
protected:
    XMFLOAT3 lookAt;
    XMFLOAT3 up;
    XMMATRIX viewMatrix;
    XMMATRIX viewMatrixInverse;
    XMMATRIX projectionMatrix;
    XMMATRIX viewProjectionMatrix;
    XMMATRIX viewProjectionMatrixInverse;
    float fieldOfView;
    float aspectRatio;
    float zNear;
    float zFar;

	void onUpdate() override;
};