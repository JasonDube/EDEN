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
    {'~', "Fill",     IM_COL32( 95, 150,  95, 255)},   // bucket: flood an enclosed area with floor

    {'D', "Door",     IM_COL32(220, 170,  60, 255)},
    {'B', "Helm",     IM_COL32( 40, 160, 150, 255)},
    {'C', "Cargo",    IM_COL32(160, 110,  50, 255)},
    {'E', "Engine",   IM_COL32(200,  90,  40, 255)},
    {'R', "Robots",   IM_COL32(140,  90, 190, 255)},
    {'W', "Window",   IM_COL32(120, 180, 255, 255)},
    {'X', "Exhaust",  IM_COL32(225, 115,  45, 255)},
    {'P', "Reactor",  IM_COL32(238, 202,  58, 255)},
    {'F', "Radiator", IM_COL32(168, 180, 190, 255)},
    {'L', "Lift",     IM_COL32(170, 220,  90, 255)},   // elevator: serves every open floor in its column
    {'A', "Mast",     IM_COL32(235, 170,  60, 255)},
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

bool isRoom(char c) { return c=='B' || c=='C' || c=='E' || c=='R' || c=='P'; }

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
// SOCKET PRICING, keep in sync with SOCKET_PRICE in make_ship_from_plan.py.
// One socket per connected same-letter cluster ("letters make sockets").
float planSocketBill(const std::vector<char>& cells, int& nSockets) {
    constexpr int W = Shipwright::kW, H = Shipwright::kH;
    auto price = [](char c) -> float {
        switch (c) { case 'B': return 1500.0f; case 'E': return 2000.0f;
                     case 'R': return 3500.0f; case 'C': return 800.0f;
                     case 'P': return 2500.0f; }
        return 0.0f;
    };
    std::vector<char> seen(cells.size(), 0);
    float bill = 0.0f;
    nSockets = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const int i = y * W + x;
            const char c = cells[i];
            if (seen[i] || !isRoom(c)) continue;
            // flood this same-letter cluster
            std::vector<int> stack{i};
            while (!stack.empty()) {
                const int j = stack.back(); stack.pop_back();
                if (j < 0 || j >= W * H || seen[j] || cells[j] != c) continue;
                seen[j] = 1;
                const int jx = j % W, jy = j / W;
                if (jx > 0)     stack.push_back(j - 1);
                if (jx < W - 1) stack.push_back(j + 1);
                if (jy > 0)     stack.push_back(j - W);
                if (jy < H - 1) stack.push_back(j + W);
            }
            bill += price(c);
            ++nSockets;
        }
    }
    return bill;
}

// MAST PRICING, keep in sync with MAST_PRICE_BASE / MAST_PRICE_CELL in
// make_ship_from_plan.py. One mast per connected A cluster; extra cells make
// the array heavier, not more numerous.
float planMastBill(const std::vector<char>& topside, int& nMasts) {
    constexpr int W = Shipwright::kW, H = Shipwright::kH;
    std::vector<char> seen(topside.size(), 0);
    float bill = 0.0f;
    nMasts = 0;
    for (int i = 0; i < W * H; ++i) {
        if (topside[i] != 'A' || seen[i]) continue;
        int cells = 0;
        std::vector<int> stack{i};
        while (!stack.empty()) {
            const int j = stack.back(); stack.pop_back();
            if (j < 0 || j >= W * H || seen[j] || topside[j] != 'A') continue;
            seen[j] = 1;
            ++cells;
            const int jx = j % W, jy = j / W;
            if (jx > 0)     stack.push_back(j - 1);
            if (jx < W - 1) stack.push_back(j + 1);
            if (jy > 0)     stack.push_back(j - W);
            if (jy < H - 1) stack.push_back(j + W);
        }
        bill += 1200.0f + 400.0f * (cells - 1);
        ++nMasts;
    }
    return bill;
}

