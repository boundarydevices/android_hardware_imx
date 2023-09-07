/*
 *
 * Copyright © 2017 NXP
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "ModelLoader/ModelLoader.hpp"

#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <assimp/Importer.hpp>
#include <fstream>
#include <glm/glm.hpp>
#include <iostream>
#include <sstream>

const string ModelLoader::CONFIG_FILE = "../Content/model.cfg";

ModelLoader::ModelLoader() : isInitialized(false) {}

ModelLoader::~ModelLoader(void) {
    for (MaterialListIter iterator = materials.begin(); iterator != materials.end(); iterator++) {
        Material *mat = *iterator;
        if (mat) delete mat;
    }
    materials.clear();

    for (VBOListIter iterator = objects.begin(); iterator != objects.end(); iterator++) {
        VBO *object = *iterator;
        if (object) delete object;
    }
    materials.clear();
}

bool ModelLoader::Initialize() {
    bool result = true;

    string filepath = "/sdcard/ferrari.dae";

    cout << "Loading model: " << filepath << endl;
    Assimp::Importer importer;

    // Do not import line and point meshes
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE,
                                aiPrimitiveType_LINE | aiPrimitiveType_POINT);

    const aiScene *scene =
            importer.ReadFile(filepath,
                              aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights |
                                      // aiProcess_RemoveRedundantMaterials|
                                      aiProcess_PreTransformVertices | aiProcess_Triangulate |
                                      aiProcess_GenUVCoords | aiProcess_SortByPType |
                                      aiProcess_FindDegenerates | aiProcess_FindInvalidData |
                                      aiProcess_GenNormals);

    if (!scene) {
        string msg = "Cannot open file with scene!: " + string(importer.GetErrorString()) + "\n";
        cerr << msg << endl;
        return false;
    }

    // Load materials
    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        // materials
        aiMaterial *m = scene->mMaterials[i];

        // get material name
        aiString name;
        m->Get(AI_MATKEY_NAME, name);

        // get material properties
        aiColor3D ambient, diffuse, specular;
        float shininess = 0.0;

        aiReturn r;
        r = m->Get(AI_MATKEY_COLOR_AMBIENT, ambient);
        if (r != AI_SUCCESS) {
            cout << "Cannot load color ambient property" << endl;
            ;
        }
        r = m->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
        if (r != AI_SUCCESS) {
            cout << "Cannot load color diffuse property" << endl;
            ;
        }
        r = m->Get(AI_MATKEY_COLOR_SPECULAR, specular);
        if (r != AI_SUCCESS) {
            cout << "Cannot load color specular property" << endl;
            ;
        }
        r = m->Get(AI_MATKEY_SHININESS, shininess);
        if (r != AI_SUCCESS) {
            cout << "Cannot load shininess property" << endl;
            ;
        }

        Material *mat = new Material(glm::vec3(ambient.r, ambient.g, ambient.b),
                                     glm::vec3(diffuse.r, diffuse.g, diffuse.b),
                                     glm::vec3(specular.r, specular.g, specular.b),
                                     glm::max(shininess, 255.0f));

        materials.push_back(mat);

        // Loads up base textures for material
        // TODO: What to do when the count is greater than 1
        unsigned int numTex = m->GetTextureCount(aiTextureType_DIFFUSE);
        for (unsigned int j = 0; j < numTex; ++j) {
            // Textures
            string path;
            aiString pth;
            aiReturn texFound;

            // Get ambient textures
            texFound = m->GetTexture(aiTextureType_DIFFUSE, j, &pth);
            if (texFound == AI_FAILURE) break;

            path += pth.C_Str();

            // TODO: implement to method
            cout << "Texture: " << path << endl;
            // GLuint texId = 0; //Utils::CreateTextureFromImage( path );
            // mat->SetTexture(texId);
        }
    }

    cout << "\nLoading scene ( " << scene->mNumMeshes << " objects ):" << endl;
    cout << "-------------------------------------------------------------------------------"
         << endl;

    // Load meshes
    aiMesh *mesh;
    unsigned int polygons = 0;
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        mesh = scene->mMeshes[i];

        if (mesh->mNumFaces == 0) continue;

        polygons += mesh->mNumFaces;

        // Create object from mesh

        // count faces
        int indices = mesh->mNumFaces;

        // Allocate memory for our vertices and normals
        Vertex *vertices = new Vertex[indices * 3];
        Vertex *normals = new Vertex[indices * 3];
        Coord *texcoords = new Coord[indices * 3];

        unsigned int FinishedFaces = 0;

        // Loop through every face
        for (unsigned curr_face = 0; curr_face < mesh->mNumFaces; curr_face++) {
            aiFace *face = &mesh->mFaces[curr_face];

            // aiProcess_Triangulate is turned on, so looping through 3 vertices
            for (unsigned i = 0; i < 3; i++) {
                Vertex v;
                v[0] = mesh->mVertices[face->mIndices[i]].x;
                v[1] = mesh->mVertices[face->mIndices[i]].y;
                v[2] = mesh->mVertices[face->mIndices[i]].z;

                // vertices
                memcpy(&vertices[FinishedFaces * 3 + i], &v, sizeof(Vertex));

                // normals
                v[0] = mesh->mNormals[face->mIndices[i]].x;
                v[1] = mesh->mNormals[face->mIndices[i]].y;
                v[2] = mesh->mNormals[face->mIndices[i]].z;

                memcpy(&normals[FinishedFaces * 3 + i], &v, sizeof(Vertex));

                // texture coordinates (if present)
                if (mesh->HasTextureCoords(0)) {
                    Coord c;
                    c[0] = mesh->mTextureCoords[0][face->mIndices[i]].x;
                    c[1] = mesh->mTextureCoords[0][face->mIndices[i]].y;
                    memcpy(&texcoords[FinishedFaces * 3 + i], &c, sizeof(Coord));
                }
            }
            FinishedFaces++;
        }

        int count = indices * 3;
        objects.push_back(new VBO(vertices, normals, texcoords, count, mesh->mMaterialIndex));

        // Clean up our allocated memory
        delete[] vertices;
        delete[] normals;
        delete[] texcoords;

        cout << "Done(vertices: " << indices * 3 << ", faces: " << indices << ")\n";
    }
    cout << "Scene loaded: " << polygons << " polygons.\n\n";

    isInitialized = result;

    return result;
}

void ModelLoader::Draw(GLuint shader) {
    if (!isInitialized) return;

    GLuint ambientLoc = glGetUniformLocation(shader, "ambient");
    GLuint diffuseLoc = glGetUniformLocation(shader, "diffuse");
    GLuint specularLoc = glGetUniformLocation(shader, "specular");

    for (VBOListIter iterator = objects.begin(); iterator != objects.end(); iterator++) {
        VBO *vbo = *iterator;
        int matId = vbo->GetMatId();
        Material *mat = materials.at(matId);

        glm::vec3 ambient = mat->GetAmbient();
        glUniform3f(ambientLoc, ambient.x, ambient.y, ambient.z);
        glm::vec3 diffuse = mat->GetDiffuse();
        glUniform3f(diffuseLoc, diffuse.x, diffuse.y, diffuse.z);
        glm::vec3 specular = mat->GetSpecular();
        glUniform3f(specularLoc, specular.x, specular.y, specular.z);

        glBindVertexArray(vbo->GetVAO());
        glDrawArrays(GL_TRIANGLES, 0, vbo->GetCount());
        glBindVertexArray(0);
    }
}
