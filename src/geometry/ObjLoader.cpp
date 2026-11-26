#include "geometry/ObjLoader.h"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace space {

namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Line tokenizing helpers
// ---------------------------------------------------------------------------

constexpr std::string_view kWhitespace = " \t\r\n";

std::string_view trim(std::string_view s)
{
    const auto begin = s.find_first_not_of(kWhitespace);
    if (begin == std::string_view::npos)
        return {};
    const auto end = s.find_last_not_of(kWhitespace);
    return s.substr(begin, end - begin + 1);
}

// Pops the next whitespace-separated token off the front of `s`.
std::string_view nextToken(std::string_view& s)
{
    s = trim(s);
    const auto end = s.find_first_of(kWhitespace);
    std::string_view token = s.substr(0, end);
    s = end == std::string_view::npos ? std::string_view{} : s.substr(end);
    return token;
}

float parseFloat(std::string_view token, const fs::path& file, int line)
{
    float value = 0.0f;
    auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (ec != std::errc{})
        throw std::runtime_error(file.string() + ":" + std::to_string(line) + ": bad number '" +
                                 std::string(token) + "'");
    return value;
}

glm::vec3 parseVec3(std::string_view rest, const fs::path& file, int line)
{
    glm::vec3 v;
    v.x = parseFloat(nextToken(rest), file, line);
    v.y = parseFloat(nextToken(rest), file, line);
    v.z = parseFloat(nextToken(rest), file, line);
    return v;
}

// Texture statements may carry options ("-bm 1 file.png"); the file name is last then.
// Without options, the whole remainder is the name, which allows spaces in paths.
fs::path parseMapPath(std::string_view rest)
{
    rest = trim(rest);
    if (!rest.empty() && rest.front() == '-') {
        const auto lastSpace = rest.find_last_of(kWhitespace);
        rest = rest.substr(lastSpace + 1);
    }
    std::string name(rest);
    for (char& c : name)
        if (c == '\\')
            c = '/';
    return fs::path(name);
}

// ---------------------------------------------------------------------------
// MTL
// ---------------------------------------------------------------------------

void loadMtl(const fs::path& path, std::vector<MaterialDesc>& materials)
{
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Cannot open material library: " + path.string());

    const fs::path dir = path.parent_path();
    MaterialDesc* current = nullptr;
    std::string lineBuf;
    int lineNo = 0;

    while (std::getline(file, lineBuf)) {
        ++lineNo;
        std::string_view rest = lineBuf;
        const std::string_view keyword = nextToken(rest);
        if (keyword.empty() || keyword.front() == '#')
            continue;

        if (keyword == "newmtl") {
            MaterialDesc& m = materials.emplace_back();
            m.name = std::string(trim(rest));
            current = &m;
            continue;
        }
        if (!current)
            continue;

        if (keyword == "Ka")
            current->ambient = parseVec3(rest, path, lineNo);
        else if (keyword == "Kd")
            current->diffuse = parseVec3(rest, path, lineNo);
        else if (keyword == "Ks")
            current->specular = parseVec3(rest, path, lineNo);
        else if (keyword == "Ke")
            current->emissive = parseVec3(rest, path, lineNo);
        else if (keyword == "Ns")
            current->shininess = parseFloat(nextToken(rest), path, lineNo);
        else if (keyword == "d")
            current->opacity = parseFloat(nextToken(rest), path, lineNo);
        else if (keyword == "Tr")
            current->opacity = 1.0f - parseFloat(nextToken(rest), path, lineNo);
        else if (keyword == "map_Kd")
            current->diffuseMap = (dir / parseMapPath(rest)).lexically_normal();
        else if (keyword == "map_Ke")
            current->emissiveMap = (dir / parseMapPath(rest)).lexically_normal();
        // Other statements (illum, Ni, map_Bump, ...) are ignored for now.
    }
}

// ---------------------------------------------------------------------------
// OBJ
// ---------------------------------------------------------------------------

// Zero-based indices into the position/texcoord/normal arrays; -1 = absent.
struct FaceIndex {
    int v = -1, vt = -1, vn = -1;
    bool operator==(const FaceIndex&) const = default;
};

struct FaceIndexHash {
    std::size_t operator()(const FaceIndex& k) const noexcept
    {
        std::size_t h = std::hash<int>{}(k.v);
        h = h * 31 + std::hash<int>{}(k.vt);
        h = h * 31 + std::hash<int>{}(k.vn);
        return h;
    }
};

// Accumulates the geometry of one submesh (all faces sharing a material).
struct SubMeshBuilder {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<bool> needsNormal;
    std::unordered_map<FaceIndex, std::uint32_t, FaceIndexHash> lookup;
};

// Resolves a 1-based (or negative, relative) OBJ index to a 0-based one.
int resolveIndex(std::string_view token, std::size_t count, const fs::path& file, int line)
{
    if (token.empty())
        return -1;
    int value = 0;
    auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
    const int resolved = value < 0 ? static_cast<int>(count) + value : value - 1;
    if (ec != std::errc{} || value == 0 || resolved < 0 || resolved >= static_cast<int>(count))
        throw std::runtime_error(file.string() + ":" + std::to_string(line) + ": bad index '" +
                                 std::string(token) + "'");
    return resolved;
}

