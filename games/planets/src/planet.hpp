#pragma once
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <unordered_map>

namespace planets {
enum class Material : uint8_t { Air, Soil, Sediment, Basalt, Granite, IronOre, CopperOre, Mantle, MoltenIron, SolidIron, Count };
constexpr size_t materialCount=static_cast<size_t>(Material::Count);
struct Properties { const char* name; glm::vec3 color; double density; bool molten; };
const Properties& properties(Material m);
struct Cell { int64_t x=0,y=0,z=0; bool operator==(const Cell&) const=default; };
struct CellHash { size_t operator()(const Cell& p) const; };
struct Hit { Cell cell, previous; glm::dvec3 position; double distance; Material material; };
struct Config { double radius=250000,cellSize=.5; uint32_t seed=74123; };
struct Field { double density; Material material; }; // Positive inside material.
class Planet {
    Config config_;
    std::unordered_map<Cell,Material,CellHash> edits_;
    std::array<uint64_t,materialCount> inventory_{};
    Field originalField(Cell p) const;
public:
    explicit Planet(Config config={});
    const Config& config() const { return config_; }
    const auto& edits() const { return edits_; }
    const auto& inventory() const { return inventory_; }
    Cell cellAt(glm::dvec3 p) const;
    glm::dvec3 center(Cell p) const;
    double height(glm::dvec3 direction) const;
    double temperature(glm::dvec3 p) const;
    Material original(Cell p) const;
    Field field(Cell p) const;
    Material sample(Cell p) const;
    Material sample(glm::dvec3 p) const { return sample(cellAt(p)); }
    uint64_t dig(glm::dvec3 center,double radius);
    uint64_t deposit(glm::dvec3 center,double radius,Material material);
    std::optional<Hit> ray(glm::dvec3 origin,glm::dvec3 direction,double maxDistance=80) const;
    void save(const std::filesystem::path& path) const;
    void load(const std::filesystem::path& path);
    void refine(double cellSize);
};
}
