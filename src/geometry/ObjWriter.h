#pragma once

#include "geometry/ModelData.h"

#include <filesystem>

namespace space {

struct ObjWriteOptions {
    // Copy referenced textures into a "textures" folder next to the .obj so the
    // export is self-contained (e.g. for Blender). Otherwise texture paths are
    // written relative to the output directory.
    bool copyTextures = true;
};

// Writes `model` as Wavefront .obj plus a .mtl next to it (same stem).
void writeObj(const ModelData& model, const std::filesystem::path& objPath, const ObjWriteOptions& options = {});

} // namespace space
