#pragma once

#include "render/Texture.h"

#include <filesystem>
#include <map>
#include <memory>

namespace space {

// Shares textures between models; each image file is loaded once.
class TextureCache {
public:
    std::shared_ptr<Texture> get(const std::filesystem::path& path, bool srgb = true);
    std::shared_ptr<Texture> white();

    // Drops textures no model references any more.
    void prune();

private:
    std::map<std::pair<std::filesystem::path, bool>, std::shared_ptr<Texture>> m_textures;
    std::shared_ptr<Texture> m_white;
};

} // namespace space
