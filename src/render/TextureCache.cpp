#include "render/TextureCache.h"

namespace space {

std::shared_ptr<Texture> TextureCache::get(const std::filesystem::path& path, bool srgb)
{
    const auto key = std::make_pair(std::filesystem::weakly_canonical(path), srgb);
    if (auto it = m_textures.find(key); it != m_textures.end())
        return it->second;
    auto texture = std::make_shared<Texture>(key.first, srgb);
    m_textures.emplace(key, texture);
    return texture;
}

std::shared_ptr<Texture> TextureCache::white()
{
    if (!m_white)
        m_white = std::make_shared<Texture>(glm::u8vec4(255));
    return m_white;
}

void TextureCache::prune()
{
    std::erase_if(m_textures, [](const auto& entry) { return entry.second.use_count() == 1; });
}

} // namespace space
