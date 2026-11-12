// Offline schema/parser check using the engine's existing yaml-cpp library.
// Does not initialize Vulkan, instantiate Scene, or claim rendering validation.
#include <yaml-cpp/yaml.h>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

static void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "Usage: validate_yaml.exe <generated-scene-directory>");
        size_t checked = 0;
        for (const auto& entry : std::filesystem::directory_iterator(argv[1]))
        {
            if (entry.path().extension() != ".cscene") continue;
            const auto scene = YAML::LoadFile(entry.path().string());
            auto manifestPath = entry.path();
            manifestPath.replace_extension(".manifest.json");
            const auto manifest = YAML::LoadFile(manifestPath.string());
            Require(scene["Renderer"].as<std::string>() == "Forward", "Renderer must be Forward");
            std::set<uint64_t> ids;
            uint64_t meshes = 0, sky = 0, light = 0;
            for (const auto& entity : scene["Entities"])
            {
                Require(ids.insert(entity["Entity"].as<uint64_t>()).second, "Duplicate entity UUID");
                Require(entity["RelationshipComponent"]["Parent"].as<uint64_t>() == 0, "Unexpected parent");
                for (const char* field : {"Translation", "Rotation", "Scale"})
                {
                    const auto values = entity["TransformComponent"][field];
                    Require(values.IsSequence() && values.size() == 3, "Invalid transform vector");
                    for (const auto& value : values) Require(std::isfinite(value.as<float>()), "Non-finite transform");
                }
                if (const auto mesh = entity["MeshComponent"])
                {
                    Require(mesh["AssetHandle"].as<uint64_t>() != 0, "Missing mesh handle");
                    Require(mesh["SubmeshIndex"].as<int>() == -1, "Unexpected submesh filter");
                    Require(mesh["MaterialTable"]["0"].as<uint64_t>() != 0, "Missing material");
                    ++meshes;
                }
                if (const auto sl = entity["SkyLightComponent"])
                {
                    Require(sl["AssetHandle"].as<uint64_t>() != 0 && sl["Intensity"].as<float>() > 0, "Invalid sky");
                    sl["SampleLOD"].as<uint32_t>();
                    ++sky;
                }
                if (const auto dl = entity["DirectionalLightComponent"])
                {
                    Require(dl["Intensity"].as<float>() > 0, "Directional light must be enabled");
                    Require(dl["Radiance"].size() == 3, "Invalid light radiance");
                    ++light;
                }
            }
            Require(meshes == manifest["mesh_instances"].as<uint64_t>(), "Instance count mismatch");
            Require(sky == 1 && light == 1, "Expected exactly one sky and directional light");
            auto sidecarPath = entry.path();
            sidecarPath.replace_extension(".benchmark.yaml");
            const auto cameras = YAML::LoadFile(sidecarPath.string())["Cameras"];
            Require(cameras.size() == 3, "Expected three T2 cameras");
            std::set<std::string> names;
            for (const auto& camera : cameras)
            {
                Require(names.insert(camera["Name"].as<std::string>()).second, "Duplicate camera");
                Require(camera["Position"].size() == 3, "Invalid camera position");
                Require(std::abs(camera["Pitch"].as<float>()) < 89, "Invalid pitch");
                Require(camera["FOV"].as<float>() > 0 && camera["FOV"].as<float>() < 180, "Invalid FOV");
                Require(camera["Far"].as<float>() > camera["Near"].as<float>(), "Invalid camera planes");
                camera["Yaw"].as<float>();
            }
            std::cout << entry.path().filename().string() << ": yaml-cpp schema OK; " << meshes << " mesh instances; 3 cameras\n";
            ++checked;
        }
        Require(checked > 0, "No scene files found");
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
