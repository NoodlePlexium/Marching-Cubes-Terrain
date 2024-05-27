#ifndef MARCHING_CUBES_GPU_H
#define MARCHING_CUBES_GPU_H

#include "vendor/glm/glm.hpp"
#include "tables.h"
#include "model.h"
#include "shader.h"
#include "error.h"
#include "FastNoiseLite.h"
#include <vector>
#include <GL/glew.h>
#include <unordered_map>
#include <iostream>
#include <functional>
#include <algorithm>
#include <string>
#include <tuple>
#include <chrono>



// Custom hash function for tuple<float, float, float>
namespace std {
    template <>
    struct hash<std::tuple<float, float, float>> {
        size_t operator()(const std::tuple<float, float, float>& t) const {
            // Combine hashes of individual float values
            size_t h1 = std::hash<float>{}(std::get<0>(t));
            size_t h2 = std::hash<float>{}(std::get<1>(t));
            size_t h3 = std::hash<float>{}(std::get<2>(t));
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
}



class TerrainGPU{
public:

	TerrainGPU()
	{
        // Load shaders from file
        std::string compShaderSource = LoadShader("./shaders/marching_cubes.compute");
        computeShaderProgram = CreateComputeShader(compShaderSource);

        // load once
        TriTableValues = LoadTriTableValues();

		// Allocate memory for volume data once
        VolumeData = std::vector<float>((width + 1) * (width + 1) * (height + 1));
		InitNoiseGenerator();
	}


	Model ConstructMeshGPU(int x, int y, int z)
	{
		offsetX = x;
		offsetY = y;
		offsetZ = z;
		GenerateVolume(x, y, z);

        glUseProgram(computeShaderProgram);

		Model model;
        std::vector<float> Vertices;
        std::vector<unsigned int> Indices;

        VertexHashMap.clear();

		std::cout << "here" << std::endl;

        // shader uniforms
        BindUniformFloat1(computeShaderProgram, "densityThreshold", densityThreshold);
        BindUniformInt1(computeShaderProgram, "width", width);
        BindUniformInt1(computeShaderProgram, "height", height);
        PrintGLErrors();

        GLuint vbo1, vbo2, vbo3; 

        // Bind buffer for the first vector (VolumeData)
        glGenBuffers(1, &vbo1);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, vbo1);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * VolumeData.size(), VolumeData.data(), GL_STATIC_READ);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, vbo1);

