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
            else if (key == "thrust")   entry.thrust   = toFloat(value, 0.0f);
            else if (key == "steering") entry.steering = toFloat(value, 0.0f);
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

int PrefabCatalog::buy(const PrefabCatalogEntry& entry) {
    if (!m_hooks.credits || !m_hooks.spend || !m_hooks.giveToPlayer) {
        m_message = "the shop is not connected to anything";
        return -1;
    }

    const float have = m_hooks.credits();
    if (have < entry.price) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s costs %d CR -- you have %d",
                      entry.title.c_str(), static_cast<int>(entry.price),
                      static_cast<int>(have));
        m_message = buf;
        return -1;
    }

    const int slot = m_hooks.giveToPlayer(entry);
    if (slot < 0) {
        m_message = "your hotbar is full -- place or drop something first";
        return -1;
    }

    // Charged only once it is actually in your hand. There is no path where
    // the money leaves and nothing arrives.
    m_hooks.spend(entry.price);

    char buf[192];
    std::snprintf(buf, sizeof buf, "%s -> hotbar slot %d  (-%d CR). Right-click to place.",
                  entry.title.c_str(), (slot + 1) % 10, static_cast<int>(entry.price));
    m_message = buf;
    return slot;
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
    ImGui::TextDisabled("Bought items go to your hotbar. Right-click places them.");

    ImGui::Separator();

    if (m_entries.empty()) {
        ImGui::TextDisabled("Nothing for sale.");
        ImGui::TextWrapped("Prefabs are .lime files (or .glb + .meta sidecars) in "
                           "assets/models/prefabs that carry `meta prefab: 1`.");
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

        ImGui::BeginDisabled(!affordable);
        if (ImGui::Button("Buy")) buy(entry);
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