float planVolume(const std::vector<char>& cells, const std::vector<float>& loft) {
    constexpr float kFloorCell = 2.0f * 0.4f * 2.0f;   // plate (also under walls)
    float v = 0.0f;
    for (size_t i = 0; i < cells.size(); ++i) {
        const char c = cells[i];
        const int y = static_cast<int>(i) / Shipwright::kW;
        if (c == '#' || c == 'W' || c == 'X' || c == 'F') v += 2.0f * loft[y] * 2.0f + kFloorCell;
        else if (c == '.' || c == 'D' || c == 'L' || isRoom(c)) v += kFloorCell;
    }
    return v;
}

} // namespace

constexpr float kLoftDefault = 3.0f;   // classic WALL_H
constexpr float kLoftMin = 2.0f;       // low enough to duck through, no lower
constexpr float kLoftMax = 9.0f;       // three storeys of superstructure

Shipwright::Shipwright()
    : m_cells(kW * kH, '_'), m_loft(kH, kLoftDefault) {}

void Shipwright::clear() {
    std::fill(m_cells.begin(), m_cells.end(), '_');
    m_upper.clear();
    m_level = 0;
    std::fill(m_loft.begin(), m_loft.end(), kLoftDefault);
    m_overlay.clear();
    m_overlayIdx.clear();
    m_status = "cleared";
}

std::vector<char>& Shipwright::activeLayer() {
    return m_level == 0 ? m_cells : m_upper[m_level - 1];
}

bool Shipwright::supportedBelow(int i) const {
    // A storey cell bears weight only if the storey below has structure
    // there (a mast is not a floor).
    if (m_level <= 0) return true;
    const std::vector<char>& below = (m_level == 1) ? m_cells : m_upper[m_level - 2];
    return below[i] != '_' && below[i] != 'A';
}

void Shipwright::paint(int x, int y, char c) {
    if (x < 0 || x >= kW || y < 0 || y >= kH) return;
    // THE STROKE LANDS ON THE LEVEL YOU ARE STANDING ON. (The first storey
    // build shipped with this function still writing deck 1 directly -- a
    // patch that silently failed to apply -- so every "upstairs" wall fell
    // through the floor. The field prints below are its parole officer.)
    // Overhangs are legal (the roof below pours out to be this floor) --
    // the anchor law is enforced by the live red rings and the yard, not
    // the brush.
    auto& L = activeLayer();
    auto set = [&](int i) { L[i] = c; };
    set(y * kW + x);
    if (m_mirrorX) set(y * kW + (kW - 1 - x));
    // An edit voids the survey -- the colours must never lie.
    m_overlay.clear();
    m_overlayIdx.clear();
}

void Shipwright::fillFloor(int x, int y) {
    // THE BUCKET: flood the clicked empty region with floor tiles -- but
    // only if it is truly enclosed. A flood that reaches the grid edge has
    // found a hole in the hull; on a storey, spilling onto unsupported air
    // is falling off the roof. Either way it refuses and says so.
    if (x < 0 || x >= kW || y < 0 || y >= kH) return;
    auto& L = activeLayer();
    if (L[y * kW + x] != '_') {
        m_status = "the bucket wants an empty cell inside closed walls";
        return;
    }
    std::vector<char> seen(kW * kH, 0);
    std::vector<int> region, stack{y * kW + x};
    bool leaks = false;
    while (!stack.empty()) {
        const int j = stack.back(); stack.pop_back();
        if (j < 0 || j >= kW * kH || seen[j] || L[j] != '_') continue;
        seen[j] = 1;
        region.push_back(j);
        const int jx = j % kW, jy = j / kW;
        if (jx == 0 || jx == kW - 1 || jy == 0 || jy == kH - 1) leaks = true;
        if (jx > 0)      stack.push_back(j - 1);
        if (jx < kW - 1) stack.push_back(j + 1);
        if (jy > 0)      stack.push_back(j - kW);
        if (jy < kH - 1) stack.push_back(j + kW);
    }
    if (leaks) {
        m_status = "not enclosed -- the flood reached the grid edge; close her walls first";
        return;
    }
    for (const int j : region) L[j] = '.';
    m_status = std::to_string(region.size()) + " cells floored";
    // An edit voids the survey, same as any brush stroke.
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
        std::snprintf(rev, sizeof rev, "revolve: %.2f%s%s%s\n", m_revolveScale,
                      m_revolveFill == 1 ? " glass" : "",
                      m_revolve360 ? " 360" : "",
                      m_revolveRibs ? " ribs" : "");
        out += rev;
    }
    // Storeys travel only when something stands on them.
    for (size_t k = 0; k < m_upper.size(); ++k) {
        bool any = false;
        for (char c : m_upper[k]) if (c != '_') { any = true; break; }
        if (!any) continue;
        out += "deck" + std::to_string(k + 2) + ":\n";
        for (int y = 0; y < kH; ++y) {
            out.append(&m_upper[k][y * kW], kW);
            out.push_back('\n');
        }
    }
    return out;
}

