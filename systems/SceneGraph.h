#pragma once
#include <vector>

#include "ISystem.h"

//todo: add here flecs

class Node;

class SceneGraph : public ISystem
{
public:
	SceneGraph() = default;
	~SceneGraph() override;

	void Init() override;
	void Start() override;
	void Update() override;

	template<class NodeType, typename... Args >
	NodeType* AddNode(Args... args);

private:
	std::vector<Node*> m_nodes;
};



