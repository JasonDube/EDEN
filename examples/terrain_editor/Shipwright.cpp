#include "Shipwright.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

// Room colours for the survey overlay -- distinct, repeating past ten.
const ImU32 kRoomPalette[] = {
    IM_COL32( 45, 165, 155, 255), IM_COL32(205, 130,  50, 255),
    IM_COL32(150, 100, 200, 255), IM_COL32( 95, 170,  70, 255),
    IM_COL32(200, 175,  60, 255), IM_COL32(190,  80,  90, 255),
    IM_COL32( 80, 130, 205, 255), IM_COL32(200, 110, 170, 255),
    IM_COL32(130, 180, 180, 255), IM_COL32(160, 145, 100, 255),
};
constexpr int kRoomPaletteN = 10;

ImU32 fillFor(char c) {
    for (const auto& t : kTools)
        if (t.c == c) return t.fill;
    return IM_COL32(25, 26, 30, 255);
}

bool isRoom(char c) { return c=='B' || c=='C' || c=='E' || c=='R'; }

// THE HULL MATERIALS LADDER, tier 1 -> 6. KEEP IN SYNC with MATERIALS in
// make_ship_from_plan.py -- the yard is the authority; this copy prices the
// drafting table live. Density t/unit^3, price CR/unit^3, armor for the
// damage model to come. Most tiers are locked: visible on the shelf, not
// sold in this star system -- the ladder is content before it is mechanics.
struct HullMat {
    const char* label;
    float       density;
    float       priceU3;
    int         armor;
    bool        available;
};
const HullMat kHullMats[] = {
    {"Light Alloy",             1.4f,   2.0f,  1, true},
    {"Metallic Laminate",       2.0f,   5.0f,  2, true},
    {"Adv. Metallic Laminate",  2.2f,  14.0f,  4, false},
    {"Nanocomposite",           1.1f,  40.0f,  6, false},
    {"Diamondoid",              1.6f, 150.0f, 10, false},
    {"Exotic Armor Laminate",   5.0f, 600.0f, 25, false},
};

// THE LIVE WEIGHING. Same arithmetic the yard and the helm use, run over the
// plan as it is drawn, so the designer watches her get heavier stroke by
// stroke. Volume here, material applied by the caller. Keep in sync with
// make_ship_from_plan.py (CELL 2.0, FLOOR_T 0.4, WALL_H 3.0, walls full-cell
// thick) and VesselFlight.cpp (stock engine thrust 2500 / mass 400, stock
// helm steering 900 / mass 180, turn clamp 8..80).
float planVolume(const std::vector<char>& cells, const std::vector<float>& loft) {
    constexpr float kFloorCell = 2.0f * 0.4f * 2.0f;   // plate (also under walls)
    float v = 0.0f;
    for (size_t i = 0; i < cells.size(); ++i) {
        const char c = cells[i];
        const int y = static_cast<int>(i) / Shipwright::kW;
        if (c == '#' || c == 'W') v += 2.0f * loft[y] * 2.0f + kFloorCell;
        else if (c == '.' || c == 'D' || isRoom(c)) v += kFloorCell;
    }
    return v;
}

} // namespace

constexpr float kLoftDefault = 3.0f;   // classic WALL_H
constexpr float kLoftMin = 2.0f;       // low enough to duck through, no lower
constexpr float kLoftMax = 9.0f;       // three storeys of superstructure

Shipwright::Shipwright() : m_cells(kW * kH, '_'), m_loft(kH, kLoftDefault) {}

void Shipwright::clear() {
    std::fill(m_cells.begin(), m_cells.end(), '_');
    std::fill(m_loft.begin(), m_loft.end(), kLoftDefault);
    m_overlay.clear();
    m_overlayIdx.clear();
    m_status = "cleared";
}

void Shipwright::paint(int x, int y, char c) {
    if (x < 0 || x >= kW || y < 0 || y >= kH) return;
    m_cells[y * kW + x] = c;
    if (m_mirrorX) m_cells[y * kW + (kW - 1 - x)] = c;
    // An edit voids the survey -- the colours must never lie.
    m_overlay.clear();
    m_overlayIdx.clear();
}

