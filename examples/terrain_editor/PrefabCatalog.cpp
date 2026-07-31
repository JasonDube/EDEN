#include "PrefabCatalog.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace {

// `meta <key>: <value>` -- the .lime line the shop is built on.
bool parseMetaLine(const std::string& line, std::string& key, std::string& value) {
    if (line.rfind("meta ", 0) != 0) return false;
    const std::size_t colon = line.find(':', 5);
    if (colon == std::string::npos) return false;
    key = line.substr(5, colon - 5);
    value = line.substr(colon + 1);
    // Trim both ends; a value can contain spaces (summaries do) so only the
    // edges come off.
    auto trim = [](std::string& s) {
        const std::size_t a = s.find_first_not_of(" \t\r\n");
        const std::size_t b = s.find_last_not_of(" \t\r\n");
        s = (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
    };
    trim(key);
    trim(value);
    return !key.empty();
}

float toFloat(const std::string& s, float fallback) {
    try { return std::stof(s); } catch (...) { return fallback; }
}

// Ray against a box. Returns the near hit distance, or -1 for a miss.
//
// Written here rather than borrowed because the editor does not have one -- it
// picks with bounds tests against the whole object, and what this needs is the
// distance along the aim ray so the NEAREST deck wins when two overlap.
float rayBoxDistance(const glm::vec3& origin, const glm::vec3& dir,
                     const glm::vec3& lo, const glm::vec3& hi) {
    float tNear = -1e30f, tFar = 1e30f;
    for (int axis = 0; axis < 3; ++axis) {
        const float o = origin[axis], d = dir[axis];
        if (std::fabs(d) < 1e-8f) {
            if (o < lo[axis] || o > hi[axis]) return -1.0f;   // parallel and outside
            continue;
        }
        float t1 = (lo[axis] - o) / d;
        float t2 = (hi[axis] - o) / d;
        if (t1 > t2) std::swap(t1, t2);
        tNear = std::max(tNear, t1);
        tFar  = std::min(tFar, t2);
        if (tNear > tFar) return -1.0f;
    }
    if (tFar < 0.0f) return -1.0f;          // box is behind the eye
    return tNear >= 0.0f ? tNear : 0.0f;    // 0 = already inside it
}

} // namespace

void PrefabCatalog::load(const std::string& dir) {
    namespace fs = std::filesystem;
    m_dir = dir;
    m_entries.clear();

    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        m_message = "no prefab folder at " + dir;
        return;
    }

    for (const auto& item : fs::directory_iterator(dir, ec)) {
        if (!item.is_regular_file(ec)) continue;
        const std::string ext = item.path().extension().string();
        if (ext != ".lime" && ext != ".glb") continue;

        // A .lime carries its own `meta` lines. A .glb cannot -- it is somebody
        // else's binary format -- so its metadata rides in a SIDECAR next to it:
        // helm_model.glb + helm_model.meta, the sidecar holding the same
        // `meta key: value` lines a .lime would. No sidecar, not for sale.
        std::ifstream file(ext == ".lime"
            ? item.path()
            : item.path().parent_path() / (item.path().stem().string() + ".meta"));
        if (!file) continue;

        PrefabCatalogEntry entry;
        // Absolute, because importModel treats a relative path as a name under
        // its own models/ folder and quietly misses -- the self-test caught a
        // .glb prefab failing to place for exactly that reason.
        entry.filePath = fs::absolute(item.path()).string();
        bool declaresPrefab = false;

        std::string line, key, value;
        while (std::getline(file, line)) {
            if (!parseMetaLine(line, key, value)) continue;
            if      (key == "prefab")  declaresPrefab = (value != "0");
            else if (key == "title")   entry.title = value;
            else if (key == "role")    entry.role = value;
            else if (key == "catalog") entry.shelf = value;
            else if (key == "summary") entry.summary = value;
            else if (key == "mount")   entry.mountPort = value;
            else if (key == "station") entry.stationPort = value;
            else if (key == "price")   entry.price = toFloat(value, 0.0f);
            else if (key == "mass")    entry.mass = toFloat(value, 0.0f);
        }

        // A .lime in this folder that does not say it is a prefab is not one --
        // an artist's work-in-progress should not turn up in the shop.
        if (!declaresPrefab) continue;
        m_entries.push_back(std::move(entry));
    }

    std::sort(m_entries.begin(), m_entries.end(),
              [](const PrefabCatalogEntry& a, const PrefabCatalogEntry& b) {
                  if (a.shelf != b.shelf) return a.shelf < b.shelf;
                  return a.price < b.price;
              });

    char buf[128];
    std::snprintf(buf, sizeof buf, "%zu item%s for sale", m_entries.size(),
                  m_entries.size() == 1 ? "" : "s");
    m_message = buf;
}

