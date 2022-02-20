#pragma once
#include <vector>
#include <assimp/scene.h>

#include "IndexBuffer.h"
#include "Node.h"
#include "Texture2D.h"
#include "VerticesDeclarations.h"
#include "Shader.h"

struct SubMesh
{
	MeshVertexDecl vertices;
	IndexBuffer indices;
	std::vector<Texture2D> textures;

	SubMesh(std::vector<MeshVertexDecl::Decl>&& _vertices, std::vector<glm::u16>&& _indices, std::vector<Texture2D>&& _textures)
		: vertices(std::move(_vertices)), indices(std::move(_indices)), textures(std::move(_textures)) {};
};

class Mesh : public Node
{
public:
	Mesh(const char* _path);

	void Start() override;
	void Update() override;
	void GUI() override;

	~Mesh() override
	= default;

	std::vector<std::unique_ptr<SubMesh>>& GetSubMeshes() { return subMeshes; }

private:
	void RecursivelyLoadNode(const aiNode* const pNode, const aiScene* pScene);
	SubMesh* LoadMeshFrom(const aiMesh& mesh, const aiScene* scene);
	std::vector<Texture2D> LoadMaterialTexturesType(aiMaterial* pMaterial, aiTextureType type);

	std::string path;
	std::string pathCleaned;

	std::vector<std::unique_ptr<SubMesh>> subMeshes; // todo: change this to a non pointer type, cache friendliness please !

	std::unique_ptr<Shader> m_shader;
};

