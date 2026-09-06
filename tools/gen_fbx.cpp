// Genesis Render - build-time asset tool: export a cube with normals and UVs
// as an ASCII FBX file via Assimp. This exercises (and verifies) the engine's
// FBX import path without requiring a proprietary .fbx download.
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <cstdio>

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "genesis_cube.fbx";
    aiScene scene;
    aiMesh *m = new aiMesh;
    m->mMaterialIndex = 0;
    m->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
    m->mNumVertices = 24;
    m->mVertices = new aiVector3D[24];
    m->mNormals = new aiVector3D[24];
    m->mTextureCoords[0] = new aiVector3D[24];
    m->mNumUVComponents[0] = 2;
    const aiVector3D n[6] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0},
                             {1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    const aiVector3D quad[6][4] = {
        {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1}},
        {{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}},
        {{-1,-1,-1},{-1,1,-1},{-1,1,1},{-1,-1,1}},
        {{1,-1,-1},{1,1,-1},{1,1,1},{1,-1,1}},
        {{-1,1,-1},{1,1,-1},{1,1,1},{-1,1,1}},
        {{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1}}};
    const aiVector3D uv[4] = {{0,0,0},{1,0,0},{1,1,0},{0,1,0}};
    for (int f = 0; f < 6; ++f)
        for (int k = 0; k < 4; ++k)
        {
            int i = f * 4 + k;
            m->mVertices[i] = quad[f][k] * 0.5f;
            m->mNormals[i] = n[f];
            m->mTextureCoords[0][i] = uv[k];
        }
    m->mNumFaces = 12;
    m->mFaces = new aiFace[12];
    static const int idx[36] = {0,2,1,0,3,2, 4,5,6,4,6,7, 8,9,10,8,10,11,
                                12,14,13,12,15,14, 16,17,18,16,18,19, 20,22,21,20,23,22};
    for (int i = 0; i < 12; ++i)
    {
        m->mFaces[i].mNumIndices = 3;
        m->mFaces[i].mIndices = new unsigned int[3];
        for (int k = 0; k < 3; ++k) m->mFaces[i].mIndices[k] = idx[i * 3 + k];
    }
    aiMaterial *mat = new aiMaterial;
    aiColor4D diff(0.2f, 0.6f, 0.9f, 1.0f);
    float metal = 1.0f, rough = 0.25f;
    mat->AddProperty(&diff, 1, AI_MATKEY_COLOR_DIFFUSE);
    mat->AddProperty(&metal, 1, AI_MATKEY_METALLIC_FACTOR);
    mat->AddProperty(&rough, 1, AI_MATKEY_ROUGHNESS_FACTOR);

    aiNode *root = new aiNode;
    root->mNumMeshes = 1;
    root->mMeshes = new unsigned int[1]{0};
    scene.mRootNode = root;
    scene.mMeshes = new aiMesh *[1]{m};
    scene.mNumMeshes = 1;
    scene.mMaterials = new aiMaterial *[1]{mat};
    scene.mNumMaterials = 1;
    scene.mFlags = 0;

    Assimp::Exporter exp;
    aiReturn r = exp.Export(&scene, "fbxa", out);
    printf("export %s -> %d\n", out, (int)r);
    return r == aiReturn_SUCCESS ? 0 : 1;
}
