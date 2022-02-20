#pragma once
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "systems/VulkanContext.h"

using namespace glm;

class Camera
{
public:

	struct UBO
	{
		mat4 model;
		mat4 view;
		mat4 proj;
	};

	Camera(vec3 startingPosition);

	[[nodiscard]] mat4 GetProjection() const
	{
		const auto sizes = VulkanContext::GraphicInstance->GetWindowSize();
		return perspective(radians(65.0f), static_cast<float>(sizes.width) / static_cast<float>(sizes.height), 0.01f, 100.0f);
	}

	[[nodiscard]] mat4 GetView() const
	{
		return lookAt(position, position + forward, up);
	}

	[[nodiscard]] vec3 GetPosition() const { return position; }
	void Move(vec3 vec);
	void Rotate(vec2 delta);
	void MoveWorld(glm::vec3 movement);
	void SetPosition(glm::vec3 _position);

private:
	void updateVectors();

	vec3 position;
	vec3 direction;
	vec3 forward;
	vec3 right;
	vec3 up;

	float lookAngle;
	vec2 angles;

	static constexpr vec3 worldUp = vec3(0, 1, 0);
};

