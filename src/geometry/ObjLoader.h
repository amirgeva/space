#pragma once

#include "geometry/ModelData.h"

#include <filesystem>

namespace space {

// Loads a Wavefront .obj file (and the .mtl libraries it references) into CPU-side ModelData.
//
// Supported: v, vt, vn, f (v, v/vt, v//vn, v/vt/vn, negative indices, n-gons
// triangulated as fans), usemtl, mtllib. Faces are grouped into one submesh per
// material. Missing normals are generated as smooth per-vertex normals.
// MTL: newmtl, Ka, Kd, Ks, Ke, Ns, d/Tr, map_Kd, map_Ke. Texture paths are resolved
// relative to the .mtl; textures themselves are loaded when the model is uploaded.
ModelData loadObj(const std::filesystem::path& path);

} // namespace space