void generateMissingNormals(SubMeshBuilder& b)
{
    for (std::size_t i = 0; i + 2 < b.indices.size(); i += 3) {
        const std::uint32_t tri[3] = {b.indices[i], b.indices[i + 1], b.indices[i + 2]};
        const glm::vec3& p0 = b.vertices[tri[0]].position;
        const glm::vec3& p1 = b.vertices[tri[1]].position;
        const glm::vec3& p2 = b.vertices[tri[2]].position;
        const glm::vec3 faceNormal = glm::cross(p1 - p0, p2 - p0); // area-weighted
        for (std::uint32_t idx : tri)
            if (b.needsNormal[idx])
                b.vertices[idx].normal += faceNormal;
    }
    for (std::size_t i = 0; i < b.vertices.size(); ++i) {
        if (!b.needsNormal[i])
            continue;
        glm::vec3& n = b.vertices[i].normal;
        n = glm::dot(n, n) > 0.0f ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

} // namespace

ModelData loadObj(const fs::path& path)
{
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Cannot open model: " + path.string());

    const fs::path dir = path.parent_path();

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texcoords;
    std::vector<glm::vec3> normals;

    std::vector<MaterialDesc> materials;

    // Builders keyed by material name, in first-use order.
    std::vector<std::string> builderOrder;
    std::unordered_map<std::string, SubMeshBuilder> builders;
    std::string currentMaterial = "default";

    std::string lineBuf;
    int lineNo = 0;
    std::vector<std::uint32_t> polygon;

    while (std::getline(file, lineBuf)) {
        ++lineNo;
        std::string_view rest = lineBuf;
        const std::string_view keyword = nextToken(rest);
        if (keyword.empty() || keyword.front() == '#')
            continue;

        if (keyword == "v") {
            positions.push_back(parseVec3(rest, path, lineNo));
        } else if (keyword == "vt") {
            glm::vec2 uv;
            uv.x = parseFloat(nextToken(rest), path, lineNo);
            const std::string_view v = nextToken(rest);
            uv.y = v.empty() ? 0.0f : parseFloat(v, path, lineNo);
            texcoords.push_back(uv);
        } else if (keyword == "vn") {
            normals.push_back(parseVec3(rest, path, lineNo));
        } else if (keyword == "f") {
            auto [it, inserted] = builders.try_emplace(currentMaterial);
            if (inserted)
                builderOrder.push_back(currentMaterial);
            SubMeshBuilder& b = it->second;

            polygon.clear();
            for (std::string_view corner = nextToken(rest); !corner.empty(); corner = nextToken(rest)) {
                // corner is "v", "v/vt", "v//vn" or "v/vt/vn"
                FaceIndex key;
                const auto s1 = corner.find('/');
                key.v = resolveIndex(corner.substr(0, s1), positions.size(), path, lineNo);
                if (s1 != std::string_view::npos) {
                    const std::string_view tail = corner.substr(s1 + 1);
                    const auto s2 = tail.find('/');
                    key.vt = resolveIndex(tail.substr(0, s2), texcoords.size(), path, lineNo);
                    if (s2 != std::string_view::npos)
                        key.vn = resolveIndex(tail.substr(s2 + 1), normals.size(), path, lineNo);
                }

                auto [found, isNew] = b.lookup.try_emplace(key, static_cast<std::uint32_t>(b.vertices.size()));
                if (isNew) {
                    Vertex vert;
                    vert.position = positions[key.v];
                    vert.normal = key.vn >= 0 ? normals[key.vn] : glm::vec3(0.0f);
                    vert.texcoord = key.vt >= 0 ? texcoords[key.vt] : glm::vec2(0.0f);
                    b.vertices.push_back(vert);
                    b.needsNormal.push_back(key.vn < 0);
                }
                polygon.push_back(found->second);
            }

            if (polygon.size() < 3)
                throw std::runtime_error(path.string() + ":" + std::to_string(lineNo) +
                                         ": face with fewer than 3 vertices");
            for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
                b.indices.insert(b.indices.end(), {polygon[0], polygon[i], polygon[i + 1]});
        } else if (keyword == "usemtl") {
            currentMaterial = std::string(trim(rest));
        } else if (keyword == "mtllib") {
            loadMtl(dir / parseMapPath(rest), materials);
        }
        // o, g, s, l, ... are ignored for now.
    }

    auto materialIndex = [&](const std::string& name) {
        for (std::size_t i = 0; i < materials.size(); ++i)
            if (materials[i].name == name)
                return i;
        materials.push_back(MaterialDesc{.name = name});
        return materials.size() - 1;
    };

    ModelData model;
    for (const std::string& name : builderOrder) {
        SubMeshBuilder& b = builders.at(name);
        if (b.indices.empty())
            continue;
        generateMissingNormals(b);
        const std::size_t matIndex = materialIndex(name);
        model.subMeshes.push_back({MeshData{std::move(b.vertices), std::move(b.indices)}, matIndex});
    }
    model.materials = std::move(materials);

    if (model.subMeshes.empty())
        throw std::runtime_error("Model has no faces: " + path.string());
    return model;
}

} // namespace space