        // Bind buffer Vertices
        glGenBuffers(1, &vbo2);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, vbo2);
        size_t vertexBufferSize = static_cast<size_t>((width + 1) * (width + 1) * (height + 1) * 48 * sizeof(float)); // for vertices
        glBufferData(GL_SHADER_STORAGE_BUFFER, vertexBufferSize, nullptr, GL_DYNAMIC_DRAW); // Allocate space without initializing data
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, vbo2);

        // Bind buffer for TriTable
        glGenBuffers(1, &vbo3);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, vbo3);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(int) * 4096, TriTableValues.data(), GL_STATIC_READ); 
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, vbo3);

        // Atomic flag
        GLuint atomicFlagBuffer;
        GLuint initialAtomicValue = 0; // Initial value for the atomic integer
        glGenBuffers(1, &atomicFlagBuffer);
        glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomicFlagBuffer);
        glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), &initialAtomicValue, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 3, atomicFlagBuffer);
        GLuint *ptr = (GLuint*)glMapBuffer(GL_ATOMIC_COUNTER_BUFFER, GL_READ_WRITE);
        *ptr = 0; // Initialize counter value
        glUnmapBuffer(GL_ATOMIC_COUNTER_BUFFER);
 
        // use compute shader
        glUseProgram(computeShaderProgram);
        glDispatchCompute(width, height, width);
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
        glFinish();


        // copy vertex data from GPU to CPU
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, vbo2); 
        GLfloat* verticesDataPtr = nullptr;
        verticesDataPtr = (GLfloat*)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);
        if (verticesDataPtr) {
            Vertices.assign(verticesDataPtr, verticesDataPtr + ((width + 1) * (width + 1) * (height + 1) * 48));
            glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
        }

        // free GPU memory
        glDeleteBuffers(1, &vbo1);
        glDeleteBuffers(1, &vbo2);
        glDeleteBuffers(1, &vbo3);

		auto start = std::chrono::high_resolution_clock::now();

		std::cout << "there" << std::endl;


        // set model vertices and indices
        int vCount = (width + 1) * (width + 1) * (height + 1) * 48;
        for (int i=0; i<vCount; i+=12)
        {
            if (Vertices[i] != 0)
            {
                // vertex 1
                if (GetVertexIndex(Vertices[i], Vertices[i+1], Vertices[i+2]) == -1)
                {
                    SetVertexIndex(Vertices[i], Vertices[i+1], Vertices[i+2], model.VertexCount());
                    model.indices.push_back(model.VertexCount());
                    model.vertices.push_back(Vertices[i]);
                    model.vertices.push_back(Vertices[i+1]);
                    model.vertices.push_back(Vertices[i+2]);
                    model.vertices.push_back(Vertices[i+9]);
                    model.vertices.push_back(Vertices[i+10]);
                    model.vertices.push_back(Vertices[i+11]);
                }
                else
                {
                    model.indices.push_back(GetVertexIndex(Vertices[i], Vertices[i+1], Vertices[i+2]));
                }

                // vertex 2
                if (GetVertexIndex(Vertices[i+3], Vertices[i+4], Vertices[i+5]) == -1)
                {
                    SetVertexIndex(Vertices[i+3], Vertices[i+4], Vertices[i+5], model.VertexCount());
                    model.indices.push_back(model.VertexCount());
                    model.vertices.push_back(Vertices[i+3]);
                    model.vertices.push_back(Vertices[i+4]);
                    model.vertices.push_back(Vertices[i+5]);
                    model.vertices.push_back(Vertices[i+9]);
                    model.vertices.push_back(Vertices[i+10]);
                    model.vertices.push_back(Vertices[i+11]);
                }
                else
                {
                    model.indices.push_back(GetVertexIndex(Vertices[i+3], Vertices[i+4], Vertices[i+5]));
                }

                // vertex 3
                if (GetVertexIndex(Vertices[i+6], Vertices[i+7], Vertices[i+8]) == -1)
                {
                    SetVertexIndex(Vertices[i+6], Vertices[i+7], Vertices[i+8], model.VertexCount());
                    model.indices.push_back(model.VertexCount());
                    model.vertices.push_back(Vertices[i+6]);
                    model.vertices.push_back(Vertices[i+7]);
                    model.vertices.push_back(Vertices[i+8]);
                    model.vertices.push_back(Vertices[i+9]);
                    model.vertices.push_back(Vertices[i+10]);
                    model.vertices.push_back(Vertices[i+11]);
                }
                else
                {
                    model.indices.push_back(GetVertexIndex(Vertices[i+6], Vertices[i+7], Vertices[i+8]));
                }
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(end - start);
        float meshGenTime = duration.count();

        std::cout << "mesh generation time: " <<  meshGenTime * 1000 << "ms" << std::endl;
        std::cout << " " << std::endl;
        std::cout << " " << std::endl;
        return model;
    }

    void AddDensity(glm::vec3 position, float amount)
	{
        int xI = int(floor(position.x + 0.5f));
        int yI = int(floor(position.y + 0.5f));
        int zI = int(floor(position.z + 0.5f));
        int index = zI + (width+1) * (yI + (height+1) * xI);
        VolumeData[index] += amount;

        if (VolumeData[index] < 0.0f) VolumeData[index] = 0.0f;
        else if (VolumeData[index] > 1.0f) VolumeData[index] = 1.0f;
	}


private:
	int width = 24;
	int height = 24;
    int offsetX = 0;
    int offsetY = 0;
    int offsetZ = 0;
    float densityThreshold = 0.6f;
    unsigned int computeShaderProgram;

    std::vector<int> TriTableValues;
    std::vector<float> VolumeData;
	std::unordered_map<std::tuple<float, float, float>, unsigned int> VertexHashMap;
	FastNoiseLite noiseGenerator3D;
	FastNoiseLite noiseGenerator2D;

    unsigned int GetVertexIndex(float x, float y, float z)
	{
		std::tuple<float, float, float> key(x, y, z);

		auto it = VertexHashMap.find(key);
		if (it != VertexHashMap.end()) 
		{
			return it->second;
		} 
		else 
		{
			return -1;
		}
	}

	void SetVertexIndex(float x, float y, float z, unsigned int value)
	{
		std::tuple<float, float, float> key(x, y,z);
		VertexHashMap[key] = value;
	}

	void GenerateVolume(int offsetX, int offsetY, int offsetZ)
    {
        int i;
        for (int y = 0; y < height + 1; ++y) {
            for (int x = 0; x < width + 1; ++x) {
                for (int z = 0; z < width + 1; ++z) {
                    float density = GetVolume3D(x + offsetX, y + offsetY, z + offsetZ);
                    VolumeData[i] = density;
                    i += 1;
                }
            }
        }
    }

	float GetVolume3D(float x, float y, float z)
	{
		int octaves = 6;
    	float amplitude = 1.0f;
		float prominance = 0.4f;
		float frequency = 4.0f;
		float totalNoise = 0.0f;
		for (int i = 0; i < octaves; ++i) 
		{
        	totalNoise += noiseGenerator3D.GetNoise(x * frequency, y * frequency, z * frequency) * amplitude;
        	frequency *= 2.0f;  // increase the frequency for each octave
        	amplitude *= prominance;  // decrease the amplitude for each octave
    	}
		return (totalNoise + 1) / 2.0f;
	}

	float GetNoise2D(float x, float y) 
	{
		int octaves = 3;
		float amplitude = 1.0f;
		float prominance = 0.4f;
		float frequency = 5.0f;
		float totalNoise = 0.0f;
		for (int i = 0; i < octaves; ++i) 
		{
	    	totalNoise += noiseGenerator3D.GetNoise(x * frequency, y * frequency) * amplitude;
	    	frequency *= 2.0f;  // Increase the frequency for each octave
	    	amplitude *= prominance;  // Decrease the amplitude for each octave
		}
		return (totalNoise + 1) / 2.0f;
	}

	void InitNoiseGenerator()
	{
		noiseGenerator3D.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
		noiseGenerator2D.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
		noiseGenerator3D.SetSeed(1);
		noiseGenerator2D.SetSeed(1);
	}

    glm::vec3 SnapToCube(float x, float y, float z)
	{	
		x = std::round(x);
		y = std::round(y);
		z = std::round(z);
		return glm::vec3(x, y, z);
	}

    std::vector<int> LoadTriTableValues()
    {
        std::vector<int> data;
        for (int y=0; y<256; ++y)
        {
            for (int x=0; x< 16; ++x)
            {
                data.push_back(TriTable[y][x]);
            }
        }
        return data;
    }
};