bool Shipwright::deserialize(const std::string& text) {
    std::vector<char> next(kW * kH, '_');
    std::vector<std::vector<char>> upper;
    std::istringstream in(text);
    std::string line;
    int y = 0, yt = 0;
    int layer = 0;                      // 0 = deck 1; k = upper[k-1]
    std::vector<float> loft(kH, kLoftDefault);
    float revolve = 0.0f;
    bool revGlass = false, rev360 = false, revRibs = false;
    auto legalLower = [](char c) {
        return c=='#'||c=='.'||c=='D'||c=='W'||c=='X'||c=='F'||c=='L'||isRoom(c);
    };
    while (std::getline(in, line)) {
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
            revRibs  = line.find(" ribs")  != std::string::npos;
            continue;
        }
        // 'deckN:' opens storey N's grid; 'topside:' is the legacy alias
        // for deck 2 (it only ever carried masts).
        int wantLayer = -1;
        if (line.rfind("topside:", 0) == 0) wantLayer = 1;
        else if (line.rfind("deck", 0) == 0 && line.find(':') != std::string::npos)
            wantLayer = std::atoi(line.c_str() + 4) - 1;
        if (wantLayer >= 1) {
            layer = wantLayer;
            while (static_cast<int>(upper.size()) < layer)
                upper.emplace_back(kW * kH, '_');
            yt = 0;
            continue;
        }
        if (layer > 0) {
            if (yt >= kH) continue;
            auto& g = upper[layer - 1];
            for (int x = 0; x < kW && x < static_cast<int>(line.size()); ++x) {
                const char c = line[x];
                g[yt * kW + x] = (c == 'A' || legalLower(c)) ? c : '_';
            }
            ++yt;
            continue;
        }
        if (y >= kH) continue;
        for (int x = 0; x < kW && x < static_cast<int>(line.size()); ++x) {
            const char c = line[x];
            next[y * kW + x] = legalLower(c) ? c : '_';
        }
        ++y;
    }
    if (y == 0) return false;
    m_cells = std::move(next);
    m_upper = std::move(upper);
    m_level = 0;
    m_loft = std::move(loft);
    m_revolveOn = revolve > 0.0f;
    if (m_revolveOn) {
        m_revolveScale = std::clamp(revolve, 0.2f, 1.0f);
        m_revolveFill = revGlass ? 1 : 0;
        m_revolve360 = rev360;
        m_revolveRibs = revRibs;
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

    // ---- the ticker: yard wisdom on an endless reel ------------------------
    {
        static const char* kWisdom[] = {
            "a reactor with no fins is a bomb with a schedule -- fins with no reactor are jewellery",
            "walls make rooms, letters make sockets",
            "the keel stays flat: ships land on their bellies",
            "every exhaust grid wants an engine room behind it",
            "draw the reactor room 2x2 -- plants are big",
            "robot stations are an investment, not a doodle: 3,500 CR the pad",
            "nanocomposite flies lighter than alloy; exotic laminate stops what alloy cannot",
            "one part per socket, and the pad does the aiming",
            "short of power? kill the cargo bay to feed engine three -- the helm console remembers",
            "bow at the top -- she flies the way you drew her",
            "a hull you cannot enter is a sculpture, not a ship",
            "Finalize before you pay: the survey shows her rooms by name",
            "service is where you stand, power_out is where the kilowatts leave -- never plug a toaster into a doormat",
            "wrong connections have consequences -- blown transformers and friends",
            "the floor is for boots, the wall is for wire -- the void carries no workers' comp",
            "a floor plan cannot hold a mast -- the Mast brush closes the roof and stands her voice topside",
            "no comms mast, no hails: she flies mute",
            "an antenna rooted in vacuum hails nobody",
        };
        static std::string reel;
        if (reel.empty()) {
            for (const char* q : kWisdom) { reel += q; reel += "      +++      "; }
        }
        static float scroll = 0.0f;
        scroll += ImGui::GetIO().DeltaTime * 28.0f;
        const float reelW = ImGui::CalcTextSize(reel.c_str()).x;
        if (scroll > reelW) scroll -= reelW;
        const float availW = ImGui::GetContentRegionAvail().x;
        const ImVec2 tpos = ImGui::GetCursorScreenPos();
        const float th = ImGui::GetTextLineHeight();
        ImDrawList* tdl = ImGui::GetWindowDrawList();
        tdl->AddRectFilled(tpos, ImVec2(tpos.x + availW, tpos.y + th + 6.0f),
                           IM_COL32(20, 22, 28, 255));
        tdl->PushClipRect(tpos, ImVec2(tpos.x + availW, tpos.y + th + 6.0f), true);
        tdl->AddText(ImVec2(tpos.x - scroll, tpos.y + 3.0f),
                     IM_COL32(212, 180, 96, 255), reel.c_str());
        tdl->AddText(ImVec2(tpos.x - scroll + reelW, tpos.y + 3.0f),
                     IM_COL32(212, 180, 96, 255), reel.c_str());
        tdl->PopClipRect();
        ImGui::Dummy(ImVec2(availW, th + 8.0f));
    }

    // ---- tools -------------------------------------------------------------
    // Every brush lives on every storey. Picking Mast at ground level climbs
    // to the first roof by itself -- masts do not stand in rooms.
    for (const auto& t : kTools) {
        ImGui::PushID(t.label);
        const bool active = (m_tool == t.c);
        ImGui::PushStyleColor(ImGuiCol_Button, t.fill);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.fill);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.fill);
        if (ImGui::Button(active ? "##sel" : "##tool", ImVec2(22, 22))) {
            m_tool = t.c;
            if (t.c == 'A' && m_level == 0) {
                if (m_upper.empty()) m_upper.emplace_back(kW * kH, '_');
                m_level = 1;
                m_status = "up to the roof -- masts stand on lids; every other brush draws deck 2 here";
            }
        }
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
    // Storeys: level 0 is deck 1; above it, the slate is the roof of the
    // storey below and every brush draws the next deck on it.
    const bool upperMode = (m_level > 0);
    if (upperMode)
        ImGui::TextColored(ImVec4(0.92f, 0.67f, 0.24f, 1.0f),
                           "DECK %d -- drawing on the roof below; walls make rooms up here too, A stands a mast",
                           m_level + 1);
    else
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
    float volAll = planVolume(m_cells, m_loft);
    {
        // Storeys weigh too: classic wall height everywhere above deck 1,
        // plus each storey's roof riding as its floor plate contribution.
        std::vector<float> flat(kH, kLoftDefault);
        for (const auto& L : m_upper) volAll += planVolume(L, flat);
    }
    const float vol = volAll;
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
        int nSockets = 0;
        float socketBill = planSocketBill(m_cells, nSockets);
        for (const auto& L : m_upper) {
            int nUp = 0;
            socketBill += planSocketBill(L, nUp);
            nSockets += nUp;
        }
        if (nSockets > 0) {
            ImGui::SameLine(0, 18);
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.40f, 1.0f),
                               "%d socket%s %.0f CR", nSockets, nSockets == 1 ? "" : "s", socketBill);
        }
        int nMasts = 0;
        float mastBill = 0.0f;
        for (const auto& L : m_upper) {
            int nUp = 0;
            mastBill += planMastBill(L, nUp);
            nMasts += nUp;
        }
        if (nMasts > 0) {
            ImGui::SameLine(0, 18);
            ImGui::TextColored(ImVec4(0.92f, 0.62f, 0.25f, 1.0f),
                               "%d mast%s %.0f CR", nMasts, nMasts == 1 ? "" : "s", mastBill);
        }
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
    // ("Copy as text" and "Print to console" retired 2026-08-02: they were
    // the prompt-era channels for arguing plans into the generator before
    // BUILD SHIP existed. The clipboard still lives in Save/Load's file.)
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
    if (m_painter) {
        ImGui::SameLine();
        // Push/pop must agree even though the CLICK flips the flag between
        // them -- pop by what was pushed, never by the current state. (The
        // mismatch corrupted ImGui's style stack: "PopStyleColor too many
        // times", found in the crash log.)
        const bool wasOn = m_painterOn;
        if (wasOn) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(140, 90, 40, 255));
        if (ImGui::Button(wasOn ? "Painter [on]" : "Painter")) {
            m_painterOn = m_painter();
            if (m_painterOn) {
                // Straight into the 3D: the drafting table folds away the
                // moment the painter picks up the brush. Tab brings it back.
                open = false;
            }
            m_status = m_painterOn
                ? "painter on -- RMB toggles mouse-look, WASD moves (double-space flies); click pieces, G gizmo, T wires; Tab returns"
                : "painter off -- the hull keeps what she wears";
        }
        if (ImGui::IsItemHovered() && !m_painterOn)
            ImGui::SetTooltip("click-select + Building Textures, as on akelba's slabs");
        if (wasOn) ImGui::PopStyleColor();
    }
    if (m_scrap) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(150, 45, 40, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(185, 55, 48, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(120, 35, 32, 255));
        if (ImGui::Button("Scrap")) ImGui::OpenPopup("Scrap the ship?");
        ImGui::PopStyleColor(3);
        if (ImGui::BeginPopupModal("Scrap the ship?", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Every yard-built piece and everything fitted aboard "
                               "goes to the breakers. The plan on this table survives; "
                               "the credits do not come back.");
            ImGui::Separator();
            if (ImGui::Button("Scrap her", ImVec2(130, 0))) {
                m_status = m_scrap();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep her", ImVec2(130, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    if (m_saveShip) {
        ImGui::SameLine();
        if (ImGui::Button("Save Ship")) ImGui::OpenPopup("Save ship as");
        if (ImGui::BeginPopupModal("Save ship as", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextDisabled("the ship nearest you: hull, fittings, wiring -- one file");
            const bool enter = ImGui::InputText("name", m_shipNameBuf, sizeof m_shipNameBuf,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Save", ImVec2(120, 0)) || enter) {
                m_status = m_saveShip(m_shipNameBuf);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    if (m_launchShip && m_listShips) {
        ImGui::SameLine();
        if (ImGui::Button("Fleet")) ImGui::OpenPopup("Fleet library");
        if (ImGui::BeginPopupModal("Fleet library", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const auto ships = m_listShips();
            if (ships.empty())
                ImGui::TextDisabled("no ships in assets/ships yet -- Save Ship makes one");
            for (const auto& sn : ships) {
                if (ImGui::Button(sn.c_str(), ImVec2(240, 0))) {
                    m_status = m_launchShip(sn);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(240, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // ---- the grid ----------------------------------------------------------
    const float cell = 13.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::InvisibleButton("plan_grid", ImVec2(kW * cell, kH * cell));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (ImGui::Button(m_sideView ? "Side\nview\n[on]" : "Side\nview")) m_sideView = !m_sideView;
    // THE STAIRCASE: Roof goes up a storey (creating it if it is new),
    // Down comes back. Each level is a full deck plan standing on the
    // roof of the one below.
    {
        bool hasStructure = false;
        {
            auto& L = m_level == 0 ? m_cells : m_upper[m_level - 1];
            for (char c : L) if (c != '_') { hasStructure = true; break; }
        }
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(150, 105, 40, 255));
        if (ImGui::Button("Roof\n^") && hasStructure) {
            if (static_cast<int>(m_upper.size()) < m_level + 1)
                m_upper.emplace_back(kW * kH, '_');
            ++m_level;
            m_status = "roofed -- deck " + std::to_string(m_level + 1) +
                       " draws on this lid; Roof again to keep climbing, Down to descend";
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && !hasStructure)
            ImGui::SetTooltip("draw something on this deck before roofing it");
        ImGui::Text("d.%d", m_level + 1);
        if (m_level > 0 && ImGui::Button("Down\nv")) {
            --m_level;
            m_status = "down to deck " + std::to_string(m_level + 1);
        }
    }
    ImGui::EndGroup();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const int hx = static_cast<int>((mouse.x - origin.x) / cell);
    const int hy = static_cast<int>((mouse.y - origin.y) / cell);

    if (hovered && hx >= 0 && hx < kW && hy >= 0 && hy < kH) {
        if (m_tool == 'A' && m_level == 0) {
            // never paints; the palette click already climbed. Belt and braces.
        } else if (m_tool == '~') {
            // The bucket fills on the CLICK, not the drag -- one pour per
            // click. With mirror on, the mirrored bay gets its own pour if
            // it is a separate pool (a symmetric region fills once).
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                fillFloor(hx, hy);
                const int mx = kW - 1 - hx;
                if (m_mirrorX && mx != hx && m_cells[hy * kW + mx] == '_')
                    fillFloor(mx, hy);
            }
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) paint(hx, hy, '_');
        } else {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))  paint(hx, hy, m_tool);
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) paint(hx, hy, '_');
        }
    }

    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const char c = m_cells[y * kW + x];
            const ImVec2 a(origin.x + x * cell, origin.y + y * cell);
            const ImVec2 b(a.x + cell - 1.0f, a.y + cell - 1.0f);
            // Read the level FRESH: the Down button mutates m_level midway
            // through this very frame, and drawing with the stale upperMode
            // indexed m_upper[-2] -- the "stepping down to the bottom
            // floor" crash. Same disease as the ImGui frame-order lesson:
            // never act on a flag captured before its mutator ran.
            if (m_level > 0) {
                const std::vector<char>& below =
                    (m_level == 1) ? m_cells : m_upper[m_level - 2];
                const char bc = below[y * kW + x];
                const char uc = m_upper[m_level - 1][y * kW + x];
                // The lid stays DARK so this storey's strokes stay bright
                // -- the first slate wash made fresh walls look "greyed
                // out" and read as a bug, not a level.
                ImU32 fill = (bc == '_' || bc == 'A') ? IM_COL32(25, 26, 30, 255)
                           : (bc=='#'||bc=='W'||bc=='X'||bc=='F') ? IM_COL32(66, 70, 80, 255)
                                                                  : IM_COL32(48, 51, 60, 255);
                if (uc != '_') fill = fillFor(uc);
                dl->AddRectFilled(a, b, fill);
                if (uc != '_' && uc != '#' && uc != '.') {
                    const char label[2] = {uc, 0};
                    dl->AddText(ImVec2(a.x + 3.0f, a.y), IM_COL32(0, 0, 0, 200), label);
                }
                continue;
            }
            const int ri = m_overlayIdx.empty() ? -1 : m_overlayIdx[y * kW + x];
            dl->AddRectFilled(a, b, ri >= 0 ? kRoomPalette[ri % kRoomPaletteN] : fillFor(c));
            if (isRoom(c) || c == 'D' || c == 'W' || c == 'X' || c == 'F') {
                const char label[2] = {c, 0};
                dl->AddText(ImVec2(a.x + 3.0f, a.y), IM_COL32(0, 0, 0, 200), label);
            }
        }
    }
    // THE EXHAUST LAW, checked live: every X must touch an E. Orphans get a
    // red ring here and a refusal at the yard -- the drafting table warns
    // before the money does.
    int orphanX = 0, orphanF = 0, coldP = 0;
    const std::vector<char>& lawLayer =
        (m_level > 0 && m_level <= static_cast<int>(m_upper.size()))
            ? m_upper[m_level - 1] : m_cells;
    auto neighbourIs = [&lawLayer](int x, int y, char want) {
        return (x + 1 < kW && lawLayer[y * kW + x + 1] == want) ||
               (x > 0     && lawLayer[y * kW + x - 1] == want) ||
               (y + 1 < kH && lawLayer[(y + 1) * kW + x] == want) ||
               (y > 0     && lawLayer[(y - 1) * kW + x] == want);
    };
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const char c = lawLayer[y * kW + x];
            const bool badX = (c == 'X' && !neighbourIs(x, y, 'E'));
            const bool badF = (c == 'F' && !neighbourIs(x, y, 'P'));
            if (!badX && !badF) continue;
            if (badX) ++orphanX; else ++orphanF;
            const ImVec2 a(origin.x + x * cell, origin.y + y * cell);
            dl->AddRect(a, ImVec2(a.x + cell, a.y + cell), IM_COL32(255, 60, 60, 230), 0, 0, 2.0f);
        }
    }
    // THE SUPPORT LAW, checked live: an edit below can pull the floor out
    // from under a storey -- every unsupported cell up here gets a red
    // ring and a refusal at the yard.
    // THE ANCHOR LAW, checked live: overhangs are welcome, but every
    // connected piece of a storey must touch the storey below somewhere.
    // Unanchored islands ring red, whole.
    int orphanA = 0;
    if (m_level > 0) {   // fresh read -- see the mid-frame mutation note above
        const std::vector<char>& below =
            (m_level == 1) ? m_cells : m_upper[m_level - 2];
        const auto& L = m_upper[m_level - 1];
        std::vector<char> seenC(kW * kH, 0);
        for (int i0 = 0; i0 < kW * kH; ++i0) {
            if (seenC[i0] || L[i0] == '_' || L[i0] == 'A') continue;
            std::vector<int> comp, stk{i0};
            bool anchored = false;
            while (!stk.empty()) {
                const int j = stk.back(); stk.pop_back();
                if (j < 0 || j >= kW * kH || seenC[j] || L[j] == '_' || L[j] == 'A') continue;
                seenC[j] = 1; comp.push_back(j);
                if (below[j] != '_' && below[j] != 'A') anchored = true;
                const int jx = j % kW, jy = j / kW;
                if (jx > 0)      stk.push_back(j - 1);
                if (jx < kW - 1) stk.push_back(j + 1);
                if (jy > 0)      stk.push_back(j - kW);
                if (jy < kH - 1) stk.push_back(j + kW);
            }
            if (anchored) continue;
            orphanA += static_cast<int>(comp.size());
            for (const int j : comp) {
                const ImVec2 a(origin.x + (j % kW) * cell, origin.y + (j / kW) * cell);
                dl->AddRect(a, ImVec2(a.x + cell, a.y + cell),
                            IM_COL32(255, 60, 60, 230), 0, 0, 2.0f);
            }
        }
    }
    // Reactor clusters that reach no fin: flood each P blob once.
    {
        std::vector<char> pseen(kW * kH, 0);
        for (int i = 0; i < kW * kH; ++i) {
            if (m_cells[i] != 'P' || pseen[i]) continue;
            bool finned = false;
            std::vector<int> stack{i};
            while (!stack.empty()) {
                const int j = stack.back(); stack.pop_back();
                if (j < 0 || j >= kW * kH || pseen[j] || m_cells[j] != 'P') continue;
                pseen[j] = 1;
                const int jx = j % kW, jy = j / kW;
                if (neighbourIs(jx, jy, 'F')) finned = true;
                if (jx > 0)      stack.push_back(j - 1);
                if (jx < kW - 1) stack.push_back(j + 1);
                if (jy > 0)      stack.push_back(j - kW);
                if (jy < kH - 1) stack.push_back(j + kW);
            }
            if (!finned) ++coldP;
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
            ImGui::Checkbox("ribs", &m_revolveRibs);
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
                if (c == '#' || c == 'X' || c == 'F') hasWall = true;
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

    if (orphanX > 0 || orphanF > 0 || coldP > 0 || orphanA > 0) {
        ImGui::Separator();
        if (orphanA > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "%d cell%s float with no anchor -- an overhang must touch the storey below somewhere; the yard will refuse",
                               orphanA, orphanA == 1 ? "" : "s");
        if (orphanX > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "%d exhaust cell%s without an adjacent Engine (E) -- the yard will refuse",
                               orphanX, orphanX == 1 ? "" : "s");
        if (orphanF > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "%d radiator fin%s without an adjacent Reactor (P) -- the yard will refuse",
                               orphanF, orphanF == 1 ? "" : "s");
        if (coldP > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "%d reactor room%s with no radiator fin (F) on their walls -- the yard will refuse",
                               coldP, coldP == 1 ? "" : "s");
    }
    if (!m_status.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_status.c_str());
    }
    ImGui::End();
}
