#include "core/OrbitCamera.h"
#include "core/Window.h"
#include "geometry/ObjLoader.h"
#include "procgen/ShipGenerator.h"
#include "render/Model.h"
#include "render/Shader.h"
#include "render/TextureCache.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct Options {
    std::optional<fs::path> modelPath; // view an .obj instead of generating a ship
    space::ShipType shipType = space::ShipType::LogisticsFreighter;
    std::uint64_t seed = 1;
    std::optional<fs::path> screenshot; // render one frame to this PNG and exit
    float yaw = 0.6f;
    float pitch = 0.25f;
    float zoom = 1.0f;
    float focus = 0.0f; // camera target along the ship: -1 stern .. +1 bow
};

void printUsage()
{
    std::printf("Usage: space [--seed N] [--type freighter|station] [--model file.obj]\n"
                "             [--screenshot out.png] [--yaw radians] [--pitch radians]\n"
                "             [--zoom factor] [--focus -1..1]\n\n"
                "Controls: left-drag orbit, wheel zoom, N new random ship, [ / ] previous / next seed,\n"
                "          T next ship type, Space toggle auto-rotate, Esc quit\n");
}

Options parseArgs(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto value = [&]() -> std::string_view {
            if (i + 1 >= argc)
                throw std::runtime_error(std::string("Missing value for ") + std::string(arg));
            return argv[++i];
        };
        if (arg == "--seed")
            o.seed = std::strtoull(std::string(value()).c_str(), nullptr, 10);
        else if (arg == "--type") {
            const auto type = space::shipTypeFromString(value());
            if (!type)
                throw std::runtime_error("Unknown ship type");
            o.shipType = *type;
        } else if (arg == "--model")
            o.modelPath = fs::path(value());
        else if (arg == "--screenshot")
            o.screenshot = fs::path(value());
        else if (arg == "--yaw")
            o.yaw = std::stof(std::string(value()));
        else if (arg == "--pitch")
            o.pitch = std::stof(std::string(value()));
        else if (arg == "--zoom")
            o.zoom = std::stof(std::string(value()));
        else if (arg == "--focus")
            o.focus = std::stof(std::string(value()));
        else if (arg == "--help" || arg == "-h") {
            printUsage();
            std::exit(0);
        } else
            throw std::runtime_error("Unknown argument: " + std::string(arg));
    }
    return o;
}

fs::path assetDir(const char* argv0)
{
    // Assets are copied next to the executable at build time.
    auto exeDir = fs::absolute(argv0).parent_path();
    if (fs::exists(exeDir / "assets"))
        return exeDir / "assets";
    return fs::current_path() / "assets";
}

void saveScreenshot(const fs::path& path, int width, int height)
{
    std::vector<unsigned char> pixels(std::size_t(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    stbi_flip_vertically_on_write(1);
    if (!stbi_write_png(path.string().c_str(), width, height, 3, pixels.data(), width * 3))
        throw std::runtime_error("Cannot write screenshot " + path.string());
    std::printf("Saved %s\n", path.string().c_str());
}

} // namespace

int main(int argc, char** argv)
{
    try {
        Options opt = parseArgs(argc, argv);
        space::Window window(1280, 720, "Space");

        const auto assets = assetDir(argv[0]);
        space::Shader shader(assets / "shaders/basic.vert", assets / "shaders/basic.frag");
        space::TextureCache textures;
        space::OrbitCamera camera;
        camera.yaw = opt.yaw;
        camera.pitch = opt.pitch;

        std::unique_ptr<space::Model> model;
        auto load = [&]() {
            const auto start = std::chrono::steady_clock::now();
            space::ModelData data;
            std::string title;
            if (opt.modelPath) {
                data = space::loadObj(*opt.modelPath);
                title = "Space - " + opt.modelPath->filename().string();
            } else {
                data = space::generateShip({opt.shipType, opt.seed, assets / "textures/ship"});
                title = "Space - " + std::string(space::toString(opt.shipType)) + " #" + std::to_string(opt.seed);
            }
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            model.reset();
            textures.prune();
            model = std::make_unique<space::Model>(data, textures);
            std::printf("%s: %zu triangles, %zu materials, built in %.1f ms\n", title.c_str(), data.triangleCount(),
                        data.materials.size(), ms);
            window.setTitle(title);

            const glm::vec3 size = model->boundsMax() - model->boundsMin();
            camera.target = (model->boundsMin() + model->boundsMax()) * 0.5f;
            camera.target.z += opt.focus * size.z * 0.5f;
            camera.distance = glm::length(size) * 0.62f / opt.zoom;
        };
        load();

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glEnable(GL_MULTISAMPLE);
        glClearColor(0.008f, 0.01f, 0.02f, 1.0f);

        const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.6f, -0.8f, -0.45f));
        bool autoRotate = !opt.screenshot;
        glm::vec2 lastCursor = window.cursorPos();
        double lastTime = glfwGetTime();

        while (!window.shouldClose()) {
            window.pollEvents();
            const double now = glfwGetTime();
            const float dt = float(now - lastTime);
            lastTime = now;

            if (window.isKeyDown(GLFW_KEY_ESCAPE))
                window.requestClose();
            if (window.wasKeyPressed(GLFW_KEY_SPACE))
                autoRotate = !autoRotate;
            if (!opt.modelPath) {
                bool regenerate = false;
                if (window.wasKeyPressed(GLFW_KEY_N)) {
                    opt.seed = (std::uint64_t(std::rand()) << 16) ^ std::uint64_t(std::rand()) ^
                               std::uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
                    opt.seed %= 1000000;
                    regenerate = true;
                }
                if (window.wasKeyPressed(GLFW_KEY_T)) {
                    const auto& types = space::kShipTypes;
                    const auto it = std::find(std::begin(types), std::end(types), opt.shipType);
                    opt.shipType = (it + 1 == std::end(types)) ? types[0] : *(it + 1);
                    regenerate = true;
                }
                if (window.wasKeyPressed(GLFW_KEY_RIGHT_BRACKET)) {
                    ++opt.seed;
                    regenerate = true;
                }
                if (window.wasKeyPressed(GLFW_KEY_LEFT_BRACKET) && opt.seed > 0) {
                    --opt.seed;
                    regenerate = true;
                }
                if (regenerate)
                    load();
            }

            const glm::vec2 cursor = window.cursorPos();
            if (window.isMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT)) {
                const glm::vec2 d = cursor - lastCursor;
                camera.rotate(-d.x * 0.008f, d.y * 0.008f);
                autoRotate = false;
            }
            lastCursor = cursor;
            camera.zoom(window.scrollDelta());
            if (autoRotate)
                camera.rotate(dt * 0.15f, 0.0f);

            const glm::mat4 projection = glm::perspective(glm::radians(50.0f), window.aspect(),
                                                          camera.distance * 0.01f, camera.distance * 10.0f);
            const glm::mat4 modelMatrix(1.0f);

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            shader.use();
            shader.set("uModel", modelMatrix);
            shader.set("uViewProj", projection * camera.view());
            shader.set("uNormalMatrix", glm::mat3(glm::transpose(glm::inverse(modelMatrix))));
            shader.set("uLightDir", lightDir);
            shader.set("uCameraPos", camera.position());
            model->draw(shader);

            if (opt.screenshot) {
                saveScreenshot(*opt.screenshot, window.width(), window.height());
                break;
            }
            window.swapBuffers();
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