bool PrefabCatalog::aimedDeckPoint(glm::vec3& outPoint, std::string& outDeck) const {
    if (!m_hooks.aimRay || !m_hooks.decks) return false;

    glm::vec3 origin(0.0f), dir(0.0f);
    m_hooks.aimRay(origin, dir);
    if (glm::length(dir) < 0.0001f) return false;
    dir = glm::normalize(dir);

    const std::vector<CatalogDeck> decks = m_hooks.decks();
    float best = 1e30f;
    bool  hit  = false;
    for (const CatalogDeck& deck : decks) {
        const float t = rayBoxDistance(origin, dir, deck.min, deck.max);
        if (t < 0.0f || t >= best) continue;
        best = t;
        outDeck = deck.name;
        const glm::vec3 where = origin + dir * t;
        // On TOP of the deck, wherever along it the player is pointing. Using
        // the hit point's own y would bury the helm in the side of the slab
        // when the player aims at its edge.
        outPoint = glm::vec3(where.x, deck.max.y, where.z);
        hit = true;
    }
    return hit;
}

std::string PrefabCatalog::buyAndPlace(const PrefabCatalogEntry& entry) {
    if (!m_hooks.place || !m_hooks.credits || !m_hooks.spend) {
        m_message = "the shop is not connected to anything";
        return {};
    }

    const float have = m_hooks.credits();
    if (have < entry.price) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s costs %d CR -- you have %d",
                      entry.title.c_str(), static_cast<int>(entry.price),
                      static_cast<int>(have));
        m_message = buf;
        return {};
    }

    glm::vec3 where(0.0f);
    std::string deck;
    if (!aimedDeckPoint(where, deck)) {
        m_message = "look at a deck you have built, then buy";
        return {};
    }

    // Face the buyer, so a helm bought while standing behind it is one you can
    // then walk up to and use.
    const float yaw = m_hooks.viewYawDegrees ? m_hooks.viewYawDegrees() + 180.0f : 0.0f;

    const std::string name = m_hooks.place(entry.filePath, where, yaw);
    if (name.empty()) {
        m_message = "could not place " + entry.title + " (is the file still there?)";
        return {};
    }

    // Charged only once it is actually standing on the deck. Deducting on the
    // click and refunding on failure is two states and a bug waiting to happen;
    // this way there is no path where the money leaves and nothing arrives.
    m_hooks.spend(entry.price);

    char buf[192];
    std::snprintf(buf, sizeof buf, "%s placed on %s  (-%d CR)",
                  entry.title.c_str(), deck.c_str(), static_cast<int>(entry.price));
    m_message = buf;
    return name;
}

void PrefabCatalog::render(bool& open) {
    if (!open) return;

    ImGui::SetNextWindowSize(ImVec2(360, 380), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60, 90), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Catalog", &open)) { ImGui::End(); return; }

    const float have = m_hooks.credits ? m_hooks.credits() : 0.0f;
    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "%d CR", static_cast<int>(have));
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan")) load(m_dir);

    // Say plainly whether a purchase can land right now, because "nothing
    // happened" is the worst possible answer to clicking Buy.
    glm::vec3 where(0.0f);
    std::string deck;
    const bool aimed = aimedDeckPoint(where, deck);
    if (aimed) {
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "aiming at %s", deck.c_str());
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "look at a deck to buy");
    }

    ImGui::Separator();

    if (m_entries.empty()) {
        ImGui::TextDisabled("Nothing for sale.");
        ImGui::TextWrapped("Prefabs are .lime files in assets/models/prefabs "
                           "that carry `meta prefab: 1`.");
    }

    std::string shelf;
    for (const PrefabCatalogEntry& entry : m_entries) {
        if (entry.shelf != shelf) {
            shelf = entry.shelf;
            ImGui::Spacing();
            ImGui::TextDisabled("%s", shelf.c_str());
            ImGui::Separator();
        }

        ImGui::PushID(entry.filePath.c_str());
        ImGui::Text("%s", entry.title.c_str());
        ImGui::SameLine();
        const bool affordable = have >= entry.price;
        ImGui::TextColored(affordable ? ImVec4(0.3f, 1.0f, 0.5f, 1.0f)
                                      : ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                           "%d CR", static_cast<int>(entry.price));
        if (!entry.summary.empty()) ImGui::TextWrapped("%s", entry.summary.c_str());

        ImGui::BeginDisabled(!affordable || !aimed);
        if (ImGui::Button("Buy & Place")) buyAndPlace(entry);
        ImGui::EndDisabled();

        ImGui::PopID();
        ImGui::Spacing();
    }

    if (!m_message.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_message.c_str());
    }

    ImGui::End();
}
