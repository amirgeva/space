#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace space {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texcoord;
};

// CPU-side Phong material, mirroring the common subset of Wavefront .mtl.
struct MaterialDesc {
    std::string name = "default";
    glm::vec3 ambient{1.0f};   // Ka
    glm::vec3 diffuse{0.8f};   // Kd
    glm::vec3 specular{0.5f};  // Ks
    glm::vec3 emissive{0.0f};  // Ke
    float shininess = 32.0f;   // Ns
    float opacity = 1.0f;      // d

    std::filesystem::path diffuseMap;  // map_Kd (empty = none)
    std::filesystem::path emissiveMap; // map_Ke (empty = none)
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

// CPU-side model: geometry grouped by material. Produced by the OBJ loader and the
// procedural generators; uploaded to the GPU by render::Model or saved by writeObj.
struct ModelData {
    struct SubMesh {
        MeshData mesh;
        std::size_t materialIndex = 0;
    };

    std::vector<MaterialDesc> materials;
    std::vector<SubMesh> subMeshes;

    std::size_t triangleCount() const
    {
        std::size_t n = 0;
        for (const SubMesh& s : subMeshes)
            n += s.mesh.indices.size() / 3;
        return n;
    }
};

} // namespace space
