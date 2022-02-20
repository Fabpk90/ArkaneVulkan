#include "Camera.h"

#include <glm/glm.hpp>

//Camera* Camera::mainCamera = nullptr;


void Camera::updateVectors()
{
    direction = glm::normalize(forward - position);
    right = glm::normalize(glm::cross(direction, glm::vec3(0, 1, 0)));
    up = glm::normalize(glm::cross(right, direction));
}

Camera::Camera(vec3 startingPosition)
	:position(startingPosition),
	 forward(position + vec3(0, 0, -1)),
	 angles(0)
{}

void Camera::Move(glm::vec3 vec)
{
    position += direction * vec.z;
    position += right * vec.x;
    position += up * vec.y;

    forward = position + direction * 2.0f;

    updateVectors();
}

void Camera::Rotate(glm::vec2 delta)
{
    angles.x += delta.x;
    angles.y -= delta.y;

    if (angles.y > 89.0f)
        angles.y = 89.0f;
    else if (angles.y < -89.0f)
        angles.y = -89.0f;


    direction.x = cos(glm::radians(angles.y)) * cos(glm::radians(angles.x));
    direction.y = sin(glm::radians(angles.y));
    direction.z = cos(glm::radians(angles.y)) * sin(glm::radians(angles.x));

    direction = glm::normalize(direction);

    forward = position + direction * 2.0f;

    updateVectors();
}


void Camera::MoveWorld(glm::vec3 movement)
{
    position += movement;

    updateVectors();
}

void Camera::SetPosition(glm::vec3 _position)
{
    position = _position;
    forward = _position + direction * 2.0f;

    updateVectors();
}