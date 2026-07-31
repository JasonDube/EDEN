#include "Shipwright.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

constexpr const char* kPlanFile = "ship_plan.txt";

struct ToolDef {
    char        c;
    const char* label;
    ImU32       fill;
};

// Colours chosen to read at a glance: structure in steels, rooms in role
// colours that match their in-game meaning (engine hot, bridge lit teal like
// the helm's screen).
const ToolDef kTools[] = {
    {'#', "Wall",     IM_COL32(150, 155, 165, 255)},
    {'.', "Floor",    IM_COL32( 70,  72,  80, 255)},
    {'D', "Door",     IM_COL32(220, 170,  60, 255)},
    {'B', "Bridge",   IM_COL32( 40, 160, 150, 255)},
    {'C', "Cargo",    IM_COL32(160, 110,  50, 255)},
    {'E', "Engine",   IM_COL32(200,  90,  40, 255)},
    {'R', "Robots",   IM_COL32(140,  90, 190, 255)},
    {'W', "Window",   IM_COL32(120, 180, 255, 255)},
    {'_', "Erase",    IM_COL32( 25,  26,  30, 255)},
};

ImU32 fillFor(char c) {
    for (const auto& t : kTools)
        if (t.c == c) return t.fill;
    return IM_COL32(25, 26, 30, 255);
}

bool isRoom(char c) { return c=='B' || c=='C' || c=='E' || c=='R'; }

// THE LIVE WEIGHING. Same arithmetic the yard and the helm use, run over the
// plan as it is drawn, so the designer watches her get heavier stroke by
// stroke. Keep in sync with make_ship_from_plan.py (CELL 2.0, FLOOR_T 0.4,
// WALL_H 3.0, walls full-cell thick) and VesselFlight.cpp (2 t per unit^3,
// stock engine thrust 2500, stock helm steering 900, turn clamp 8..80).
float planTonnage(const std::vector<char>& cells) {
    constexpr float kFloorCell = 2.0f * 0.4f * 2.0f * 2.0f;            // plate
    constexpr float kWallCell  = (2.0f * 3.0f * 2.0f + 2.0f * 0.4f * 2.0f) * 2.0f; // wall + frame
    float t = 0.0f;
    for (char c : cells) {
        if (c == '#' || c == 'W') t += kWallCell;
        else if (c == '.' || c == 'D' || isRoom(c)) t += kFloorCell;
    }
    return t;
}

} // namespace

Shipwright::Shipwright() : m_cells(kW * kH, '_') {}

void Shipwright::clear() {
    std::fill(m_cells.begin(), m_cells.end(), '_');
    m_status = "cleared";
}

void Shipwright::paint(int x, int y, char c) {
    if (x < 0 || x >= kW || y < 0 || y >= kH) return;
    m_cells[y * kW + x] = c;
    if (m_mirrorX) m_cells[y * kW + (kW - 1 - x)] = c;
}

std::string Shipwright::serialize() const {
    std::string out;
    out.reserve((kW + 1) * kH);
    for (int y = 0; y < kH; ++y) {
        out.append(&m_cells[y * kW], kW);
        out.push_back('\n');
    }
    return out;
}

bool Shipwright::deserialize(const std::string& text) {
    std::vector<char> next(kW * kH, '_');
    std::istringstream in(text);
    std::string line;
    int y = 0;
    while (std::getline(in, line) && y < kH) {
        if (line.empty()) continue;
        for (int x = 0; x < kW && x < static_cast<int>(line.size()); ++x) {
            const char c = line[x];
            next[y * kW + x] =
                (c=='#'||c=='.'||c=='D'||c=='W'||isRoom(c)) ? c : '_';
        }
        ++y;
    }
    if (y == 0) return false;
    m_cells = std::move(next);
    return true;
}