std::string Shipwright::serialize() const {
    std::string out;
    out.reserve((kW + 1) * kH + 8 * kH);
    for (int y = 0; y < kH; ++y) {
        out.append(&m_cells[y * kW], kW);
        out.push_back('\n');
    }
    // The loft trailer travels only when it says something.
    bool lofted = false;
    for (float h : m_loft) if (std::fabs(h - kLoftDefault) > 0.01f) lofted = true;
    if (lofted) {
        out += "loft:";
        char num[16];
        for (float h : m_loft) {
            std::snprintf(num, sizeof num, " %.2f", h);
            out += num;
        }
        out.push_back('\n');
    }
    if (m_revolveOn) {
        char rev[48];
        std::snprintf(rev, sizeof rev, "revolve: %.2f%s%s\n", m_revolveScale,
                      m_revolveFill == 1 ? " glass" : "",
                      m_revolve360 ? " 360" : "");
        out += rev;
    }
    return out;
}

bool Shipwright::deserialize(const std::string& text) {
    std::vector<char> next(kW * kH, '_');
    std::istringstream in(text);
    std::string line;
    int y = 0;
    std::vector<float> loft(kH, kLoftDefault);
    float revolve = 0.0f;
    bool revGlass = false, rev360 = false;
    while (std::getline(in, line) && y < kH) {
        if (line.empty()) continue;
        if (line.rfind("loft:", 0) == 0) {
            std::istringstream lv(line.substr(5));
            for (int i = 0; i < kH && (lv >> loft[i]); ++i) {}
            continue;
        }
        if (line.rfind("revolve:", 0) == 0) {
            revolve = std::strtof(line.c_str() + 8, nullptr);
            revGlass = line.find(" glass") != std::string::npos;
            rev360   = line.find(" 360")   != std::string::npos;
            continue;
        }
        for (int x = 0; x < kW && x < static_cast<int>(line.size()); ++x) {
            const char c = line[x];
            next[y * kW + x] =
                (c=='#'||c=='.'||c=='D'||c=='W'||isRoom(c)) ? c : '_';
        }
        ++y;
    }
    if (y == 0) return false;
    m_cells = std::move(next);
    m_loft = std::move(loft);
    m_revolveOn = revolve > 0.0f;
    if (m_revolveOn) {
        m_revolveScale = std::clamp(revolve, 0.2f, 1.0f);
        m_revolveFill = revGlass ? 1 : 0;
        m_revolve360 = rev360;
    }
    m_overlay.clear();
    m_overlayIdx.clear();
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

    // The material shelf: pick what she is made of. Locked tiers stay on
    // display -- a ladder you can see is a reason to get rich.
    {
        const HullMat& cur = kHullMats[m_material - 1];
        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::BeginCombo("##hullmat", cur.label)) {
            for (int i = 0; i < 6; ++i) {
                const HullMat& m = kHullMats[i];
                char row[128];
                std::snprintf(row, sizeof row, "%-24s %.1f t/u3  %.0f CR/u3  armor %d%s",
                              m.label, m.density, m.priceU3, m.armor,
                              m.available ? "" : "   -- not sold in this system");
                if (!m.available) {
                    ImGui::TextDisabled("%s", row);
                } else if (ImGui::Selectable(row, m_material == i + 1)) {
                    m_material = i + 1;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("hull material");
    }

    // The displacement line: hull weight as drawn, the materials bill, how
    // many stock engines she will demand, and how she answers a stock helm.
    const float vol = planVolume(m_cells, m_loft);
    const HullMat& mat = kHullMats[m_material - 1];
    const float tons = vol * mat.density;
    if (tons > 0.0f) {
        // AS FITTED, not bare hull -- the misread that grounded a corvette:
        // the table said "1 engine" for the hull alone, the helm weighed hull
        // PLUS the 180 t helm PLUS the 400 t engine and refused. Each stock
        // engine lifts 2500 but carries 400 of itself, so the count solves
        // n*2500 >= hull + 180 + n*400.
        constexpr float kHelmMass = 180.0f, kEngineMass = 400.0f, kEngineThrust = 2500.0f;
        const int engines = static_cast<int>(std::ceil((tons + kHelmMass) / (kEngineThrust - kEngineMass)));
        const float fitted = tons + kHelmMass + engines * kEngineMass;
        const float turn = std::clamp(100.0f * 900.0f / fitted, 8.0f, 80.0f);
        ImGui::Text("hull %.0f t", tons);
        ImGui::SameLine(0, 18);
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f),
                           m_revolveOn ? "materials %.0f CR + shell" : "materials %.0f CR",
                           vol * mat.priceU3);
        ImGui::SameLine(0, 18);
        ImGui::Text("fitted ~%.0f t", fitted);
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
    if (m_finalize) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(45, 90, 140, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(55, 110, 170, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(40, 80, 125, 255));
        const bool survey = ImGui::Button("Finalize");
        ImGui::PopStyleColor(3);
        if (survey) {
            m_overlay.clear();
            m_overlayIdx.clear();
            if (m_finalize(serialize(), m_overlay) && !m_overlay.empty()) {
                m_overlayIdx.assign(kW * kH, -1);
                for (size_t i = 0; i < m_overlay.size(); ++i)
                    for (const auto& [cx, cy] : m_overlay[i].cells)
                        if (cx >= 0 && cx < kW && cy >= 0 && cy < kH)
                            m_overlayIdx[cy * kW + cx] = static_cast<int>(i);
                m_status = "the yard reads " + std::to_string(m_overlay.size()) +
                           " room(s) -- her plates will carry the names below";
            } else {
                m_status = "the yard found no rooms in this plan";
            }
        }
        ImGui::SameLine();
    }
    if (m_buildShip) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(40, 120, 60, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(50, 150, 75, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(35, 100, 50, 255));
        const bool go = ImGui::Button("BUILD SHIP");
        ImGui::PopStyleColor(3);
        if (go) {
            const std::string result = m_buildShip(serialize(), m_material);
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
    ImGui::SameLine();
    if (ImGui::Button(m_sideView ? "Side\nview\n[on]" : "Side\nview")) m_sideView = !m_sideView;
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
            const int ri = m_overlayIdx.empty() ? -1 : m_overlayIdx[y * kW + x];
            dl->AddRectFilled(a, b, ri >= 0 ? kRoomPalette[ri % kRoomPaletteN] : fillFor(c));
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

    // THE PROFILE: the ship from her port side, bow to the RIGHT. Its own
    // window -- the first version drew a strip below the grid, but the grid
    // already fills the drafting table to its bottom edge, so the profile
    // landed past the fold, clipped into invisibility (field report:
    // "nothing changes when i press side view"). A window cannot be painted
    // over or clipped. Today, with no height variation, she reads as a
    // straight line -- this is where superstructure, decks, and a real
    // silhouette will appear the day the plan learns height.
    if (m_sideView) {
        const float pxU = cell / 2.0f;           // pixels per world unit (CELL=2)
        const float floorPx = 0.4f * pxU;        // FLOOR_T
        const float stripH  = kLoftMax * pxU + floorPx + 20.0f;
        ImGui::SetNextWindowSize(ImVec2(kW * cell + 24.0f, stripH + 76.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(90, 850), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Profile -- port side, bow right", &m_sideView)) { ImGui::End(); }
        else {
        // THE LOFT: drag along the strip to sculpt the wall-top height per
        // station -- the silhouette the yard will build. RMB resets a
        // station; the keel never moves (ships land on their bellies).
        if (ImGui::Button("Flatten")) std::fill(m_loft.begin(), m_loft.end(), kLoftDefault);
        ImGui::SameLine();
        ImGui::TextDisabled("LMB drag = loft the line   RMB = level a station   keel stays flat");
        // THE REVOLVE: lathe the half-plan 180 degrees about the centreline,
        // keel flat. The magenta curve is the dome the yard will step out of
        // boxes; the slider squashes the circle into an ellipse.
        ImGui::Checkbox("Revolve", &m_revolveOn);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderFloat("##revscale", &m_revolveScale, 0.2f, 1.0f, "height x%.2f");
        ImGui::SameLine();
        ImGui::TextDisabled("lathe the half-plan over the keel");
        if (m_revolveOn) {
            ImGui::TextDisabled("fill:");
            ImGui::SameLine();
            ImGui::RadioButton("opaque", &m_revolveFill, 0);
            ImGui::SameLine();
            ImGui::RadioButton("glass", &m_revolveFill, 1);
            ImGui::SameLine(0, 24);
            ImGui::Checkbox("360", &m_revolve360);
            ImGui::SameLine();
            ImGui::TextDisabled("full revolve -- a space hull; she will never land");
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 po = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("loft_strip", ImVec2(kW * cell, stripH));
        const float keelY = po.y + stripH - 4.0f;

        if (ImGui::IsItemHovered()) {
            const ImVec2 mp = ImGui::GetIO().MousePos;
            const int station = kH - 1 - static_cast<int>((mp.x - po.x) / cell);
            if (station >= 0 && station < kH) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    float h = (keelY - floorPx - mp.y) / pxU;
                    h = std::clamp(std::round(h * 4.0f) / 4.0f, kLoftMin, kLoftMax);
                    m_loft[station] = h;
                }
                if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
                    m_loft[station] = kLoftDefault;
            }
        }

        bool any = false;
        for (int y = 0; y < kH; ++y) {
            bool hasFloor = false, hasWall = false, hasWin = false;
            for (int x = 0; x < kW; ++x) {
                const char c = m_cells[y * kW + x];
                if (c == '#') hasWall = true;
                else if (c == 'W') { hasWall = true; hasWin = true; }
                else if (c == '.' || c == 'D' || isRoom(c)) hasFloor = true;
            }
            if (!hasFloor && !hasWall) continue;
            any = true;
            const float sx = po.x + (kH - 1 - y) * cell;   // bow (y=0) at the right
            dl->AddRectFilled(ImVec2(sx, keelY - floorPx), ImVec2(sx + cell, keelY),
                              IM_COL32(70, 72, 80, 255));
            if (hasWall) {
                const float wallPx = m_loft[y] * pxU;
                const float top = keelY - floorPx - wallPx;
                dl->AddRectFilled(ImVec2(sx, top), ImVec2(sx + cell, keelY - floorPx),
                                  IM_COL32(150, 155, 165, 255));
                if (hasWin)
                    dl->AddRectFilled(ImVec2(sx, top + wallPx * 0.25f),
                                      ImVec2(sx + cell, top + wallPx * 0.60f),
                                      IM_COL32(120, 180, 255, 255));
            }
        }
        // The loft line itself, across every station -- sculptable before
        // the walls exist, honoured when they do.
        for (int y = 0; y + 1 < kH; ++y) {
            const float x0 = po.x + (kH - 1 - y) * cell + cell * 0.5f;
            const float x1 = po.x + (kH - 2 - y) * cell + cell * 0.5f;
            dl->AddLine(ImVec2(x0, keelY - floorPx - m_loft[y] * pxU),
                        ImVec2(x1, keelY - floorPx - m_loft[y + 1] * pxU),
                        IM_COL32(255, 220, 90, 170), 1.5f);
        }
        if (m_revolveOn) {
            // Per station: radius = half-breadth + one cell, height = R * scale.
            float prevY = -1.0f, prevX = 0.0f;
            for (int y = 0; y < kH; ++y) {
                float b = 0.0f;
                for (int x = 0; x < kW; ++x) {
                    const char c = m_cells[y * kW + x];
                    if (c != '_') b = std::max(b, std::fabs(x + 0.5f - kW / 2.0f));
                }
                const float domeH = b > 0.0f ? (b * 2.0f + 2.0f) * m_revolveScale : 0.0f;
                const float sx = po.x + (kH - 1 - y) * cell + cell * 0.5f;
                const float sy = keelY - floorPx - std::min(domeH * pxU, stripH - 8.0f);
                if (domeH > 0.0f && prevY >= 0.0f)
                    dl->AddLine(ImVec2(prevX, prevY), ImVec2(sx, sy),
                                IM_COL32(230, 90, 200, 190), 1.5f);
                prevY = domeH > 0.0f ? sy : -1.0f;
                prevX = sx;
            }
        }
        dl->AddLine(ImVec2(po.x, keelY), ImVec2(po.x + kW * cell, keelY),
                    IM_COL32(255, 255, 255, 40));
        dl->AddText(ImVec2(po.x + kW * cell - 42.0f, po.y), IM_COL32(255, 255, 120, 200), "BOW>");
        if (!any)
            dl->AddText(ImVec2(po.x + 8.0f, po.y + 4.0f), IM_COL32(140, 140, 140, 255),
                        "profile -- draw a hull, then loft her line");
        ImGui::End();
        }
    }

    // The survey legend: swatch, plate name, size -- the ship's future
    // damage-model addresses, shown before a credit is spent.
    if (!m_overlay.empty()) {
        ImGui::Separator();
        for (size_t i = 0; i < m_overlay.size(); ++i) {
            const ImU32 col = kRoomPalette[i % kRoomPaletteN];
            ImGui::ColorButton(("##room" + std::to_string(i)).c_str(),
                               ImGui::ColorConvertU32ToFloat4(col),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
            ImGui::SameLine();
            ImGui::Text("%s", m_overlay[i].name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%d cells", static_cast<int>(m_overlay[i].cells.size()));
            if (i % 3 < 2 && i + 1 < m_overlay.size()) ImGui::SameLine(0, 24);
        }
    }

    if (!m_status.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_status.c_str());
    }
    ImGui::End();
}
