#pragma once
#include <theone/assets/model.hpp>
#include <nlohmann/json.hpp>
#include <map>

namespace transformers {
using Json = nlohmann::json;
struct Piece {
    std::string file, kind;
    std::shared_ptr<one::Model> model;
    std::vector<std::string> meshNames;
    std::vector<bool> normalVisible, specialVisible;
    int animation(const std::string& name) const;
    int node(const std::string& name) const;
};
struct Catalog {
    std::filesystem::path folder;
    Json data;
    std::vector<Piece> pieces;
    explicit Catalog(const std::filesystem::path& sidecar);
    int piece(const std::string& file) const;
    int kind(const std::string& name) const;
    int clip(const std::string& name, int piece = -1) const;
    const Json& clips() const { return data.at("clips"); }
};
std::string stringOr(const Json& value, const char* key, const std::string& fallback = {});
}
