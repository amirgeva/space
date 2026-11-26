#include "core/FlyCamera.h"
#include "core/OrbitCamera.h"
#include "core/Window.h"
#include "effects/EffectFactory.h"
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
#include <utility>
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
    std::printf("Usage: space [--seed N] [--type freighter|station|fighter] [--model file.obj]\n"
                "             [--screenshot out.png] [--yaw radians] [--pitch radians]\n"
                "             [--zoom factor] [--focus -1..1]\n\n"
                "Controls: left-drag orbit, wheel zoom, N new random ship, [ / ] previous / next seed,\n"
                "          T next ship type, Space toggle auto-rotate, Esc quit\n"
                "Flying:   Up / Down pitch, Left / Right roll, Q / A forward / backward,\n"
                "          left-drag look around, wheel move, R or Space back to the orbit view\n");
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

// Near and far planes that keep the whole model in view from eye, as tight as possible for depth precision.
std::pair<float, float> clipPlanes(const glm::vec3& eye, const glm::vec3& boundsMin, const glm::vec3& boundsMax)
{
    const float radius = glm::length(boundsMax - boundsMin) * 0.5f;
    const float farPlane = glm::length(eye - (boundsMin + boundsMax) * 0.5f) + radius * 1.5f;
    // Nothing is closer than the bounding box. The 0.3 leaves room for geometry in the corners of the view,
    // whose depth is less than its distance.
    const float toBox = glm::length(eye - glm::clamp(eye, boundsMin, boundsMax));
    return {std::max(farPlane * 1e-4f, toBox * 0.3f), farPlane};
}

constexpr float kPitchRate = 1.0f; // radians per second
constexpr float kRollRate = 1.5f;  // radians per second
// Flying speed in model diagonals per second: slow at first, for moving around inside a hangar, then
// doubling every second the key is held, up to a cruise speed for crossing the whole model.
constexpr float kFlySpeedMin = 0.02f;
constexpr float kFlySpeedMax = 0.4f;

} // namespace

int main(int argc, char** argv)
{
    try {
        Options opt = parseArgs(argc, argv);
        space::Window window(1280, 720, "Space");

        const auto assets = assetDir(argv[0]);
        space::Shader shader(assets / "shaders/basic.vert", assets / "shaders/basic.frag");
        space::TextureCache textures;
        space::EffectFactory effects(assets);
        space::OrbitCamera camera;
        camera.yaw = opt.yaw;
        camera.pitch = opt.pitch;
        space::FlyCamera fly;
        bool flying = false; // the fly camera is in use instead of the orbit camera

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
            for (const space::EffectDesc& effect : data.effects)
                model->attach(effects.create(effect));
            std::printf("%s: %zu triangles, %zu materials, built in %.1f ms\n", title.c_str(), data.triangleCount(),
                        data.materials.size(), ms);
            window.setTitle(title);

            const glm::vec3 size = model->boundsMax() - model->boundsMin();
            camera.target = (model->boundsMin() + model->boundsMax()) * 0.5f;
            camera.target.z += opt.focus * size.z * 0.5f;
            camera.distance = glm::length(size) * 0.62f / opt.zoom;
            flying = false;
        };
        load();

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glEnable(GL_MULTISAMPLE);
        glClearColor(0.008f, 0.01f, 0.02f, 1.0f);

        const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.6f, -0.8f, -0.45f));
        bool autoRotate = !opt.screenshot;
        float thrustTime = 0.0f; // how long Q / A has been held
        glm::vec2 lastCursor = window.cursorPos();
        double lastTime = glfwGetTime();

        while (!window.shouldClose()) {
            window.pollEvents();
            const double now = glfwGetTime();
            const float dt = std::min(float(now - lastTime), 0.1f); // no jump after a slow frame
            lastTime = now;

            if (window.isKeyDown(GLFW_KEY_ESCAPE))
                window.requestClose();
            if (window.wasKeyPressed(GLFW_KEY_SPACE)) {
                autoRotate = !autoRotate;
                flying = false;
            }
            if (window.wasKeyPressed(GLFW_KEY_R))
                flying = false;
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

            // Flight controls take over from the orbit camera, starting from the current view.
            const auto axis = [&](int positiveKey, int negativeKey) {
                return float(window.isKeyDown(positiveKey)) - float(window.isKeyDown(negativeKey));
            };
            const float pitchInput = axis(GLFW_KEY_UP, GLFW_KEY_DOWN);
            const float rollInput = axis(GLFW_KEY_RIGHT, GLFW_KEY_LEFT);
            const float thrustInput = axis(GLFW_KEY_Q, GLFW_KEY_A);
            if (!flying && (pitchInput != 0.0f || rollInput != 0.0f || thrustInput != 0.0f)) {
                fly.lookAt(camera.position(), camera.target, glm::vec3(0.0f, 1.0f, 0.0f));
                flying = true;
                autoRotate = false;
            }
            const float modelSize = glm::length(model->boundsMax() - model->boundsMin());
            thrustTime = thrustInput != 0.0f ? thrustTime + dt : 0.0f;
            if (flying) {
                const float speed = std::min(kFlySpeedMin * std::exp2(thrustTime), kFlySpeedMax);
                fly.pitch(pitchInput * kPitchRate * dt);
                fly.roll(rollInput * kRollRate * dt);
                fly.move(thrustInput * speed * modelSize * dt);
            }

            const glm::vec2 cursor = window.cursorPos();
            if (window.isMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT)) {
                const glm::vec2 d = cursor - lastCursor;
                if (flying) {
                    fly.yaw(d.x * 0.003f);
                    fly.pitch(d.y * 0.003f);
                } else {
                    camera.rotate(-d.x * 0.008f, d.y * 0.008f);
                }
                autoRotate = false;
            }
            lastCursor = cursor;
            if (flying)
                fly.move(window.scrollDelta() * modelSize * 0.03f);
            else
                camera.zoom(window.scrollDelta());
            if (autoRotate)
                camera.rotate(dt * 0.15f, 0.0f);

            const glm::vec3 eye = flying ? fly.position : camera.position();
            const auto [nearPlane, farPlane] = clipPlanes(eye, model->boundsMin(), model->boundsMax());
            const glm::mat4 projection = glm::perspective(glm::radians(50.0f), window.aspect(), nearPlane, farPlane);
            const glm::mat4 modelMatrix(1.0f);

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            shader.use();
            shader.set("uModel", modelMatrix);
            const glm::mat4 viewProj = projection * (flying ? fly.view() : camera.view());
            shader.set("uViewProj", viewProj);
            shader.set("uNormalMatrix", glm::mat3(glm::transpose(glm::inverse(modelMatrix))));
            shader.set("uLightDir", lightDir);
            shader.set("uCameraPos", eye);
            model->draw(shader);
            model->updateEffects(dt);
            model->drawEffects({viewProj, modelMatrix, eye});

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
