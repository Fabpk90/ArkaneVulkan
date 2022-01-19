#pragma once
#include <vector>
#include <assimp/scene.h>

#include "Texture2D.h"
#include "VerticesDeclarations.h"

struct SubMesh
{
	MeshVertexDecl vertices;
	std::vector<glm::u16> indices;
	std::vector<Texture2D> textures;

	SubMesh(std::vector<MeshVertexDecl::Decl>&& vertices, std::vector<glm::u16>&& indices, std::vector<Texture2D>&& textures)
		: vertices(std::move(vertices)), indices(std::move(indices)), textures(std::move(textures)) {};
};

class Mesh
{
public:
	Mesh(const char* _path);
	Mesh() = default;

	~Mesh()
	{
		for (const auto* subMesh : subMeshes)
		{
			delete subMesh;
		}
	}

	std::vector<SubMesh*>& GetSubMeshes() { return subMeshes; }

private:
	void RecursivelyLoadNode(const aiNode* const pNode, const aiScene* pScene);
	SubMesh* LoadMeshFrom(const aiMesh& mesh, const aiScene* scene);
	std::vector<Texture2D> LoadMaterialTexturesType(aiMaterial* pMaterial, aiTextureType type);

private:
	std::string path;
	std::string pathCleaned;

	std::vector<SubMesh*> subMeshes; // todo: change this to a non pointer type, cache friendliness please !
};

