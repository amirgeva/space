// shipgen: generates a spacecraft and writes it as Wavefront .obj/.mtl.
//
//   shipgen [--type freighter|station] [--seed N] [--count K] [--out ship.obj] [--textures dir]
//
// With --count K > 1, seeds N..N+K-1 are written as <out-stem>_<seed>.obj.

#include "geometry/ObjWriter.h"
#include "procgen/ShipGenerator.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

fs::path defaultTextureDir(const char* argv0)
{
    for (const fs::path& base : {fs::absolute(argv0).parent_path(), fs::current_path()}) {
        const fs::path dir = base / "assets" / "textures" / "ship";
        if (fs::exists(dir))
            return dir;
    }
    return fs::current_path() / "assets" / "textures" / "ship";
}

} // namespace

int main(int argc, char** argv)
{
    try {
        space::ShipParams params;
        params.textureDir = defaultTextureDir(argv[0]);
        fs::path out = "ship.obj";
        int count = 1;

        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            auto value = [&]() -> std::string {
                if (i + 1 >= argc)
                    throw std::runtime_error("Missing value for " + std::string(arg));
                return argv[++i];
            };
            if (arg == "--seed")
                params.seed = std::strtoull(value().c_str(), nullptr, 10);
            else if (arg == "--count")
                count = std::max(1, std::atoi(value().c_str()));
            else if (arg == "--out")
                out = value();
            else if (arg == "--textures")
                params.textureDir = fs::absolute(value());
            else if (arg == "--type") {
                const auto type = space::shipTypeFromString(value());
                if (!type)
                    throw std::runtime_error("Unknown ship type");
                params.type = *type;
            } else {
                std::printf("Usage: shipgen [--type freighter|station] [--seed N] [--count K] [--out ship.obj] "
                            "[--textures dir]\n");
                return arg == "--help" || arg == "-h" ? 0 : 1;
            }
        }

        const std::uint64_t first = params.seed;
        for (int k = 0; k < count; ++k) {
            params.seed = first + std::uint64_t(k);
            fs::path path = out;
            if (count > 1)
                path.replace_filename(out.stem().string() + "_" + std::to_string(params.seed) + out.extension().string());
            if (path.has_parent_path())
                fs::create_directories(path.parent_path());
            const space::ModelData model = space::generateShip(params);
            space::writeObj(model, path);
            std::printf("%s: %s seed %llu, %zu triangles\n", path.string().c_str(),
                        std::string(space::toString(params.type)).c_str(), (unsigned long long)params.seed,
                        model.triangleCount());
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
