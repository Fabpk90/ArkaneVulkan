#pragma once
#include <memory>
#include <vector>

#include "ISystem.h"

class SystemManager
{
public:
	SystemManager() { instance = this; };


	void AddSystem(ISystem* newSystem)
	{
		m_systems.emplace_back(newSystem);

		newSystem->Init();
	}

	void Start() const
	{
		for (const auto & system : m_systems)
		{
			system->Start();
		}
	}

	void Update() const
	{
		for (const auto & system : m_systems)
		{
			system->Update();
		}
	}

	[[nodiscard]] bool ShouldContinue() const { return m_continueLooping; }
	void SetContinueLooping(bool _continue) { m_continueLooping = _continue; }

	static SystemManager* instance;

private:
	std::vector<std::unique_ptr<ISystem>> m_systems;
	bool m_continueLooping = true;
};

