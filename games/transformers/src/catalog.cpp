#include "catalog.hpp"
#include <fstream>
#include <stdexcept>

namespace transformers {
std::string stringOr(const Json& value, const char* key, const std::string& fallback) {
    auto it = value.find(key);
    return it != value.end() && it->is_string() ? it->get<std::string>() : fallback;
}
namespace {
Json glbDocument(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    uint32_t header[5]{};
    f.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!f || header[0] != 0x46546c67 || header[1] != 2 || header[4] != 0x4e4f534a || header[3] > 64*1024*1024)
        throw std::runtime_error("Invalid GLB JSON: " + path.string());
    std::string text(header[3], '\0');
    f.read(text.data(), text.size());
    if (!f) throw std::runtime_error("Truncated GLB: " + path.string());
    return Json::parse(text);
}
}
int Piece::animation(const std::string& name) const {
    for (size_t i=0; i<model->animations.size(); ++i)
        if (model->animations[i].name == name) return int(i);
    return -1;
}
int Piece::node(const std::string& name) const {
    for (size_t i=0; i<model->nodes.size(); ++i)
        if (model->nodes[i].name == name) return int(i);
    return -1;
}
int Catalog::piece(const std::string& file) const {
    for (size_t i=0; i<pieces.size(); ++i) if (pieces[i].file == file) return int(i);
    return -1;
}
int Catalog::kind(const std::string& name) const {
    for (size_t i=0; i<pieces.size(); ++i) if (pieces[i].kind == name) return int(i);
    return -1;
}
int Catalog::clip(const std::string& name, int part) const {
    for (size_t i=0; i<clips().size(); ++i)
        if (clips()[i]["name"] == name && (part < 0 || clips()[i]["file"] == pieces.at(part).file)) return int(i);
    return -1;
}
Catalog::Catalog(const std::filesystem::path& sidecar) : folder(sidecar.parent_path()) {
    std::ifstream f(sidecar);
    if (!f) throw std::runtime_error("Missing Optimus export: " + sidecar.string());
    f >> data;
    if (data.at("format_version") != 1) throw std::runtime_error("Unsupported Optimus sidecar version");
    const auto supplemental=folder/(sidecar.stem().string()+"_supplemental.json");
    if(std::filesystem::exists(supplemental)) {
        std::ifstream extraFile(supplemental);Json extra;extraFile>>extra;
        if(extra.at("format_version")!=1)throw std::runtime_error("Unsupported supplemental version");
        for(const char* key:{"files","clips","render_sets","materials"})
            if(extra.contains(key))for(const auto& item:extra[key])data[key].push_back(item);
        data["supplemental"]=std::move(extra);
    }
    for (const auto& entry : data.at("files")) {
        Piece p; p.file=entry.at("file"); p.kind=entry.at("kind");
        const auto path = folder / p.file;
        const auto glb = glbDocument(path);
        p.model = one::Model::load(path);
        if (glb.at("nodes").size() != p.model->nodes.size()) throw std::runtime_error("GLB node indices changed");
        for (const auto& n : glb.at("nodes")) {
            const auto extra = n.value("extras", Json::object());
            p.meshNames.push_back(extra.value("model", std::string{}));
            const bool visible = !extra.value("render_set_hidden", false);
            p.normalVisible.push_back(visible);
            p.specialVisible.push_back(visible);
        }
        for (const auto& sets : data.at("render_sets")) if (sets["file"] == p.file)
            for (const auto& group : sets.at("groups"))
                for (const auto& set : group.at("sets"))
                    for (const auto& mesh : set.at("meshes"))
                        for (size_t n=0; n<p.meshNames.size(); ++n) if (p.meshNames[n] == mesh.get<std::string>()) {
                            p.normalVisible[n] = set["name"] == group["default"];
                            p.specialVisible[n] = set["name"] == "SpecialAttackMesh";
                        }
        pieces.push_back(std::move(p));
    }
    for (const auto& c : clips()) {
        const int p=piece(c.at("file"));
        if (p<0 || (!c.at("animation").is_null() && pieces[p].animation(c.at("animation"))<0))
            throw std::runtime_error("Unresolved exported clip: " + c.at("name").get<std::string>());
    }
}
}
