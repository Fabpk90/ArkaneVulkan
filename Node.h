#pragma once
#include <memory>
#include <vector>

class Node
{
public:
	Node() = default;
	virtual ~Node() = default;

	virtual void Start() = 0;
	virtual void Update() = 0;

	virtual void GUI() = 0;

protected:
	std::vector<std::unique_ptr<Node>> m_children;
};