// class Terrain{
// public:

//     struct Chunk {
// 		int x;
// 		int y;
// 		int z;
// 	};


//     void UpdateTerrain (int renderDistance, float playerX, float playerY, float playerZ)
//     {
//         // CHUNK COORDINATES THAT BOUND THE PLAYER
//         int CenterChunkX = static_cast<int>(std::floor(playerX / width) * width);
//         int CenterChunkY = static_cast<int>(std::floor(playerY / height) * height);
// 	    int CenterChunkZ = static_cast<int>(std::floor(playerZ / width) * width);
// 	    int offsetH = (renderDistance - 1) * width / 2;
// 	    int offsetV = (renderDistance - 1) * height / 2;


//         // GENERATE 1 CHUNK PER FRAME
// 		bool generatedChunkThisFrame = false;

// 		// Chunk index marked for removal
// 		int markedChunkIndex = -1;

//         for (int y = 0; y < renderDistance; ++y) {
//             for (int x = 0; x < renderDistance; ++x) {
//                 for (int z = 0; z < renderDistance; ++z) {
//                     int worldX = x * width - offsetH + CenterChunkX;
//                     int worldY = y * height - offsetV + CenterChunkY;
//                     int worldZ = z * width - offsetH + CenterChunkZ;

//                     // Stop if chunk was generated this frame
//                     if (generatedChunkThisFrame) break;

//                     bool chunkPresent = false;

//                     // Check if there is a chunk at the position
//                     for (int i = 0; i < chunks.size(); ++i) {

//                         // 1 - check if chunk exists at current iteration position
//                         //if (chunks[i].x == worldX && chunks[i].y == worldY && chunks[i].z == worldZ) {
//                         if (chunks[i].x == worldX && chunks[i].z == worldZ) {
//                             chunkPresent = true;
//                             break;
//                         }


//                         // 2 - mark chunks for removal
//                        // int chunkDist = std::max(std::max(std::abs(chunks[i].x - CenterChunkX), std::abs(chunks[i].z - CenterChunkZ)), std::abs(chunks[i].y - CenterChunkY));
//                         int chunkDist = std::max(std::abs(chunks[i].x - CenterChunkX), std::abs(chunks[i].z - CenterChunkZ));
//                         if (chunkDist > std::floor((renderDistance * width) / 2.0)){
//                             markedChunkIndex = i;
//                         } 
//                     }


//                     // No chunk present? Create a new one
//                     if (!chunkPresent){
// 						TerrainGPU newTerrain(worldX, 0, worldZ);
// 						Model newModel = newTerrain.ConstructMeshGPU();
//                         Chunk newChunk;
//                         newChunk.x = worldX;
//                         newChunk.y = 0;
//                         newChunk.z = worldZ;
//                         chunks.push_back(newChunk);
//                         generatedChunkThisFrame = true;
//                     }
//                 }
//             }
//         }

// 		// Remove 1 Chunk per frame
// 		if (markedChunkIndex != -1){
// 			chunks.erase(chunks.begin() + markedChunkIndex);
// 			models.erase(models.begin() + markedChunkIndex);
// 		}
//     }


//     std::vector<Model> models;

// private:
// 	int width = 32;
// 	int height = 32;
//     std::vector<Chunk> chunks;
// };


#endif