void Shipwright::render(bool& open) {
    if (!open) return;

    ImGui::SetNextWindowSize(ImVec2(760, 780), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(80, 40), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Shipwright -- deck plan", &open)) { ImGui::End(); return; }

    // ---- tools -------------------------------------------------------------
    for (const auto& t : kTools) {
        ImGui::PushID(t.label);
        const bool active = (m_tool == t.c);
        ImGui::PushStyleColor(ImGuiCol_Button, t.fill);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.fill);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.fill);
        if (ImGui::Button(active ? "##sel" : "##tool", ImVec2(22, 22))) m_tool = t.c;
        ImGui::PopStyleColor(3);
        if (active) {
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(a, b, IM_COL32(255, 255, 120, 255), 0, 0, 2.5f);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(t.label);
        ImGui::SameLine(0, 14);
        ImGui::PopID();
    }
    ImGui::NewLine();
    ImGui::Checkbox("Mirror (ships are symmetric)", &m_mirrorX);
    ImGui::SameLine(0, 20);
    ImGui::TextDisabled("LMB paint   RMB erase   top of grid = BOW");

    // The displacement line: hull weight as drawn, how many stock engines
    // she will demand, and how she will answer a stock helm.
    const float tons = planTonnage(m_cells);
    if (tons > 0.0f) {
        const int engines = static_cast<int>(std::ceil(tons / 2500.0f));
        const float turn = std::clamp(100.0f * 900.0f / tons, 8.0f, 80.0f);
        ImGui::Text("hull %.0f t", tons);
        ImGui::SameLine(0, 18);
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                           "needs %d stock engine%s", engines, engines == 1 ? "" : "s");
        ImGui::SameLine(0, 18);
        ImGui::TextDisabled("turns %.0f deg/s on a stock helm", turn);
    } else {
        ImGui::TextDisabled("hull 0 t -- draw, and watch her take on weight");
    }

    // ---- actions -----------------------------------------------------------
    if (ImGui::Button("Copy as text")) {
        ImGui::SetClipboardText(serialize().c_str());
        m_status = "plan copied to clipboard";
    }
    ImGui::SameLine();
    if (ImGui::Button("Print to console")) {
        // The prompt channel: this lands in editor_console.log where the
        // generator's author reads it. Deliberately NOT behind g_diagnostics --
        // it is an export the user asked for by pressing the button.
        std::printf("[Shipwright] plan %dx%d\n%s[Shipwright] end\n",
                    kW, kH, serialize().c_str());
        std::fflush(stdout);
        m_status = "plan printed to the console/log";
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::ofstream f(kPlanFile);
        f << serialize();
        m_status = std::string("saved to ") + kPlanFile;
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        std::ifstream f(kPlanFile);
        if (f) {
            std::stringstream ss; ss << f.rdbuf();
            m_status = deserialize(ss.str()) ? std::string("loaded ") + kPlanFile
                                             : "could not read a plan from the file";
        } else {
            m_status = std::string("no ") + kPlanFile + " next to the binary yet";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) clear();
    ImGui::SameLine(0, 24);
    if (m_buildShip) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(40, 120, 60, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(50, 150, 75, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(35, 100, 50, 255));
        const bool go = ImGui::Button("BUILD SHIP");
        ImGui::PopStyleColor(3);
        if (go) {
            const std::string result = m_buildShip(serialize());
            m_status = result.empty()
                ? "the yard refused the plan -- see the console for the generator's report"
                : result;
        }
    }
    if (m_openCatalog) {
        ImGui::SameLine();
        if (ImGui::Button("Catalog")) {
            m_openCatalog();
            m_status = "the chandlery is open -- buy parts, Tab out, right-click to place";
        }
    }

    // ---- the grid ----------------------------------------------------------
    const float cell = 13.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::InvisibleButton("plan_grid", ImVec2(kW * cell, kH * cell));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const int hx = static_cast<int>((mouse.x - origin.x) / cell);
    const int hy = static_cast<int>((mouse.y - origin.y) / cell);

    if (hovered && hx >= 0 && hx < kW && hy >= 0 && hy < kH) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))  paint(hx, hy, m_tool);
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) paint(hx, hy, '_');
    }

    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const char c = m_cells[y * kW + x];
            const ImVec2 a(origin.x + x * cell, origin.y + y * cell);
            const ImVec2 b(a.x + cell - 1.0f, a.y + cell - 1.0f);
            dl->AddRectFilled(a, b, fillFor(c));
            if (isRoom(c) || c == 'D' || c == 'W') {
                const char label[2] = {c, 0};
                dl->AddText(ImVec2(a.x + 3.0f, a.y), IM_COL32(0, 0, 0, 200), label);
            }
        }
    }
    // Centreline, so symmetry has a spine to hang from.
    dl->AddLine(ImVec2(origin.x + (kW / 2) * cell, origin.y),
                ImVec2(origin.x + (kW / 2) * cell, origin.y + kH * cell),
                IM_COL32(255, 255, 255, 28));
    // Hover cross.
    if (hovered && hx >= 0 && hx < kW && hy >= 0 && hy < kH) {
        const ImVec2 a(origin.x + hx * cell, origin.y + hy * cell);
        dl->AddRect(a, ImVec2(a.x + cell, a.y + cell), IM_COL32(255, 255, 120, 200));
        if (m_mirrorX) {
            const ImVec2 m(origin.x + (kW - 1 - hx) * cell, origin.y + hy * cell);
            dl->AddRect(m, ImVec2(m.x + cell, m.y + cell), IM_COL32(255, 255, 120, 90));
        }
    }

    if (!m_status.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_status.c_str());
    }
    ImGui::End();
}
