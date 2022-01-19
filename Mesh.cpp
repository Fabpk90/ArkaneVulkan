#include "Mesh.h"

#include <fstream>
#include <iostream>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

#include "Texture2D.h"

Mesh::Mesh(const char* _path) : path(_path)
{
    const auto index = path.find_last_of('/') + 1; // +1 to include the /
    pathCleaned = path.substr(0, index);

	const std::ifstream file(path);

    if (!file.bad())
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);

        subMeshes.reserve(scene->mNumMeshes);

        RecursivelyLoadNode(scene->mRootNode, scene);

        importer.FreeScene();
    }
}

void Mesh::RecursivelyLoadNode(const aiNode* const pNode, const aiScene* pScene)
{
    for (u32 i = 0; i < pNode->mNumMeshes; i++)
    {
        subMeshes.emplace_back(LoadMeshFrom(*pScene->mMeshes[pNode->mMeshes[i]], pScene));
    }

    for (u32 i = 0; i < pNode->mNumChildren; i++)
    {
        RecursivelyLoadNode(pNode->mChildren[i], pScene);
    }
}

SubMesh* Mesh::LoadMeshFrom(const aiMesh& mesh, const aiScene* scene)
{
    std::vector<MeshVertexDecl::Decl> vertices;
    std::vector<u16> indices;
    std::vector<Texture2D> textures;

    vertices.reserve(mesh.mNumVertices);
    indices.reserve(mesh.mNumFaces);


    for (u32 i = 0; i < mesh.mNumVertices; i++)
    {
        MeshVertexDecl::Decl v{};

        v.pos.x = mesh.mVertices[i].x;
        v.pos.y = mesh.mVertices[i].y;
        v.pos.z = mesh.mVertices[i].z;

        v.normal.x = mesh.mNormals[i].x;
        v.normal.y = mesh.mNormals[i].y;
        v.normal.z = mesh.mNormals[i].z;

        v.uv.x = mesh.mTextureCoords[0][i].x;
        v.uv.y = mesh.mTextureCoords[0][i].y;

        vertices.emplace_back(v);
    }

    for (u32 i = 0; i < mesh.mNumFaces; i++)
    {
        const auto& face = mesh.mFaces[i];

        for (u32 j = 0; j < face.mNumIndices; j++)
        {
            indices.emplace_back(face.mIndices[j]);
        }
    }

    //the mesh has some materials
    if (mesh.mMaterialIndex != 0)
    {
	    const auto mat = scene->mMaterials[mesh.mMaterialIndex];

        auto texAlbedo = LoadMaterialTexturesType(mat, aiTextureType_BASE_COLOR);

        aiString texture_file;
        mat->Get(AI_MATKEY_TEXTURE(aiTextureType_DIFFUSE, 0), texture_file);

        const auto tex = scene->GetEmbeddedTexture(texture_file.C_Str());
        if (tex)
        {
            std::cout << "oh yeah diffuse embedded" << std::endl;
        }

        if (!texAlbedo.empty())
            texAlbedo = LoadMaterialTexturesType(mat, aiTextureType_DIFFUSE);

        textures = texAlbedo;

        auto texSpecular = LoadMaterialTexturesType(mat, aiTextureType_SPECULAR);

        if (texSpecular.empty())
        {
            texSpecular = LoadMaterialTexturesType(mat, aiTextureType_HEIGHT);
        }

        auto texMetallic = LoadMaterialTexturesType(mat, aiTextureType_METALNESS);
        auto texRoughness = LoadMaterialTexturesType(mat, aiTextureType_DIFFUSE_ROUGHNESS);
        auto texAO = LoadMaterialTexturesType(mat, aiTextureType_AMBIENT_OCCLUSION);

        auto texNormal = LoadMaterialTexturesType(mat, aiTextureType_NORMALS);

        textures.insert(textures.end(), std::make_move_iterator(texSpecular.begin()), std::make_move_iterator(texSpecular.end()));
        textures.insert(textures.end(), std::make_move_iterator(texMetallic.begin()), std::make_move_iterator(texMetallic.end()));
        textures.insert(textures.end(), std::make_move_iterator(texRoughness.begin()), std::make_move_iterator(texRoughness.end()));
        textures.insert(textures.end(), std::make_move_iterator(texAO.begin()), std::make_move_iterator(texAO.end()));
        textures.insert(textures.end(), std::make_move_iterator(texNormal.begin()), std::make_move_iterator(texNormal.end()));
    }

    return new SubMesh(std::move(vertices), std::move(indices), std::move(textures));
}

std::vector<Texture2D> Mesh::LoadMaterialTexturesType(aiMaterial* pMaterial, aiTextureType type)
{
    std::vector<Texture2D> textures(pMaterial->GetTextureCount(type));

    for (u32 i = 0; i < textures.size(); ++i)
    {
        aiString path;
        pMaterial->GetTexture(type, i, &path);

        std::string texPath = pathCleaned;
        texPath += path.C_Str();

        textures[i].LoadFrom(texPath.c_str(), vk::ImageLayout::eGeneral);
    }

    return textures;
}
