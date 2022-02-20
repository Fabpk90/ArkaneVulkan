#pragma once
#include "ISystem.h"

class SceneManager : public ISystem
{
public:
	SceneManager() = default;
	~SceneManager() override;

	void Init() override;
	void Start() override;
	void Update() override;

private:

};

