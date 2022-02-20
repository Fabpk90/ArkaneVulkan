#include "SceneGraph.h"

#include "../Mesh.h"
#include "../Node.h"

template <class NodeType, typename ... Args>
NodeType* SceneGraph::AddNode(Args... args)
{
	auto* node = new NodeType(std::forward<Args>(args)...);
	m_nodes.emplace_back(node);

	return node;
}

SceneGraph::~SceneGraph()
{
	for (const Node* node : m_nodes)
	{
		delete node;
	}
}

void SceneGraph::Init()
{
	//AddNode<Mesh>("assets/meshdesc/mesh.json");
}

void SceneGraph::Start()
{
	for (const auto& node : m_nodes)
	{
		node->Start();
	}
}

void SceneGraph::Update()
{
	for (const auto& node : m_nodes)
	{
		node->Update();
		node->GUI();
	}
}
