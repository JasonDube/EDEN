#include "EditorUI.hpp"
#include "SceneObject.hpp"
#include "AINode.hpp"
#include "AIPath.hpp"
#include "Zone/ZoneSystem.hpp"
#include <eden/Action.hpp>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <set>
#include <iostream>

namespace eden {

EditorUI::EditorUI() {
    // Initialize color swatches
    m_colorSwatches = {
        {0.2f, 0.5f, 0.15f},   // Grass green
        {0.15f, 0.4f, 0.1f},   // Dark green
        {0.76f, 0.7f, 0.5f},   // Sand
        {0.4f, 0.35f, 0.3f},   // Rock brown
        {0.5f, 0.45f, 0.4f},   // Light rock
        {0.95f, 0.95f, 0.95f}, // Snow white
        {0.1f, 0.2f, 0.6f},    // Water blue
        {0.6f, 0.3f, 0.1f},    // Dirt
        {0.3f, 0.25f, 0.2f},   // Dark dirt
        {0.8f, 0.6f, 0.4f},    // Light sand
        {0.1f, 0.3f, 0.1f},    // Forest green
        {0.7f, 0.7f, 0.7f},    // Gray stone
    };
}

void EditorUI::setBrushPosition(const glm::vec3& pos, bool valid) {
    m_brushPos = pos;
    m_hasBrushPos = valid;
}

void EditorUI::render() {
    renderMenuBar();

    if (m_showTerrainEditor) {
        renderMainWindow();
    }

    if (m_brushMode == BrushMode::Paint) {
        renderColorSwatches();
    }

    if (m_brushMode == BrushMode::Texture) {
        renderTextureSelector();
    }

    if (m_brushMode == BrushMode::PathMode) {
        renderPathToolWindow();
    }

    if (m_skyParams && m_showSkySettings) {
        renderSkySettings();
    }

    if (m_showWaterSettings) {
        renderWaterSettings();
    }

    if (m_showLevelSettings) {
        renderLevelSettings();
    }

    if (m_showCharacterController) {
        renderCharacterController();
    }

    if (m_showModels) {
        renderModelsWindow();
    }

    if (m_showTerrainInfo) {
        renderTerrainInfo();
    }

    if (m_showAINodes) {
        renderAINodesWindow();
    }

    if (m_showHelp) {
        renderHelpWindow();
    }

    if (m_showTechTree) {
        renderTechTreeWindow();
    }

    if (m_showGroveEditor) {
        renderGroveEditor();
    }

    if (m_showZones) {
        renderZonesWindow();
    }

    if (m_showMindMap) {
        renderMindMapWindow();
    }

    // Show building texture window in build modes or when a building part is selected
    bool inBuildMode = (m_brushMode == BrushMode::WallDraw || m_brushMode == BrushMode::Foundation);
    bool hasBuildingSelected = false;
    if (m_selectedObjectIndex >= 0 && m_selectedObjectIndex < static_cast<int>(m_sceneObjects.size())) {
        const auto& name = m_sceneObjects[m_selectedObjectIndex]->getName();
        hasBuildingSelected = (name.find("Building_") == 0 || name.find("Foundation_") == 0);
    }
    if (m_showBuildingTextures || inBuildMode || hasBuildingSelected) {
        renderBuildingTextureWindow();
    }
}

void EditorUI::renderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New", "Ctrl+N")) {
                if (m_onFileNew) m_onFileNew();
            }
            if (ImGui::MenuItem("New EDEN OS Level")) {
                if (m_onNewEdenOSLevel) m_onNewEdenOSLevel();
            }
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                if (m_onFileOpen) m_onFileOpen();
            }
            if (ImGui::MenuItem("Save...", "Ctrl+S")) {
                if (m_onFileSave) m_onFileSave();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Export Terrain OBJ...")) {
                if (m_onExportTerrain) m_onExportTerrain();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                if (m_onFileExit) m_onFileExit();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Add")) {
            if (ImGui::MenuItem("Spawn Point")) {
                if (m_onAddSpawn) m_onAddSpawn();
            }
            if (ImGui::MenuItem("Cylinder")) {
                if (m_onAddCylinder) m_onAddCylinder();
            }
            if (ImGui::BeginMenu("Cube")) {
                if (ImGui::MenuItem("Small (1m)")) {
                    if (m_onAddCube) m_onAddCube(1.0f);
                }
                if (ImGui::MenuItem("Medium (3m)")) {
                    if (m_onAddCube) m_onAddCube(3.0f);
                }
                if (ImGui::MenuItem("Large (6m)")) {
                    if (m_onAddCube) m_onAddCube(6.0f);
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Door (Level Transition)")) {
                if (m_onAddDoor) m_onAddDoor();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Game")) {
            if (ImGui::MenuItem("Run Game", "F5")) {
                if (m_onRunGame) m_onRunGame();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem("Terrain Editor", nullptr, &m_showTerrainEditor);
            ImGui::MenuItem("Terrain Info", nullptr, &m_showTerrainInfo);
            ImGui::MenuItem("Sky Settings", nullptr, &m_showSkySettings);
            ImGui::MenuItem("Water Settings", nullptr, &m_showWaterSettings);
            ImGui::MenuItem("Level Settings", nullptr, &m_showLevelSettings);
            ImGui::MenuItem("Character Controller", nullptr, &m_showCharacterController);
            ImGui::MenuItem("Models", nullptr, &m_showModels);
            ImGui::MenuItem("AI Nodes", nullptr, &m_showAINodes);
            ImGui::MenuItem("Tech Tree", nullptr, &m_showTechTree);
            ImGui::MenuItem("Grove Script Editor", nullptr, &m_showGroveEditor);
            ImGui::MenuItem("Zones", nullptr, &m_showZones);
            ImGui::MenuItem("AI Mind Map", nullptr, &m_showMindMap);
            ImGui::MenuItem("Building Textures", nullptr, &m_showBuildingTextures);
            ImGui::MenuItem("Terminal", "Ctrl+`", &m_showTerminal);
            ImGui::Separator();
            if (ImGui::MenuItem("Show All")) {
                m_showTerrainEditor = true;
                m_showTerrainInfo = true;
                m_showSkySettings = true;
                m_showWaterSettings = true;
                m_showLevelSettings = true;
                m_showCharacterController = true;
                m_showModels = true;
                m_showAINodes = true;
                m_showTechTree = true;
                m_showGroveEditor = true;
                m_showZones = true;
                m_showMindMap = true;
            }
            if (ImGui::MenuItem("Hide All")) {
                m_showTerrainEditor = false;
                m_showTerrainInfo = false;
                m_showSkySettings = false;
                m_showWaterSettings = false;
                m_showLevelSettings = false;
                m_showCharacterController = false;
                m_showModels = false;
                m_showAINodes = false;
                m_showTechTree = false;
                m_showGroveEditor = false;
                m_showMindMap = false;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            ImGui::MenuItem("Keyboard Shortcuts", "F1", &m_showHelp);
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void EditorUI::renderMainWindow() {
    ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 340), ImGuiCond_FirstUseEver);

    ImGui::Begin("Terrain Editor");

    ImGui::Text("FPS: %.1f", m_fps);
    ImGui::Separator();

    ImGui::Text("Camera");
    const char* modeStr = (m_movementMode == MovementMode::Fly) ? "FLY" : "WALK";
    ImVec4 modeColor = (m_movementMode == MovementMode::Fly)
        ? ImVec4(0.4f, 0.7f, 1.0f, 1.0f)   // Blue for fly
        : ImVec4(0.4f, 0.9f, 0.4f, 1.0f);  // Green for walk
    ImGui::Text("Mode: ");
    ImGui::SameLine();
    ImGui::TextColored(modeColor, "%s", modeStr);
    if (m_movementMode == MovementMode::Walk) {
        ImGui::SameLine();
        ImGui::Text(m_onGround ? "(grounded)" : "(airborne)");
    }
    if (ImGui::SliderFloat("Speed", &m_cameraSpeed, 1.0f, 200.0f)) {
        if (m_onSpeedChanged) {
            m_onSpeedChanged(m_cameraSpeed);
        }
    }
    ImGui::Separator();

    ImGui::Checkbox("Terrain Tools", &m_terrainToolsEnabled);
    ImGui::Separator();

    ImGui::Text("Brush Settings");

    // Brush mode
    const char* modeNames[] = { "Raise", "Lower", "Smooth", "Flatten", "Paint", "Crack", "Texture", "Plateau", "Level Min", "Grab", "Select", "Deselect", "Move Object", "Spire", "Ridged", "Trench", "Path", "Terrace", "Flatten to Y", "Wall Draw", "Foundation" };
    int currentMode = static_cast<int>(m_brushMode);
    if (ImGui::Combo("Mode", &currentMode, modeNames, 21)) {
        m_brushMode = static_cast<BrushMode>(currentMode);
    }

    ImGui::SliderFloat("Radius", &m_brushRadius, 1.0f, 50.0f);
    ImGui::SliderFloat("Strength", &m_brushStrength, 0.1f, 50.0f);
    ImGui::SliderFloat("Falloff", &m_brushFalloff, 0.0f, 1.0f);

    if (m_brushMode == BrushMode::FlattenToY) {
        ImGui::SliderFloat("Target Y", &m_pathElevation, -50.0f, 100.0f, "%.1f m");
    }

    if (m_brushMode == BrushMode::WallDraw) {
        ImGui::SliderFloat("Wall Height", &m_wallHeight, 1.0f, 20.0f, "%.1f m");
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Click-drag on terrain to draw a building");
    }

    if (m_brushMode == BrushMode::Foundation) {
        ImGui::SliderFloat("Foundation Height", &m_foundationHeight, 0.1f, 5.0f, "%.1f m");
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Click-drag on terrain to place a foundation");
    }

    // Brush shape selector
    const char* shapeNames[] = { "Circle", "Ellipse", "Square" };
    int currentShape = static_cast<int>(m_brushShape);
    if (ImGui::Combo("Shape", &currentShape, shapeNames, 3)) {
        m_brushShape = static_cast<BrushShape>(currentShape);
    }
    if (m_brushShape == BrushShape::Ellipse) {
        ImGui::SliderFloat("Aspect", &m_brushShapeAspectRatio, 0.1f, 1.0f, "%.2f");
    }
    if (m_brushShape != BrushShape::Circle) {
        float rotDegrees = glm::degrees(m_brushShapeRotation);
        if (ImGui::SliderFloat("Rotation", &rotDegrees, -180.0f, 180.0f, "%.0f deg")) {
            m_brushShapeRotation = glm::radians(rotDegrees);
        }
    }

    ImGui::Separator();
    ImGui::Checkbox("Show Brush Ring", &m_showBrushRing);
    const char* triModes[] = { "Default", "Alternating", "Adaptive" };
    ImGui::Combo("Triangulation", &m_triangulationMode, triModes, 3);

    if (m_hasSelection) {
        ImGui::Separator();
        ImGui::Text("Selection Active");

        // Tilt controls
        static float tiltX = 0.0f;
        static float tiltZ = 0.0f;
        ImGui::Text("Tilt Selection (degrees):");
        ImGui::SliderFloat("Tilt X", &tiltX, -45.0f, 45.0f, "%.1f");
        ImGui::SliderFloat("Tilt Z", &tiltZ, -45.0f, 45.0f, "%.1f");
        if (ImGui::Button("Apply Tilt")) {
            if (m_onTiltSelection && (std::abs(tiltX) > 0.01f || std::abs(tiltZ) > 0.01f)) {
                m_onTiltSelection(tiltX, tiltZ);
                tiltX = 0.0f;
                tiltZ = 0.0f;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            tiltX = 0.0f;
            tiltZ = 0.0f;
        }

        ImGui::Spacing();
        if (ImGui::Button("Clear Selection")) {
            if (m_onClearSelection) {
                m_onClearSelection();
            }
        }
    }

    ImGui::Separator();
    ImGui::Text("Camera Position:");
    ImGui::Text("  X: %.1f  Y: %.1f  Z: %.1f", m_cameraPos.x, m_cameraPos.y, m_cameraPos.z);

    if (m_hasBrushPos) {
        ImGui::Separator();
        ImGui::Text("Brush Position:");
        ImGui::Text("  X: %.1f  Z: %.1f", m_brushPos.x, m_brushPos.z);
        ImGui::Text("  Height: %.1f", m_brushPos.y);
    }

    ImGui::Separator();
    ImGui::Text("Fog Settings");
    float fogCol[3] = {m_fogColor.x, m_fogColor.y, m_fogColor.z};
    if (ImGui::ColorEdit3("Fog Color", fogCol)) {
        m_fogColor = glm::vec3(fogCol[0], fogCol[1], fogCol[2]);
    }
    ImGui::SliderFloat("Fog Start", &m_fogStart, 0.0f, 2000.0f);
    ImGui::SliderFloat("Fog End", &m_fogEnd, 1.0f, 4000.0f);

    ImGui::Separator();
    ImGui::Text("Controls:");
    ImGui::BulletText("Right-click + drag: Look");
    ImGui::BulletText("WASD: Move");
    ImGui::BulletText("Space: Jump/Up");
    ImGui::BulletText("Double-Space: Toggle fly");
    ImGui::BulletText("Shift: Down (fly mode)");
    ImGui::BulletText("Ctrl: Speed boost");
    ImGui::BulletText("Left-click: Paint");

    ImGui::End();
}

void EditorUI::renderColorSwatches() {
    ImGui::SetNextWindowPos(ImVec2(10, 380), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 150), ImGuiCond_FirstUseEver);
    ImGui::Begin("Color Swatches");

    // Current color preview
    ImGui::Text("Current Color:");
    ImGui::SameLine();
    ImVec4 currentCol(m_paintColor.x, m_paintColor.y, m_paintColor.z, 1.0f);
    ImGui::ColorButton("##current", currentCol, 0, ImVec2(40, 20));

    ImGui::Separator();

    // Swatch grid
    int columns = 6;
    for (size_t i = 0; i < m_colorSwatches.size(); i++) {
        ImGui::PushID(static_cast<int>(i));
        ImVec4 col(m_colorSwatches[i].x, m_colorSwatches[i].y, m_colorSwatches[i].z, 1.0f);

        bool selected = (static_cast<int>(i) == m_selectedSwatch);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 0, 1));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
        }

        if (ImGui::ColorButton("##swatch", col, 0, ImVec2(35, 35))) {
            m_selectedSwatch = static_cast<int>(i);
            m_paintColor = m_colorSwatches[i];
        }

        if (selected) {
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }

        ImGui::PopID();

        if ((i + 1) % columns != 0 && i < m_colorSwatches.size() - 1) {
            ImGui::SameLine();
        }
    }

    ImGui::Separator();
    // Custom color picker
    float col[3] = {m_paintColor.x, m_paintColor.y, m_paintColor.z};
    if (ImGui::ColorEdit3("Custom", col)) {
        m_paintColor = glm::vec3(col[0], col[1], col[2]);
        m_selectedSwatch = -1;
    }

    ImGui::End();
}

void EditorUI::renderTextureSelector() {
    ImGui::SetNextWindowPos(ImVec2(10, 380), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("Texture Layers");

    const char* texNames[] = { "Grass", "Sand/Dirt", "Rock", "Snow" };
    ImVec4 texColors[] = {
        ImVec4(0.39f, 0.59f, 0.31f, 1.0f),  // Grass
        ImVec4(0.71f, 0.63f, 0.47f, 1.0f),  // Sand
        ImVec4(0.47f, 0.43f, 0.39f, 1.0f),  // Rock
        ImVec4(0.94f, 0.94f, 0.98f, 1.0f),  // Snow
    };

    // Layer selection
    if (ImGui::CollapsingHeader("Select Layer", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < 4; i++) {
            ImGui::PushID(i);

            bool selected = (i == m_selectedTexture);
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.9f, 1.0f));
            }

            if (ImGui::ColorButton("##tex", texColors[i], 0, ImVec2(40, 40))) {
                m_selectedTexture = i;
            }

            if (selected) {
                ImGui::PopStyleColor();
            }

            ImGui::SameLine();
            ImGui::Text("%s%s", texNames[i], selected ? " [Paint]" : "");

            ImGui::PopID();
        }
    }

    // Color adjustments for selected layer
    if (ImGui::CollapsingHeader("Color Adjustments", ImGuiTreeNodeFlags_DefaultOpen)) {
        int i = m_selectedTexture;

        ImGui::Text("Adjusting: %s", texNames[i]);
        ImGui::Spacing();

        // Get current HSB values
        float hue = (&m_texHue.x)[i];
        float sat = (&m_texSaturation.x)[i];
        float bright = (&m_texBrightness.x)[i];

        // Create a preview color from HSB adjustments
        // Start with a reference color (neutral for that texture type)
        float baseHue = 0.0f;
        switch (i) {
            case 0: baseHue = 0.33f; break;  // Grass - green
            case 1: baseHue = 0.1f; break;   // Sand - yellow/tan
            case 2: baseHue = 0.08f; break;  // Rock - brown/gray
            case 3: baseHue = 0.0f; break;   // Snow - white (no hue)
        }

        // Convert HSB adjustments to a preview color
        float previewHue = baseHue + hue / 360.0f;
        while (previewHue < 0.0f) previewHue += 1.0f;
        while (previewHue > 1.0f) previewHue -= 1.0f;
        float previewSat = std::clamp(0.5f * sat, 0.0f, 1.0f);  // Base sat 0.5
        float previewVal = std::clamp(0.7f * bright, 0.0f, 1.0f);  // Base val 0.7

        // Convert HSV to RGB for the color picker
        float rgb[3];
        ImGui::ColorConvertHSVtoRGB(previewHue, previewSat, previewVal, rgb[0], rgb[1], rgb[2]);

        // Color picker - when user changes it, update HSB values
        ImGui::Text("Color Preview & Picker:");
        if (ImGui::ColorPicker3("##colorpicker", rgb,
            ImGuiColorEditFlags_PickerHueWheel |
            ImGuiColorEditFlags_NoSidePreview |
            ImGuiColorEditFlags_NoInputs |
            ImGuiColorEditFlags_NoAlpha)) {

            // Convert picked RGB back to HSV
            float h, s, v;
            ImGui::ColorConvertRGBtoHSV(rgb[0], rgb[1], rgb[2], h, s, v);

            // Calculate HSB adjustments relative to base
            float newHue = (h - baseHue) * 360.0f;
            if (newHue > 180.0f) newHue -= 360.0f;
            if (newHue < -180.0f) newHue += 360.0f;

            float newSat = (s > 0.01f) ? s / 0.5f : 1.0f;  // Relative to base sat
            float newBright = (v > 0.01f) ? v / 0.7f : 1.0f;  // Relative to base val

            (&m_texHue.x)[i] = newHue;
            (&m_texSaturation.x)[i] = std::clamp(newSat, 0.0f, 2.0f);
            (&m_texBrightness.x)[i] = std::clamp(newBright, 0.0f, 2.0f);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Fine Tuning:");

        // Sliders for fine control
        if (ImGui::SliderFloat("Hue Shift", &hue, -180.0f, 180.0f, "%.0f deg")) {
            (&m_texHue.x)[i] = hue;
        }
        if (ImGui::SliderFloat("Saturation", &sat, 0.0f, 2.0f, "%.2f")) {
            (&m_texSaturation.x)[i] = sat;
        }
        if (ImGui::SliderFloat("Brightness", &bright, 0.0f, 2.0f, "%.2f")) {
            (&m_texBrightness.x)[i] = bright;
        }

        ImGui::Spacing();
        if (ImGui::Button("Reset to Original")) {
            (&m_texHue.x)[i] = 0.0f;
            (&m_texSaturation.x)[i] = 1.0f;
            (&m_texBrightness.x)[i] = 1.0f;
        }
    }

    ImGui::End();
}

void EditorUI::renderSkySettings() {
    ImGui::SetNextWindowPos(ImVec2(300, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Sky Settings");

    bool changed = false;

    if (ImGui::CollapsingHeader("Sky Gradient", ImGuiTreeNodeFlags_DefaultOpen)) {
        float zenith[3] = {m_skyParams->zenithColor.x, m_skyParams->zenithColor.y, m_skyParams->zenithColor.z};
        if (ImGui::ColorEdit3("Zenith (Top)", zenith)) {
            m_skyParams->zenithColor = glm::vec3(zenith[0], zenith[1], zenith[2]);
            changed = true;
        }

        float midSky[3] = {m_skyParams->midSkyColor.x, m_skyParams->midSkyColor.y, m_skyParams->midSkyColor.z};
        if (ImGui::ColorEdit3("Mid Sky", midSky)) {
            m_skyParams->midSkyColor = glm::vec3(midSky[0], midSky[1], midSky[2]);
            changed = true;
        }

        float below[3] = {m_skyParams->belowHorizonColor.x, m_skyParams->belowHorizonColor.y, m_skyParams->belowHorizonColor.z};
        if (ImGui::ColorEdit3("Below Horizon", below)) {
            m_skyParams->belowHorizonColor = glm::vec3(below[0], below[1], below[2]);
            changed = true;
        }

        if (ImGui::SliderFloat("Horizon Height", &m_skyParams->horizonHeight, 0.1f, 0.5f)) {
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Horizon Colors", ImGuiTreeNodeFlags_DefaultOpen)) {
        float h1[3] = {m_skyParams->horizonColor1.x, m_skyParams->horizonColor1.y, m_skyParams->horizonColor1.z};
        if (ImGui::ColorEdit3("Horizon 1", h1)) {
            m_skyParams->horizonColor1 = glm::vec3(h1[0], h1[1], h1[2]);
            changed = true;
        }

        float h2[3] = {m_skyParams->horizonColor2.x, m_skyParams->horizonColor2.y, m_skyParams->horizonColor2.z};
        if (ImGui::ColorEdit3("Horizon 2", h2)) {
            m_skyParams->horizonColor2 = glm::vec3(h2[0], h2[1], h2[2]);
            changed = true;
        }

        float h3[3] = {m_skyParams->horizonColor3.x, m_skyParams->horizonColor3.y, m_skyParams->horizonColor3.z};
        if (ImGui::ColorEdit3("Horizon 3", h3)) {
            m_skyParams->horizonColor3 = glm::vec3(h3[0], h3[1], h3[2]);
            changed = true;
        }

        float h4[3] = {m_skyParams->horizonColor4.x, m_skyParams->horizonColor4.y, m_skyParams->horizonColor4.z};
        if (ImGui::ColorEdit3("Horizon 4", h4)) {
            m_skyParams->horizonColor4 = glm::vec3(h4[0], h4[1], h4[2]);
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Nebula")) {
        if (ImGui::SliderFloat("Intensity", &m_skyParams->nebulaIntensity, 0.0f, 1.0f)) {
            changed = true;
        }
        if (ImGui::SliderFloat("Scale", &m_skyParams->nebulaScale, 0.5f, 5.0f)) {
            changed = true;
        }

        float n1[3] = {m_skyParams->nebulaColor1.x, m_skyParams->nebulaColor1.y, m_skyParams->nebulaColor1.z};
        if (ImGui::ColorEdit3("Nebula Color 1", n1)) {
            m_skyParams->nebulaColor1 = glm::vec3(n1[0], n1[1], n1[2]);
            changed = true;
        }

        float n2[3] = {m_skyParams->nebulaColor2.x, m_skyParams->nebulaColor2.y, m_skyParams->nebulaColor2.z};
        if (ImGui::ColorEdit3("Nebula Color 2", n2)) {
            m_skyParams->nebulaColor2 = glm::vec3(n2[0], n2[1], n2[2]);
            changed = true;
        }

        float n3[3] = {m_skyParams->nebulaColor3.x, m_skyParams->nebulaColor3.y, m_skyParams->nebulaColor3.z};
        if (ImGui::ColorEdit3("Nebula Color 3", n3)) {
            m_skyParams->nebulaColor3 = glm::vec3(n3[0], n3[1], n3[2]);
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Stars")) {
        if (ImGui::SliderFloat("Density", &m_skyParams->starDensity, 0.0f, 0.5f, "%.3f")) {
            changed = true;
        }
        if (ImGui::SliderFloat("Brightness", &m_skyParams->starBrightness, 0.0f, 2.0f)) {
            changed = true;
        }
        if (ImGui::SliderFloat("Size Min", &m_skyParams->starSizeMin, 0.1f, 2.0f)) {
            changed = true;
        }
        if (ImGui::SliderFloat("Size Max", &m_skyParams->starSizeMax, 0.5f, 5.0f)) {
            changed = true;
        }

        ImGui::Separator();
        ImGui::Text("Star Colors");
        if (ImGui::SliderFloat("Color Intensity", &m_skyParams->starColorIntensity, 0.0f, 1.0f)) {
            changed = true;
        }

        // Helper lambda to normalize all percentages to sum to 100
        auto normalizeColors = [this]() {
            float sum = m_skyParams->starWhitePercent + m_skyParams->starBluePercent +
                        m_skyParams->starYellowPercent + m_skyParams->starOrangePercent +
                        m_skyParams->starRedPercent;
            if (sum > 0.1f) {
                float scale = 100.0f / sum;
                m_skyParams->starWhitePercent *= scale;
                m_skyParams->starBluePercent *= scale;
                m_skyParams->starYellowPercent *= scale;
                m_skyParams->starOrangePercent *= scale;
                m_skyParams->starRedPercent *= scale;
            }
        };

        float total = m_skyParams->starWhitePercent + m_skyParams->starBluePercent +
                      m_skyParams->starYellowPercent + m_skyParams->starOrangePercent +
                      m_skyParams->starRedPercent;
        ImGui::Text("Distribution (Total: %.0f%%)", total);

        if (ImGui::SliderFloat("White", &m_skyParams->starWhitePercent, 0.0f, 100.0f, "%.0f%%")) {
            normalizeColors();
            changed = true;
        }
        if (ImGui::SliderFloat("Blue", &m_skyParams->starBluePercent, 0.0f, 100.0f, "%.0f%%")) {
            normalizeColors();
            changed = true;
        }
        if (ImGui::SliderFloat("Yellow", &m_skyParams->starYellowPercent, 0.0f, 100.0f, "%.0f%%")) {
            normalizeColors();
            changed = true;
        }
        if (ImGui::SliderFloat("Orange", &m_skyParams->starOrangePercent, 0.0f, 100.0f, "%.0f%%")) {
            normalizeColors();
            changed = true;
        }
        if (ImGui::SliderFloat("Red", &m_skyParams->starRedPercent, 0.0f, 100.0f, "%.0f%%")) {
            normalizeColors();
            changed = true;
        }
    }

    ImGui::End();

    if (changed && m_onSkyChanged) {
        m_onSkyChanged(*m_skyParams);
    }
}

void EditorUI::handleObjectClick(int objectIndex) {
    bool ctrlHeld = ImGui::GetIO().KeyCtrl;
    bool shiftHeld = ImGui::GetIO().KeyShift;

    if (ctrlHeld) {
        // Ctrl+click: toggle selection
        if (m_selectedObjectIndices.count(objectIndex) > 0) {
            m_selectedObjectIndices.erase(objectIndex);
        } else {
            m_selectedObjectIndices.insert(objectIndex);
        }
    } else if (shiftHeld && m_lastClickedObjectIndex >= 0) {
        // Shift+click: range selection from last clicked
        int start = std::min(m_lastClickedObjectIndex, objectIndex);
        int end = std::max(m_lastClickedObjectIndex, objectIndex);
        for (int i = start; i <= end; i++) {
            if (i >= 0 && i < static_cast<int>(m_sceneObjects.size())) {
                m_selectedObjectIndices.insert(i);
            }
        }
    } else {
        // Normal click: single select
        m_selectedObjectIndices.clear();
        m_selectedObjectIndices.insert(objectIndex);
    }

    m_lastClickedObjectIndex = objectIndex;

    // Update single-select index for property panel (use first selected)
    if (!m_selectedObjectIndices.empty()) {
        m_selectedObjectIndex = *m_selectedObjectIndices.begin();
    } else {
        m_selectedObjectIndex = -1;
    }

    // Notify callbacks
    if (m_onSelectObject) {
        m_onSelectObject(m_selectedObjectIndex);
    }
    if (m_onMultiSelectObject) {
        m_onMultiSelectObject(m_selectedObjectIndices);
    }
}

void EditorUI::renderModelsWindow() {
    ImGui::SetNextWindowPos(ImVec2(10, 540), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 280), ImGuiCond_FirstUseEver);
    ImGui::Begin("Models");

    // Import section
    if (ImGui::CollapsingHeader("Import Model", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Browse button (opens file dialog)
        if (ImGui::Button("Browse...")) {
            if (m_onBrowseModel) {
                m_onBrowseModel();
            }
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Select GLB/GLTF file");

        ImGui::Separator();

        ImGui::TextWrapped("Or type filename (e.g. rocks_1):");
        ImGui::InputText("##path", m_importPath, sizeof(m_importPath));
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Looks in models/ folder");

        if (ImGui::Button("Import")) {
            if (m_onImportModel && strlen(m_importPath) > 0) {
                m_onImportModel(m_importPath);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            m_importPath[0] = '\0';
        }
    }

    ImGui::Separator();

    // Object list with multi-select and groups
    if (ImGui::CollapsingHeader("Scene Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Ctrl+click: toggle, Shift+click: range, G: group");

        if (m_sceneObjects.empty()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No objects in scene");
        } else {
            // Track which objects are in groups
            std::set<int> objectsInGroups;
            for (const auto& group : m_objectGroups) {
                for (int idx : group.objectIndices) {
                    objectsInGroups.insert(idx);
                }
            }

            // Render groups first
            for (size_t gi = 0; gi < m_objectGroups.size(); gi++) {
                auto& group = m_objectGroups[gi];
                ImGui::PushID(static_cast<int>(gi) + 10000);

                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
                if (group.forceOpenState) {
                    ImGui::SetNextItemOpen(group.expanded);
                    group.forceOpenState = false;
                }

                bool nodeOpen = ImGui::TreeNodeEx(group.name.c_str(), flags);
                group.expanded = nodeOpen;

                // Right-click to ungroup
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Ungroup")) {
                        if (m_onUngroupObjects) {
                            m_onUngroupObjects(static_cast<int>(gi));
                        }
                    }
                    ImGui::EndPopup();
                }

                if (nodeOpen) {
                    for (int objIdx : group.objectIndices) {
                        if (objIdx < 0 || objIdx >= static_cast<int>(m_sceneObjects.size())) continue;
                        SceneObject* obj = m_sceneObjects[objIdx];
                        if (!obj) continue;

                        ImGui::PushID(objIdx);
                        bool isSelected = m_selectedObjectIndices.count(objIdx) > 0;
                        if (ImGui::Selectable(obj->getName().c_str(), isSelected)) {
                            handleObjectClick(objIdx);
                        }
                        if (ImGui::BeginPopupContextItem()) {
                            if (ImGui::MenuItem("Delete")) {
                                if (m_onDeleteObject) m_onDeleteObject(objIdx);
                            }
                            ImGui::EndPopup();
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            // Render ungrouped objects
            for (size_t i = 0; i < m_sceneObjects.size(); i++) {
                if (objectsInGroups.count(static_cast<int>(i)) > 0) continue;

                SceneObject* obj = m_sceneObjects[i];
                if (!obj) continue;

                ImGui::PushID(static_cast<int>(i));
                bool isSelected = m_selectedObjectIndices.count(static_cast<int>(i)) > 0;
                if (ImGui::Selectable(obj->getName().c_str(), isSelected)) {
                    handleObjectClick(static_cast<int>(i));
                }
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Delete")) {
                        if (m_onDeleteObject) m_onDeleteObject(static_cast<int>(i));
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }

        // Show selection count
        if (m_selectedObjectIndices.size() > 1) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "%zu objects selected (press G to group)",
                              m_selectedObjectIndices.size());
        }
    }

    // Group name popup
    if (m_showGroupNamePopup) {
        ImGui::OpenPopup("Name Group");
        m_showGroupNamePopup = false;
    }
    if (ImGui::BeginPopupModal("Name Group", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter group name:");
        ImGui::InputText("##groupname", m_newGroupName, sizeof(m_newGroupName));
        if (ImGui::Button("Create", ImVec2(120, 0))) {
            if (m_onGroupObjects && m_selectedObjectIndices.size() > 1) {
                m_onGroupObjects(m_selectedObjectIndices, m_newGroupName);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Selected object properties
    if (m_selectedObjectIndex >= 0 && m_selectedObjectIndex < static_cast<int>(m_sceneObjects.size())) {
        SceneObject* selected = m_sceneObjects[m_selectedObjectIndex];
        if (selected) {
            ImGui::Separator();

            // Rename field
            static char renameBuffer[256] = "";
            static int lastSelectedIndex = -1;

            // Update buffer when selection changes
            if (lastSelectedIndex != m_selectedObjectIndex) {
                strncpy(renameBuffer, selected->getName().c_str(), sizeof(renameBuffer) - 1);
                renameBuffer[sizeof(renameBuffer) - 1] = '\0';
                lastSelectedIndex = m_selectedObjectIndex;
            }

            ImGui::Text("Name");
            ImGui::PushItemWidth(-1);
            if (ImGui::InputText("##name", renameBuffer, sizeof(renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (strlen(renameBuffer) > 0) {
                    selected->setName(renameBuffer);
                }
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();

            // Being Type dropdown
            ImGui::Text("Being Type");
            ImGui::PushItemWidth(-1);
            int currentType = static_cast<int>(selected->getBeingType());
            const char* beingTypes[] = { "Static", "Human", "Clone", "Robot", "Android", "Cyborg", "Alien", "Eve", "AI Architect", "AlgoBot", "EDEN Companion" };
            if (ImGui::Combo("##beingtype", &currentType, beingTypes, IM_ARRAYSIZE(beingTypes))) {
                selected->setBeingType(static_cast<BeingType>(currentType));
            }
            ImGui::PopItemWidth();
            if (selected->getBeingType() == BeingType::ALGOBOT) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "(Algorithmic worker bot)");
            } else if (selected->getBeingType() == BeingType::EDEN_COMPANION) {
                ImGui::TextColored(ImVec4(0.9f, 0.5f, 1.0f, 1.0f), "(EDEN companion - Liora etc.)");
            } else if (selected->isSentient()) {
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "(Can be talked to)");
            }

            ImGui::Spacing();

            // Description (visible to AI perception)
            ImGui::Text("Description");
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Visible to AI perception.\nDescribes what this object is (e.g. \"timber board: 6x6x2m\").");
            }
            static char descBuffer[256];
            static int descBufferObjIndex = -1;
            // Re-fill buffer when selection changes (not every frame)
            if (descBufferObjIndex != m_selectedObjectIndex) {
                descBufferObjIndex = m_selectedObjectIndex;
                strncpy(descBuffer, selected->getDescription().c_str(), sizeof(descBuffer) - 1);
                descBuffer[sizeof(descBuffer) - 1] = '\0';
            }
            ImGui::PushItemWidth(-1);
            if (ImGui::InputTextMultiline("##description", descBuffer, sizeof(descBuffer),
                    ImVec2(0, ImGui::GetTextLineHeight() * 3))) {
                selected->setDescription(descBuffer);
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();

            // Visibility checkbox
            bool visible = selected->isVisible();
            if (ImGui::Checkbox("Visible", &visible)) {
                selected->setVisible(visible);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Uncheck to hide this object.\nUseful for objects that appear via SET_VISIBLE action.");
            }

            // Door properties (level transition)
            if (selected->isDoor()) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.3f, 0.5f, 1.0f, 1.0f), "Door Properties");

                static char doorIdBuf[64];
                strncpy(doorIdBuf, selected->getDoorId().c_str(), sizeof(doorIdBuf) - 1);
                doorIdBuf[sizeof(doorIdBuf) - 1] = '\0';
                ImGui::Text("Door ID");
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Unique identifier for this door.\nUsed to link doors across levels.");
                }
                if (ImGui::InputText("##doorId", doorIdBuf, sizeof(doorIdBuf))) {
                    selected->setDoorId(doorIdBuf);
                }

                static char targetLevelBuf[256];
                strncpy(targetLevelBuf, selected->getTargetLevel().c_str(), sizeof(targetLevelBuf) - 1);
                targetLevelBuf[sizeof(targetLevelBuf) - 1] = '\0';
                ImGui::Text("Target Level");
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Level file to load when entering this door.\nE.g., 'interior.eden' or full path.");
                }
                if (ImGui::InputText("##targetLevel", targetLevelBuf, sizeof(targetLevelBuf))) {
                    selected->setTargetLevel(targetLevelBuf);
                }

                static char targetDoorBuf[64];
                strncpy(targetDoorBuf, selected->getTargetDoorId().c_str(), sizeof(targetDoorBuf) - 1);
                targetDoorBuf[sizeof(targetDoorBuf) - 1] = '\0';
                ImGui::Text("Target Door ID");
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Door ID in target level to spawn at.\nPlayer appears at this door's position.");
                }
                if (ImGui::InputText("##targetDoorId", targetDoorBuf, sizeof(targetDoorBuf))) {
                    selected->setTargetDoorId(targetDoorBuf);
                }

                ImGui::Separator();
            }

            // Collision checkboxes
            bool aabbCollision = selected->hasAABBCollision();
            if (ImGui::Checkbox("AABB Collision", &aabbCollision)) {
                selected->setAABBCollision(aabbCollision);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Fast box-based collision.\nBlocks player from passing through in play mode.");
            }

            bool polygonCollision = selected->hasPolygonCollision();
            if (ImGui::Checkbox("Polygon Collision", &polygonCollision)) {
                selected->setPolygonCollision(polygonCollision);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Precise triangle-based collision.\nMore accurate but slower. Use for complex shapes.");
            }

            ImGui::Separator();
            ImGui::Text("Bullet Physics Collision:");

            int bulletType = static_cast<int>(selected->getBulletCollisionType());
            bool bulletChanged = false;

            bulletChanged |= ImGui::RadioButton("None##bullet", &bulletType, 0);
            ImGui::SameLine();
            bulletChanged |= ImGui::RadioButton("Box##bullet", &bulletType, 1);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Oriented bounding box.\nRotates with object, tighter than AABB.");
            }
            ImGui::SameLine();
            bulletChanged |= ImGui::RadioButton("Convex##bullet", &bulletType, 2);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Convex hull from vertices.\nTight fit, good performance.");
            }
            ImGui::SameLine();
            bulletChanged |= ImGui::RadioButton("Mesh##bullet", &bulletType, 3);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Exact triangle mesh collision.\nMost accurate, most expensive.");
            }

            if (bulletChanged) {
                selected->setBulletCollisionType(static_cast<BulletCollisionType>(bulletType));
                // Notify that physics needs to be updated
                if (m_onBulletCollisionChanged) {
                    m_onBulletCollisionChanged(selected);
                }
            }

            // Kinematic platform (lift) checkbox
            bool isKinematic = selected->isKinematicPlatform();
            if (ImGui::Checkbox("Kinematic Platform (Lift)", &isKinematic)) {
                selected->setKinematicPlatform(isKinematic);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Mark as moving platform/lift.\nPlayer can stand on this and ride it during play mode.\nUse with MOVE_TO behaviors to create elevators.");
            }

            ImGui::Spacing();

            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                Transform& transform = selected->getTransform();

                // Position
                glm::vec3 oldPos = transform.getPosition();
                glm::vec3 pos = oldPos;
                ImGui::Text("Position");
                if (m_selectedObjectIndices.size() > 1) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "(moves %zu objects)", m_selectedObjectIndices.size());
                }
                ImGui::PushItemWidth(-1);
                bool posChanged = false;
                posChanged |= ImGui::InputFloat("X##pos", &pos.x, 0.5f, 5.0f, "%.2f");
                posChanged |= ImGui::InputFloat("Y##pos", &pos.y, 0.5f, 5.0f, "%.2f");
                posChanged |= ImGui::InputFloat("Z##pos", &pos.z, 0.5f, 5.0f, "%.2f");
                ImGui::PopItemWidth();
                if (posChanged) {
                    glm::vec3 delta = pos - oldPos;
                    transform.setPosition(pos);
                    if (m_onObjectTransformChanged) {
                        m_onObjectTransformChanged(selected);
                    }
                    // Move all other selected objects by the same delta
                    if (m_selectedObjectIndices.size() > 1) {
                        for (int idx : m_selectedObjectIndices) {
                            if (idx == m_selectedObjectIndex) continue;
                            if (idx >= 0 && idx < static_cast<int>(m_sceneObjects.size()) && m_sceneObjects[idx]) {
                                glm::vec3 otherPos = m_sceneObjects[idx]->getTransform().getPosition();
                                m_sceneObjects[idx]->getTransform().setPosition(otherPos + delta);
                                if (m_onObjectTransformChanged) {
                                    m_onObjectTransformChanged(m_sceneObjects[idx]);
                                }
                            }
                        }
                    }
                }

                ImGui::Spacing();

                // Rotation (using stored euler angles to avoid gimbal lock)
                glm::vec3 euler = selected->getEulerRotation();
                ImGui::Text("Rotation");
                ImGui::PushItemWidth(-1);
                bool rotChanged = false;
                rotChanged |= ImGui::InputFloat("X##rot", &euler.x, 1.0f, 15.0f, "%.1f deg");
                rotChanged |= ImGui::InputFloat("Y##rot", &euler.y, 1.0f, 15.0f, "%.1f deg");
                rotChanged |= ImGui::InputFloat("Z##rot", &euler.z, 1.0f, 15.0f, "%.1f deg");
                ImGui::PopItemWidth();
                if (rotChanged) {
                    selected->setEulerRotation(euler);
                    if (m_onObjectTransformChanged) {
                        m_onObjectTransformChanged(selected);
                    }
                }

                ImGui::Spacing();

                // Scale
                glm::vec3 scale = transform.getScale();
                ImGui::Text("Scale");
                ImGui::PushItemWidth(-1);
                bool scaleChanged = false;
                scaleChanged |= ImGui::InputFloat("X##scale", &scale.x, 0.1f, 1.0f, "%.3f");
                scaleChanged |= ImGui::InputFloat("Y##scale", &scale.y, 0.1f, 1.0f, "%.3f");
                scaleChanged |= ImGui::InputFloat("Z##scale", &scale.z, 0.1f, 1.0f, "%.3f");
                ImGui::PopItemWidth();
                if (scaleChanged) {
                    transform.setScale(scale);
                    if (m_onObjectTransformChanged) {
                        m_onObjectTransformChanged(selected);
                    }
                }

                // Uniform scale helper
                ImGui::Spacing();
                static float uniformScale = 1.0f;
                if (ImGui::InputFloat("Uniform Scale", &uniformScale, 0.1f, 1.0f, "%.3f")) {
                    transform.setScale(uniformScale);
                    if (m_onObjectTransformChanged) {
                        m_onObjectTransformChanged(selected);
                    }
                }

                // Reset transform button
                ImGui::Spacing();
                if (ImGui::Button("Reset Transform")) {
                    glm::vec3 currentPos = transform.getPosition();  // Keep position
                    selected->resetTransform();
                    transform.setPosition(currentPos);  // Restore position
                    uniformScale = 1.0f;
                }

                ImGui::SameLine();

                // Freeze Transformations - bake rotation/scale into mesh vertices
                if (ImGui::Button("Freeze Transform")) {
                    if (m_onFreezeTransform) {
                        m_onFreezeTransform(selected);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Bake current rotation and scale into mesh vertices,\nthen reset rotation to 0 and scale to 1.\nUseful for fixing collision on pre-rotated models.");
                }

                // Snap settings
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Snap Settings");
                ImGui::Checkbox("Snap Move", &m_snapMove);
                if (m_snapMove) {
                    ImGui::SameLine();
                    ImGui::PushItemWidth(80);
                    ImGui::InputFloat("Grid Size", &m_snapMoveSize, 0.1f, 0.5f, "%.2f");
                    if (m_snapMoveSize < 0.01f) m_snapMoveSize = 0.01f;
                    ImGui::PopItemWidth();
                }
                ImGui::Checkbox("Snap Rotate", &m_snapRotate);
                if (m_snapRotate) {
                    ImGui::SameLine();
                    ImGui::PushItemWidth(80);
                    ImGui::InputFloat("Angle", &m_snapRotateAngle, 1.0f, 5.0f, "%.1f");
                    if (m_snapRotateAngle < 1.0f) m_snapRotateAngle = 1.0f;
                    ImGui::PopItemWidth();
                }
                ImGui::Checkbox("Snap to Object", &m_snapToObject);
                if (m_snapToObject) {
                    ImGui::SameLine();
                    ImGui::PushItemWidth(80);
                    ImGui::InputFloat("Snap Dist", &m_snapToObjectDist, 0.1f, 0.5f, "%.1f");
                    if (m_snapToObjectDist < 0.01f) m_snapToObjectDist = 0.01f;
                    ImGui::PopItemWidth();
                }
            }

            // Color adjustments
            if (ImGui::CollapsingHeader("Color Adjustments", ImGuiTreeNodeFlags_DefaultOpen)) {
                float hue = selected->getHueShift();
                if (ImGui::SliderFloat("Hue Shift", &hue, -180.0f, 180.0f, "%.0f deg")) {
                    selected->setHueShift(hue);
                }

                float sat = selected->getSaturation();
                if (ImGui::SliderFloat("Saturation", &sat, 0.0f, 2.0f, "%.2f")) {
                    selected->setSaturation(sat);
                }

                float bright = selected->getBrightness();
                if (ImGui::SliderFloat("Brightness", &bright, 0.0f, 2.0f, "%.2f")) {
                    selected->setBrightness(bright);
                }

                // Reset button
                if (ImGui::Button("Reset Colors")) {
                    selected->setHueShift(0.0f);
                    selected->setSaturation(1.0f);
                    selected->setBrightness(1.0f);
                }
            }

            // Prime Directives section
            if (ImGui::CollapsingHeader("Prime Directives", ImGuiTreeNodeFlags_DefaultOpen)) {
                const auto& scripts = selected->getScripts();

                // Show current directives
                if (scripts.empty()) {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No directives assigned");
                } else {
                    for (size_t i = 0; i < scripts.size(); i++) {
                        ImGui::PushID(static_cast<int>(i));
                        ImGui::BulletText("%s", scripts[i].c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X")) {
                            std::string scriptName = scripts[i];
                            selected->removeScript(scriptName);
                            if (m_onScriptRemoved) {
                                m_onScriptRemoved(m_selectedObjectIndex, scriptName);
                            }
                            ImGui::PopID();
                            break;
                        }
                        ImGui::PopID();
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Assign Directive:");

                // Trader directive button
                bool hasTrader = selected->hasScript("trader");
                if (hasTrader) {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Trader", ImVec2(-1, 0))) {
                    selected->addScript("trader");
                    if (m_onScriptAdded) {
                        m_onScriptAdded(m_selectedObjectIndex, "trader");
                    }
                }
                if (hasTrader) {
                    ImGui::EndDisabled();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Makes this model an autonomous trader.\nIt will buy/sell goods and move between GRAPH nodes.");
                }

                // Pirate directive button
                bool hasPirate = selected->hasScript("pirate");
                if (hasPirate) {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Pirate", ImVec2(-1, 0))) {
                    selected->addScript("pirate");
                    if (m_onScriptAdded) {
                        m_onScriptAdded(m_selectedObjectIndex, "pirate");
                    }
                }
                if (hasPirate) {
                    ImGui::EndDisabled();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Makes this model a pirate.\nIt will scan for traders with cargo and attack them\nuntil they jettison their goods.");
                }

                // Allegiant directive button
                bool hasAllegiant = selected->hasScript("allegiant");
                if (hasAllegiant) {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Allegiant", ImVec2(-1, 0))) {
                    selected->addScript("allegiant");
                    if (m_onScriptAdded) {
                        m_onScriptAdded(m_selectedObjectIndex, "allegiant");
                    }
                }
                if (hasAllegiant) {
                    ImGui::EndDisabled();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Marks this AlgoBot as allegiant.\nIt will only accept orders from its designated\nhuman and AI controllers.");
                }
            }

            // Behaviors section
            if (ImGui::CollapsingHeader("Behaviors", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto& behaviors = selected->getBehaviors();

                // Movement speed for FOLLOW_PATH
                float speed = selected->getPatrolSpeed();
                if (ImGui::SliderFloat("Movement Speed", &speed, 0.5f, 20.0f, "%.1f")) {
                    selected->setPatrolSpeed(speed);
                }

                // Daily schedule mode - behaviors repeat each day
                bool dailySchedule = selected->hasDailySchedule();
                if (ImGui::Checkbox("Daily Schedule", &dailySchedule)) {
                    selected->setDailySchedule(dailySchedule);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("When enabled, ON_GAME_TIME behaviors\nreset at midnight and repeat each day.\nUse for NPCs with daily routines.");
                }
                ImGui::Separator();

                if (behaviors.empty()) {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No behaviors - click Add Behavior below");
                } else {
                    for (size_t bi = 0; bi < behaviors.size(); bi++) {
                        Behavior& beh = behaviors[bi];
                        ImGui::PushID(static_cast<int>(bi));

                        // Behavior header with trigger type
                        const char* triggerNames[] = { "ON_GAMESTART", "ON_GAME_TIME", "ON_INTERACT", "ON_PROXIMITY", "ON_SIGNAL", "ON_COLLISION", "ON_COMMAND", "MANUAL" };
                        int triggerIdx = static_cast<int>(beh.trigger);

                        std::string header = beh.name.empty() ?
                            std::string(triggerNames[triggerIdx]) :
                            beh.name + " (" + triggerNames[triggerIdx] + ")";

                        // Use stable ID so changing trigger doesn't collapse the node
                        bool behaviorOpen = ImGui::TreeNodeEx("##behaviorNode", ImGuiTreeNodeFlags_None, "%s", header.c_str());

                        // Save + Delete behavior buttons (on same line)
                        ImGui::SameLine(ImGui::GetWindowWidth() - 70);
                        if (ImGui::SmallButton("Save")) {
                            if (m_onSaveBotScript) {
                                m_onSaveBotScript(selected, beh.name);
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X")) {
                            behaviors.erase(behaviors.begin() + bi);
                            if (behaviorOpen) ImGui::TreePop();
                            ImGui::PopID();
                            break;  // List modified, exit loop
                        }

                        if (behaviorOpen) {
                            // A. Trigger
                            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "A. TRIGGER");
                            if (ImGui::Combo("##trigger", &triggerIdx, triggerNames, 8)) {
                                beh.trigger = static_cast<TriggerType>(triggerIdx);
                            }

                            // Trigger-specific params
                            if (beh.trigger == TriggerType::ON_PROXIMITY) {
                                ImGui::DragFloat("Radius", &beh.triggerRadius, 0.5f, 1.0f, 100.0f);
                            } else if (beh.trigger == TriggerType::ON_SIGNAL) {
                                char signalBuf[64];
                                strncpy(signalBuf, beh.triggerParam.c_str(), sizeof(signalBuf) - 1);
                                if (ImGui::InputText("Signal Name", signalBuf, sizeof(signalBuf))) {
                                    beh.triggerParam = signalBuf;
                                }
                            } else if (beh.trigger == TriggerType::ON_GAME_TIME) {
                                char triggerTimeBuf[8];
                                strncpy(triggerTimeBuf, beh.triggerParam.c_str(), sizeof(triggerTimeBuf) - 1);
                                triggerTimeBuf[sizeof(triggerTimeBuf) - 1] = '\0';
                                ImGui::SetNextItemWidth(60);
                                if (ImGui::InputText("Start Time##triggerTime", triggerTimeBuf, sizeof(triggerTimeBuf))) {
                                    beh.triggerParam = triggerTimeBuf;
                                }
                                ImGui::SameLine();
                                ImGui::TextDisabled("e.g. 0600, 1800");
                            }

                            // B. Actions
                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "B. ACTIONS");
                            ImGui::Indent(16.0f);

                            // List actions (must match ActionType enum order!)
                            const char* actionNames[] = { "ROTATE", "ROTATE_TO", "TURN_TO", "MOVE", "MOVE_TO", "SCALE", "WAIT",
                                "SEND_SIGNAL", "SPAWN_ENTITY", "DESTROY_SELF", "SET_VISIBLE", "SET_PROPERTY", "PLAY_SOUND", "FOLLOW_PATH",
                                "GROVE_COMMAND", "PICKUP", "PLACE_VERTICAL", "PLACE_AT", "PLACE_HORIZONTAL", "PLACE_ROOF", "PLACE_WALL", "CUSTOM" };

                            for (size_t ai = 0; ai < beh.actions.size(); ai++) {
                                Action& act = beh.actions[ai];
                                ImGui::PushID(static_cast<int>(ai));

                                int actionIdx = static_cast<int>(act.type);

                                ImGui::Text("%zu:", ai + 1);
                                ImGui::SameLine();

                                ImGui::SetNextItemWidth(120);
                                if (ImGui::Combo("##type", &actionIdx, actionNames, 21)) {
                                    act.type = static_cast<ActionType>(actionIdx);
                                }

                                // Action-specific parameters
                                if (act.type == ActionType::ROTATE || act.type == ActionType::ROTATE_TO ||
                                    act.type == ActionType::MOVE || act.type == ActionType::MOVE_TO) {
                                    // Position/offset input with exact precision
                                    ImGui::Text(act.type == ActionType::MOVE ? "Offset:" : "Position:");
                                    ImGui::SetNextItemWidth(200);
                                    ImGui::InputFloat3("##vec", &act.vec3Param.x, "%.2f");

                                    // For MOVE_TO, add waypoint dropdown
                                    if (act.type == ActionType::MOVE_TO) {
                                        // Collect all waypoint-type nodes
                                        std::vector<AINode*> waypoints;
                                        for (auto* node : m_aiNodes) {
                                            if (node && (node->getType() == AINodeType::WAYPOINT ||
                                                        node->getType() == AINodeType::PATROL ||
                                                        node->getType() == AINodeType::GRAPH ||
                                                        node->getType() == AINodeType::INTEREST)) {
                                                waypoints.push_back(node);
                                            }
                                        }

                                        if (!waypoints.empty()) {
                                            ImGui::SameLine();
                                            ImGui::SetNextItemWidth(150);
                                            if (ImGui::BeginCombo("##waypoint", "Go to waypoint...")) {
                                                for (auto* wp : waypoints) {
                                                    std::string label = wp->getName() + " (" + AINode::getTypeShortName(wp->getType()) + ")";
                                                    if (ImGui::Selectable(label.c_str())) {
                                                        act.vec3Param = wp->getPosition();
                                                        act.stringParam = wp->getName();  // Store waypoint name for reference
                                                    }
                                                }
                                                ImGui::EndCombo();
                                            }
                                            // Show which waypoint is selected (if any)
                                            if (!act.stringParam.empty()) {
                                                ImGui::SameLine();
                                                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "-> %s", act.stringParam.c_str());
                                            }
                                        }
                                    }

                                    // For MOVE_TO: choice between duration or speed
                                    if (act.type == ActionType::MOVE_TO) {
                                        ImGui::Text("Movement:");
                                        ImGui::SetNextItemWidth(100);
                                        ImGui::InputFloat("Speed (m/s)", &act.floatParam, 0.5f, 1.0f, "%.1f");
                                        if (act.floatParam < 0.0f) act.floatParam = 0.0f;
                                        ImGui::SameLine();
                                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(0 = use duration)");

                                        if (act.floatParam == 0.0f) {
                                            ImGui::SetNextItemWidth(100);
                                            ImGui::InputFloat("Duration (s)", &act.duration, 0.1f, 1.0f, "%.2f");
                                            if (act.duration < 0.0f) act.duration = 0.0f;
                                        }

                                        // Animation selection for skinned models
                                        SceneObject* moveToObj = nullptr;
                                        if (m_selectedObjectIndex >= 0 && m_selectedObjectIndex < static_cast<int>(m_sceneObjects.size())) {
                                            moveToObj = m_sceneObjects[m_selectedObjectIndex];
                                        }

                                        if (moveToObj && moveToObj->isSkinned()) {
                                            const auto& animNames = moveToObj->getAnimationNames();
                                            if (!animNames.empty()) {
                                                ImGui::Text("Animation:");
                                                ImGui::SetNextItemWidth(200);

                                                const char* currentAnim = act.animationParam.empty() ? "(none - keep current)" : act.animationParam.c_str();

                                                if (ImGui::BeginCombo("##movetoanim", currentAnim)) {
                                                    if (ImGui::Selectable("(none - keep current)", act.animationParam.empty())) {
                                                        act.animationParam = "";
                                                    }

                                                    for (const auto& animName : animNames) {
                                                        bool isSelected = (act.animationParam == animName);
                                                        if (ImGui::Selectable(animName.c_str(), isSelected)) {
                                                            act.animationParam = animName;
                                                        }
                                                    }
                                                    ImGui::EndCombo();
                                                }
                                            }
                                        }
                                    } else {
                                        // For ROTATE, ROTATE_TO, MOVE: just duration
                                        ImGui::SetNextItemWidth(100);
                                        ImGui::InputFloat("Duration (s)", &act.duration, 0.1f, 1.0f, "%.2f");
                                        if (act.duration < 0.0f) act.duration = 0.0f;
                                    }

                                    const char* easingNames[] = { "Linear", "Ease In", "Ease Out", "Ease In/Out" };
                                    int easingIdx = static_cast<int>(act.easing);
                                    ImGui::SetNextItemWidth(100);
                                    if (ImGui::Combo("Easing", &easingIdx, easingNames, 4)) {
                                        act.easing = static_cast<Action::Easing>(easingIdx);
                                    }
                                } else if (act.type == ActionType::TURN_TO) {
                                    // TURN_TO: face a position (yaw only)
                                    ImGui::Text("Face position:");
                                    ImGui::SetNextItemWidth(200);
                                    ImGui::InputFloat3("##turnvec", &act.vec3Param.x, "%.2f");

                                    // Waypoint dropdown
                                    std::vector<AINode*> waypoints;
                                    for (auto* node : m_aiNodes) {
                                        if (node && (node->getType() == AINodeType::WAYPOINT ||
                                                    node->getType() == AINodeType::PATROL ||
                                                    node->getType() == AINodeType::GRAPH ||
                                                    node->getType() == AINodeType::INTEREST)) {
                                            waypoints.push_back(node);
                                        }
                                    }

                                    if (!waypoints.empty()) {
                                        ImGui::SameLine();
                                        ImGui::SetNextItemWidth(150);
                                        if (ImGui::BeginCombo("##turnwaypoint", "Face waypoint...")) {
                                            for (auto* wp : waypoints) {
                                                std::string label = wp->getName() + " (" + AINode::getTypeShortName(wp->getType()) + ")";
                                                if (ImGui::Selectable(label.c_str())) {
                                                    act.vec3Param = wp->getPosition();
                                                    act.stringParam = wp->getName();
                                                }
                                            }
                                            ImGui::EndCombo();
                                        }
                                        if (!act.stringParam.empty()) {
                                            ImGui::SameLine();
                                            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "-> %s", act.stringParam.c_str());
                                        }
                                    }

                                    ImGui::SetNextItemWidth(100);
                                    ImGui::InputFloat("Turn time (s)", &act.duration, 0.1f, 0.5f, "%.2f");
                                    if (act.duration < 0.0f) act.duration = 0.0f;
                                } else if (act.type == ActionType::WAIT) {
                                    ImGui::SetNextItemWidth(80);
                                    ImGui::InputFloat("Duration##wait", &act.duration, 1.0f, 10.0f, "%.1f sec");
                                    if (act.duration < 0.0f) act.duration = 0.0f;

                                    // Animation selection for skinned models
                                    SceneObject* selectedObj = nullptr;
                                    if (m_selectedObjectIndex >= 0 && m_selectedObjectIndex < static_cast<int>(m_sceneObjects.size())) {
                                        selectedObj = m_sceneObjects[m_selectedObjectIndex];
                                    }

                                    if (selectedObj && selectedObj->isSkinned()) {
                                        const auto& animNames = selectedObj->getAnimationNames();
                                        if (!animNames.empty()) {
                                            ImGui::Text("Animation:");
                                            ImGui::SetNextItemWidth(200);

                                            // Find current selection
                                            const char* currentAnim = act.stringParam.empty() ? "(none - keep current)" : act.stringParam.c_str();

                                            if (ImGui::BeginCombo("##waitanim", currentAnim)) {
                                                // Option to keep current animation
                                                if (ImGui::Selectable("(none - keep current)", act.stringParam.empty())) {
                                                    act.stringParam = "";
                                                }

                                                // List available animations
                                                for (const auto& animName : animNames) {
                                                    bool isSelected = (act.stringParam == animName);
                                                    if (ImGui::Selectable(animName.c_str(), isSelected)) {
                                                        act.stringParam = animName;
                                                    }
                                                }
                                                ImGui::EndCombo();
                                            }

                                            if (!act.stringParam.empty()) {
                                                ImGui::SameLine();
                                                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "-> %s", act.stringParam.c_str());
                                            }
                                        }
                                    }
                                } else if (act.type == ActionType::SET_VISIBLE) {
                                    ImGui::Checkbox("Visible", &act.boolParam);
                                } else if (act.type == ActionType::FOLLOW_PATH) {
                                    // Path selector
                                    if (!m_aiPaths.empty()) {
                                        int currentPathIdx = -1;
                                        for (size_t pi = 0; pi < m_aiPaths.size(); pi++) {
                                            if (m_aiPaths[pi] && m_aiPaths[pi]->getName() == act.stringParam) {
                                                currentPathIdx = static_cast<int>(pi);
                                                break;
                                            }
                                        }

                                        ImGui::SetNextItemWidth(150);
                                        if (ImGui::BeginCombo("##path", currentPathIdx >= 0 ? m_aiPaths[currentPathIdx]->getName().c_str() : "Select path...")) {
                                            for (size_t pi = 0; pi < m_aiPaths.size(); pi++) {
                                                if (m_aiPaths[pi]) {
                                                    bool isSelected = (currentPathIdx == static_cast<int>(pi));
                                                    if (ImGui::Selectable(m_aiPaths[pi]->getName().c_str(), isSelected)) {
                                                        act.stringParam = m_aiPaths[pi]->getName();
                                                    }
                                                }
                                            }
                                            ImGui::EndCombo();
                                        }
                                    } else {
                                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No paths! Create in AI Nodes");
                                    }
                                } else if (act.type == ActionType::SEND_SIGNAL) {
                                    char sigBuf[64];
                                    strncpy(sigBuf, act.stringParam.c_str(), sizeof(sigBuf) - 1);
                                    ImGui::SetNextItemWidth(150);
                                    if (ImGui::InputText("##signal", sigBuf, sizeof(sigBuf))) {
                                        act.stringParam = sigBuf;
                                    }
                                } else if (act.type == ActionType::PICKUP || act.type == ActionType::PLACE_VERTICAL) {
                                    ImGui::Text(act.type == ActionType::PICKUP ? "Pick up:" : "Place into:");
                                    ImGui::SetNextItemWidth(180);
                                    if (ImGui::BeginCombo("##objpick", act.stringParam.empty() ? "Select object..." : act.stringParam.c_str())) {
                                        for (auto* obj : m_sceneObjects) {
                                            if (!obj || obj == selected) continue;
                                            const std::string& name = obj->getName();
                                            if (name.empty()) continue;
                                            bool isSelected = (act.stringParam == name);
                                            if (ImGui::Selectable(name.c_str(), isSelected)) {
                                                act.stringParam = name;
                                                act.vec3Param = obj->getTransform().getPosition();
                                            }
                                        }
                                        ImGui::EndCombo();
                                    }
                                    ImGui::SetNextItemWidth(80);
                                    ImGui::InputFloat("Speed##pickspd", &act.floatParam, 0.5f, 1.0f, "%.1f");
                                    if (act.floatParam <= 0.0f) act.floatParam = 2.0f;
                                    ImGui::Checkbox("Ground movement", &act.boolParam);
                                } else if (act.type == ActionType::PLACE_AT) {
                                    ImGui::Text("Place at:");
                                    ImGui::SetNextItemWidth(200);
                                    ImGui::InputFloat3("##placepos", &act.vec3Param.x, "%.1f");
                                    ImGui::SetNextItemWidth(80);
                                    ImGui::InputFloat("Speed##placespd", &act.floatParam, 0.5f, 1.0f, "%.1f");
                                    if (act.floatParam <= 0.0f) act.floatParam = 2.0f;
                                    ImGui::Checkbox("Ground movement", &act.boolParam);
                                } else if (act.type == ActionType::PLACE_HORIZONTAL) {
                                    // Show the two target names (pipe-delimited in stringParam)
                                    ImGui::Text("Targets: %s", act.stringParam.c_str());
                                    ImGui::SetNextItemWidth(80);
                                    ImGui::InputFloat("Speed##plhspd", &act.floatParam, 0.5f, 1.0f, "%.1f");
                                    if (act.floatParam <= 0.0f) act.floatParam = 2.0f;
                                    ImGui::Checkbox("Ground movement", &act.boolParam);
                                } else if (act.type == ActionType::PLACE_ROOF) {
                                    // Show the four corner names (pipe-delimited in stringParam)
                                    ImGui::Text("Corners: %s", act.stringParam.c_str());
                                    ImGui::SetNextItemWidth(80);
                                    ImGui::InputFloat("Speed##plrspd", &act.floatParam, 0.5f, 1.0f, "%.1f");
                                    if (act.floatParam <= 0.0f) act.floatParam = 2.0f;
                                    ImGui::Checkbox("Ground movement", &act.boolParam);
                                } else if (act.type == ActionType::PLACE_WALL) {
                                    // Show the two post names (pipe-delimited in stringParam)
                                    ImGui::Text("Posts: %s", act.stringParam.c_str());
                                    ImGui::SetNextItemWidth(80);
                                    ImGui::InputFloat("Speed##plwspd", &act.floatParam, 0.5f, 1.0f, "%.1f");
                                    if (act.floatParam <= 0.0f) act.floatParam = 2.0f;
                                    ImGui::Checkbox("Ground movement", &act.boolParam);
                                }

                                // Delete action button
                                ImGui::SameLine();
                                if (ImGui::SmallButton("X##act")) {
                                    beh.actions.erase(beh.actions.begin() + ai);
                                    ImGui::PopID();
                                    break;
                                }

                                ImGui::PopID();
                            }
                            ImGui::Unindent(16.0f);

                            // C. Exit condition section
                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "C. EXIT CONDITION");

                            const char* exitNames[] = { "NONE", "ON_PATH_COMPLETE", "ON_GAME_TIME", "ON_DURATION", "ON_SIGNAL", "ON_PROXIMITY_EXIT" };
                            int exitIdx = static_cast<int>(beh.exitCondition);

                            ImGui::SetNextItemWidth(150);
                            if (ImGui::Combo("##exit", &exitIdx, exitNames, 6)) {
                                beh.exitCondition = static_cast<ExitCondition>(exitIdx);
                            }

                            // Exit condition parameters
                            if (beh.exitCondition == ExitCondition::ON_GAME_TIME) {
                                char exitTimeBuf[16];
                                strncpy(exitTimeBuf, beh.exitParam.c_str(), sizeof(exitTimeBuf) - 1);
                                exitTimeBuf[sizeof(exitTimeBuf) - 1] = '\0';
                                ImGui::SetNextItemWidth(80);
                                if (ImGui::InputText("Exit Time##exitTime", exitTimeBuf, sizeof(exitTimeBuf))) {
                                    beh.exitParam = exitTimeBuf;
                                }
                                ImGui::SameLine();
                                ImGui::TextDisabled("e.g. 1200, 1800");
                            } else if (beh.exitCondition == ExitCondition::ON_DURATION) {
                                ImGui::SetNextItemWidth(80);
                                ImGui::DragFloat("Seconds", &beh.exitDuration, 1.0f, 0.0f, 86400.0f, "%.0f");
                            } else if (beh.exitCondition == ExitCondition::ON_SIGNAL) {
                                char sigBuf[64];
                                strncpy(sigBuf, beh.exitParam.c_str(), sizeof(sigBuf) - 1);
                                sigBuf[sizeof(sigBuf) - 1] = '\0';
                                ImGui::SetNextItemWidth(150);
                                if (ImGui::InputText("Signal##exit", sigBuf, sizeof(sigBuf))) {
                                    beh.exitParam = sigBuf;
                                }
                            }

                            // D. Options
                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "D. OPTIONS");
                            ImGui::Checkbox("Loop Behavior", &beh.loop);

                            // E. Add action
                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "E. ADD ACTION");
                            static int addActionType = 0;
                            const char* addActionNames[] = { "FOLLOW_PATH", "WAIT", "ROTATE", "MOVE", "MOVE_TO", "TURN_TO" };
                            ImGui::SetNextItemWidth(110);
                            ImGui::Combo("##addtype", &addActionType, addActionNames, 6);
                            ImGui::SameLine();
                            if (ImGui::Button("Add Action")) {
                                if (addActionType == 0) {
                                    Action act;
                                    act.type = ActionType::FOLLOW_PATH;
                                    if (!m_aiPaths.empty()) {
                                        act.stringParam = m_aiPaths[0]->getName();
                                    }
                                    beh.actions.push_back(act);
                                } else if (addActionType == 1) {
                                    beh.actions.push_back(Action::Wait(5.0f));
                                } else if (addActionType == 2) {
                                    beh.actions.push_back(Action::Rotate(glm::vec3(0, 90, 0), 0.5f));
                                } else if (addActionType == 3) {
                                    beh.actions.push_back(Action::Move(glm::vec3(0, 0, 10), 2.0f));
                                } else if (addActionType == 4) {
                                    beh.actions.push_back(Action::MoveTo(glm::vec3(0, 0, 0), 2.0f));
                                } else if (addActionType == 5) {
                                    // TURN_TO - turn to face a position
                                    Action act;
                                    act.type = ActionType::TURN_TO;
                                    act.vec3Param = glm::vec3(0, 0, 0);
                                    act.duration = 0.5f;
                                    beh.actions.push_back(act);
                                }
                            }

                            ImGui::TreePop();
                        }

                        ImGui::PopID();
                    }
                }

                ImGui::Separator();

                // Bot scripts section — list scripts from this object's folder
                if (m_onListBotScripts) {
                    auto scripts = m_onListBotScripts(selected->getName());
                    if (!scripts.empty()) {
                        ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "Scripts (%s)", selected->getName().c_str());
                        for (const auto& scriptName : scripts) {
                            ImGui::Bullet();
                            ImGui::SameLine();
                            ImGui::Text("%s", scriptName.c_str());
                            ImGui::SameLine();
                            std::string loadLabel = "Load##" + scriptName;
                            if (ImGui::SmallButton(loadLabel.c_str())) {
                                if (m_onLoadBotScript) {
                                    m_onLoadBotScript(selected, scriptName);
                                }
                            }
                            ImGui::SameLine();
                            std::string editLabel = "Edit##" + scriptName;
                            if (ImGui::SmallButton(editLabel.c_str())) {
                                // Load script file into Grove editor and open it
                                std::string path = "scripts/" + selected->getName() + "/" + scriptName;
                                std::ifstream inFile(path);
                                if (inFile.is_open()) {
                                    std::string content((std::istreambuf_iterator<char>(inFile)),
                                                         std::istreambuf_iterator<char>());
                                    setGroveSource(content);
                                    m_groveCurrentFile = path;
                                    m_groveModified = false;
                                    m_groveOutput.clear();
                                    m_groveHasError = false;
                                    m_showGroveEditor = true;
                                }
                            }
                        }
                        ImGui::Separator();
                    }
                }

                // Add new behavior button
                if (ImGui::Button("+ Add Behavior (ON_GAMESTART)")) {
                    Behavior newBeh;
                    newBeh.trigger = TriggerType::ON_GAMESTART;
                    // Default to FOLLOW_PATH action if paths exist
                    Action act;
                    act.type = ActionType::FOLLOW_PATH;
                    if (!m_aiPaths.empty()) {
                        act.stringParam = m_aiPaths[0]->getName();
                    }
                    newBeh.actions.push_back(act);
                    selected->addBehavior(newBeh);
                }
                ImGui::SameLine();
                if (ImGui::Button("+ Add Behavior (ON_INTERACT)")) {
                    Behavior newBeh;
                    newBeh.trigger = TriggerType::ON_INTERACT;
                    newBeh.actions.push_back(Action::Rotate(glm::vec3(0, 90, 0), 0.5f, Action::Easing::EASE_OUT));
                    selected->addBehavior(newBeh);
                }

                if (ImGui::Button("Load Grove Script")) {
                    if (m_onLoadBehaviorScript) {
                        m_onLoadBehaviorScript(selected);
                    }
                }
            }

            // Info
            ImGui::Separator();
            ImGui::Text("Vertices: %u", selected->getVertexCount());
            ImGui::Text("Indices: %u", selected->getIndexCount());

            // Delete button
            ImGui::Separator();
            if (ImGui::Button("Delete Object")) {
                if (m_onDeleteObject) {
                    m_onDeleteObject(m_selectedObjectIndex);
                }
            }
        }
    }

    ImGui::End();
}

void EditorUI::renderPathToolWindow() {
    ImGui::SetNextWindowPos(ImVec2(10, 380), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 220), ImGuiCond_FirstUseEver);
    ImGui::Begin("Path Tool");

    // Instructions
    ImGui::TextWrapped("Click on terrain to place control points. Points connect with a smooth spline curve.");
    ImGui::Separator();

    // Point count
    ImGui::Text("Control Points: %zu", m_pathPointCount);

    if (m_pathPointCount < 2) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Need at least 2 points");
    }

    ImGui::Separator();

    // Brush to apply along path
    ImGui::Text("Brush to Apply:");
    const char* pathBrushNames[] = { "Raise", "Lower", "Smooth", "Flatten", "Paint", "Crack", "Texture", "Plateau", "Level Min", "Spire", "Ridged", "Trench", "Terrace", "Flatten to Y" };
    // Map indices to actual BrushMode values (skip Grab, Select, Deselect, MoveObject, PathMode)
    BrushMode pathBrushModes[] = {
        BrushMode::Raise, BrushMode::Lower, BrushMode::Smooth, BrushMode::Flatten,
        BrushMode::Paint, BrushMode::Crack, BrushMode::Texture, BrushMode::Plateau,
        BrushMode::LevelMin, BrushMode::Spire, BrushMode::Ridged, BrushMode::Trench,
        BrushMode::Terrace, BrushMode::FlattenToY
    };

    int currentPathBrush = 13; // Default to Flatten to Y
    for (int i = 0; i < 14; i++) {
        if (pathBrushModes[i] == m_pathBrushMode) {
            currentPathBrush = i;
            break;
        }
    }

    if (ImGui::Combo("##pathbrush", &currentPathBrush, pathBrushNames, 14)) {
        m_pathBrushMode = pathBrushModes[currentPathBrush];
    }

    if (m_pathBrushMode == BrushMode::FlattenToY) {
        ImGui::SliderFloat("Target Y", &m_pathElevation, -50.0f, 100.0f, "%.1f m");
    }

    ImGui::Separator();

    // Action buttons
    bool canApply = m_pathPointCount >= 2;

    if (!canApply) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Apply to Path", ImVec2(-1, 30))) {
        if (m_onApplyPath) {
            m_onApplyPath();
        }
    }
    if (!canApply) {
        ImGui::EndDisabled();
    }

    ImGui::Spacing();

    bool canUndo = m_pathPointCount > 0;
    if (!canUndo) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Undo Point")) {
        if (m_onUndoPathPoint) {
            m_onUndoPathPoint();
        }
    }
    if (!canUndo) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    if (!canUndo) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Clear Path")) {
        if (m_onClearPath) {
            m_onClearPath();
        }
    }
    if (!canUndo) {
        ImGui::EndDisabled();
    }

    ImGui::Separator();

    // Tube/Wire creation section
    ImGui::Text("Create Wire/Tube:");
    ImGui::SliderFloat("Tube Radius", &m_tubeRadius, 0.02f, 1.0f, "%.2f");
    ImGui::SliderInt("Segments", &m_tubeSegments, 4, 16);
    ImGui::ColorEdit3("Wire Color", &m_tubeColor.x);

    if (!canApply) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Create Tube Mesh", ImVec2(-1, 30))) {
        if (m_onCreateTube) {
            m_onCreateTube(m_tubeRadius, m_tubeSegments, m_tubeColor);
        }
    }
    if (!canApply) {
        ImGui::EndDisabled();
    }

    ImGui::Separator();

    // Road creation section
    ImGui::Text("Create Road:");
    ImGui::SliderFloat("Road Width", &m_roadWidth, 1.0f, 20.0f, "%.1f");
    ImGui::ColorEdit3("Road Color", &m_roadColor.x);
    ImGui::Checkbox("Fixed Y Level", &m_roadUseFixedY);
    if (m_roadUseFixedY) {
        ImGui::SliderFloat("Road Y", &m_roadFixedY, -50.0f, 200.0f, "%.1f");
    }

    if (!canApply) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Create Road Mesh", ImVec2(-1, 30))) {
        if (m_onCreateRoad) {
            m_onCreateRoad(m_roadWidth, m_roadColor, m_roadUseFixedY, m_roadFixedY);
        }
    }
    if (!canApply) {
        ImGui::EndDisabled();
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Tip: Adjust Radius, Strength,");
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "and Falloff in main panel");

    ImGui::End();
}

void EditorUI::renderWaterSettings() {
    ImGui::SetNextWindowPos(ImVec2(630, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(250, 180), ImGuiCond_FirstUseEver);
    ImGui::Begin("Water Settings");

    bool changed = false;

    if (ImGui::Checkbox("Show Water", &m_waterVisible)) {
        changed = true;
    }

    ImGui::Separator();

    if (ImGui::SliderFloat("Water Level", &m_waterLevel, -100.0f, 200.0f, "%.1f")) {
        changed = true;
    }

    if (ImGui::SliderFloat("Wave Amplitude", &m_waveAmplitude, 0.0f, 5.0f, "%.2f")) {
        changed = true;
    }

    if (ImGui::SliderFloat("Wave Frequency", &m_waveFrequency, 0.01f, 0.5f, "%.3f")) {
        changed = true;
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Lower terrain below water");
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "level to expose water.");

    ImGui::End();

    if (changed && m_onWaterChanged) {
        m_onWaterChanged(m_waterLevel, m_waveAmplitude, m_waveFrequency, m_waterVisible);
    }
}

void EditorUI::renderLevelSettings() {
    ImGui::SetNextWindowPos(ImVec2(630, 220), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(250, 120), ImGuiCond_FirstUseEver);
    ImGui::Begin("Level Settings");

    ImGui::Text("Physics Backend");
    ImGui::Separator();

    const char* backendNames[] = { "Jolt Physics", "Homebrew Physics" };
    int currentBackend = static_cast<int>(m_physicsBackend);

    if (ImGui::Combo("##physicsbackend", &currentBackend, backendNames, 2)) {
        m_physicsBackend = static_cast<PhysicsBackend>(currentBackend);
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Physics backend is used");
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "when entering play mode.");

    ImGui::End();
}

void EditorUI::renderCharacterController() {
    ImGui::SetNextWindowPos(ImVec2(900, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 480), ImGuiCond_FirstUseEver);
    ImGui::Begin("Character Controller");

    // Physics backend indicator
    const char* backendName = (m_physicsBackend == PhysicsBackend::Jolt) ? "Jolt" : "Homebrew";
    ImVec4 backendColor = (m_physicsBackend == PhysicsBackend::Jolt)
        ? ImVec4(0.3f, 0.7f, 1.0f, 1.0f)   // Blue for Jolt
        : ImVec4(0.3f, 1.0f, 0.5f, 1.0f);  // Green for Homebrew
    ImGui::Text("Physics Backend: ");
    ImGui::SameLine();
    ImGui::TextColored(backendColor, "%s", backendName);
    ImGui::Separator();

    // Camera Mode
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* cameraModes[] = { "First Person", "Third Person" };
        int currentCameraMode = static_cast<int>(m_cameraMode);
        if (ImGui::Combo("Camera Mode", &currentCameraMode, cameraModes, 2)) {
            m_cameraMode = static_cast<CameraMode>(currentCameraMode);
        }

        if (m_cameraMode == CameraMode::ThirdPerson) {
            ImGui::SliderFloat("Distance", &m_thirdPersonDistance, 1.0f, 20.0f, "%.1f m");
            ImGui::SliderFloat("Height", &m_thirdPersonHeight, 0.0f, 10.0f, "%.1f m");
            ImGui::SliderFloat("Look At Height", &m_thirdPersonLookAtHeight, 0.0f, 3.0f, "%.1f m");
            ImGui::Checkbox("Show Collision Hull", &m_showCollisionHull);
        }
    }

    // Controller Type
    if (ImGui::CollapsingHeader("Controller Type", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* controllerTypes[] = { "Character", "Vehicle", "Flight", "Spectator" };
        int currentType = static_cast<int>(m_controllerType);
        if (ImGui::Combo("Type", &currentType, controllerTypes, 4)) {
            m_controllerType = static_cast<ControllerType>(currentType);
        }

        if (m_controllerType == ControllerType::Character) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Walking character with");
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "gravity and jumping.");
        } else if (m_controllerType == ControllerType::Vehicle) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Ground vehicle with");
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "wheel physics.");
        } else if (m_controllerType == ControllerType::Flight) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Flight mode with");
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "6DOF movement.");
        } else if (m_controllerType == ControllerType::Spectator) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Free-flying camera");
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "with no collision.");
        }
    }

    // Movement Settings
    if (ImGui::CollapsingHeader("Movement", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Speed", &m_characterSpeed, 1.0f, 50.0f, "%.1f m/s");
        ImGui::SliderFloat("Sprint Multiplier", &m_characterSprintMultiplier, 1.0f, 5.0f, "%.1fx");
        ImGui::SliderFloat("Jump Velocity", &m_characterJumpVelocity, 1.0f, 30.0f, "%.1f m/s");
        ImGui::SliderFloat("Gravity", &m_characterGravity, 1.0f, 50.0f, "%.1f m/s²");
    }

    // Collision Hull
    if (ImGui::CollapsingHeader("Collision Hull")) {
        const char* hullTypes[] = { "Capsule", "Box", "Sphere" };
        int currentHull = static_cast<int>(m_collisionHullType);
        if (ImGui::Combo("Hull Type", &currentHull, hullTypes, 3)) {
            m_collisionHullType = static_cast<CollisionHullType>(currentHull);
        }

        ImGui::SliderFloat("Height", &m_characterHeight, 0.5f, 4.0f, "%.2f m");
        ImGui::SliderFloat("Radius", &m_characterRadius, 0.1f, 1.0f, "%.2f m");
    }

    // Ragdoll Settings
    if (ImGui::CollapsingHeader("Ragdoll")) {
        ImGui::Checkbox("Enable Ragdoll", &m_ragdollEnabled);
        if (m_ragdollEnabled) {
            ImGui::Checkbox("Ragdoll on Death", &m_ragdollOnDeath);
        }
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Ragdoll physics for");
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "character death/impact.");
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.3f, 1.0f), "Changes apply in play mode");

    ImGui::End();
}

void EditorUI::renderTerrainInfo() {
    ImGui::SetNextWindowPos(ImVec2(10, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 220), ImGuiCond_FirstUseEver);
    ImGui::Begin("Terrain Info");

    // Chunk info
    ImGui::Text("Chunks: %d x %d (%d total)",
        m_terrainInfo.chunkCountX,
        m_terrainInfo.chunkCountZ,
        m_terrainInfo.totalChunks());

    ImGui::Text("Chunk Size: %.0fm (%d vertices)",
        m_terrainInfo.chunkSizeMeters(),
        m_terrainInfo.chunkResolution);

    ImGui::Text("Tile Size: %.1fm", m_terrainInfo.tileSize);
    ImGui::Text("Height Scale: %.0fm", m_terrainInfo.heightScale);

    ImGui::Separator();

    // Metric dimensions
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Metric");
    float sizeM = m_terrainInfo.totalSizeMeters();
    float areaKm = m_terrainInfo.totalAreaSqKm();
    if (sizeM >= 1000.0f) {
        ImGui::Text("Size: %.2f km x %.2f km", sizeM / 1000.0f, sizeM / 1000.0f);
    } else {
        ImGui::Text("Size: %.0f m x %.0f m", sizeM, sizeM);
    }
    ImGui::Text("Area: %.2f sq km", areaKm);

    ImGui::Separator();

    // Imperial dimensions
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Imperial");
    float sizeMi = m_terrainInfo.totalSizeMiles();
    float sizeFt = m_terrainInfo.totalSizeFeet();
    float areaSqMi = m_terrainInfo.totalAreaSqMiles();
    ImGui::Text("Size: %.2f mi x %.2f mi", sizeMi, sizeMi);
    ImGui::Text("Size: %.0f ft x %.0f ft", sizeFt, sizeFt);
    ImGui::Text("Area: %.2f sq mi", areaSqMi);

    ImGui::Separator();

    // Camera position
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Camera: (%.1f, %.1f, %.1f)",
        m_cameraPos.x, m_cameraPos.y, m_cameraPos.z);

    ImGui::End();
}

void EditorUI::renderAINodesWindow() {
    ImGui::SetNextWindowPos(ImVec2(900, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("AI Nodes");

    // Node type names
    const char* typeNames[] = {
        "Waypoint", "Patrol", "Spawn", "Trigger",
        "Objective", "Cover", "Interest", "Graph", "Custom"
    };

    // Add node section
    if (ImGui::CollapsingHeader("Add Node", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Combo("Type", &m_selectedAINodeType, typeNames, 9);

        if (ImGui::Button("Drop Node Below Camera", ImVec2(-1, 0))) {
            if (m_onToggleAIPlacement) {
                m_onToggleAIPlacement(true, m_selectedAINodeType);
            }
        }
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Places node at camera XZ position");
    }

    // Utilities section
    if (ImGui::CollapsingHeader("Utilities", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        if (ImGui::Button("Create Test Economy", ImVec2(-1, 0))) {
            if (m_onCreateTestEconomy) {
                m_onCreateTestEconomy();
            }
        }
        ImGui::PopStyleColor();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Creates producer/manufacturer/consumer\nnodes for testing. Then press F5.");

        ImGui::Separator();

        if (ImGui::Button("Connect All GRAPH Nodes", ImVec2(-1, 0))) {
            if (m_onConnectAllGraphNodes) {
                m_onConnectAllGraphNodes();
            }
        }
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Creates bidirectional connections\nbetween all GRAPH type nodes");
    }

    // Node list
    if (ImGui::CollapsingHeader("Node List", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (m_aiNodes.empty()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No AI nodes in scene");
        } else {
            ImGui::Text("%zu nodes (%zu selected)", m_aiNodes.size(), m_selectedAINodeIndices.size());
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Ctrl+click to multi-select, Shift+click for range");
            ImGui::Separator();

            for (size_t i = 0; i < m_aiNodes.size(); i++) {
                AINode* node = m_aiNodes[i];
                if (!node) continue;

                ImGui::PushID(static_cast<int>(i));

                // Type indicator
                int typeIdx = static_cast<int>(node->getType());
                const char* shortNames[] = {"WP", "PT", "SP", "TR", "OB", "CV", "IN", "GR", "CU"};
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "[%s]", shortNames[typeIdx]);
                ImGui::SameLine();

                bool isInMultiSelect = isAINodeSelected(static_cast<int>(i));
                bool isPrimarySelected = (static_cast<int>(i) == m_selectedAINodeIndex);
                bool isConnectionSource = m_aiConnectionMode && (static_cast<int>(i) == m_aiConnectionSourceIndex);

                // Highlight multi-selected nodes with different color
                if (isInMultiSelect && !isPrimarySelected) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.5f, 0.7f, 1.0f));
                }
                if (isConnectionSource) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.8f, 0.5f, 0.1f, 1.0f));
                }

                if (ImGui::Selectable(node->getName().c_str(), isPrimarySelected || isInMultiSelect || isConnectionSource)) {
                    if (m_aiConnectionMode && static_cast<int>(i) != m_aiConnectionSourceIndex) {
                        // In connection mode, clicking different node creates connection
                        if (m_onConnectAINodes) {
                            m_onConnectAINodes(m_aiConnectionSourceIndex, static_cast<int>(i));
                        }
                        m_aiConnectionMode = false;
                        m_aiConnectionSourceIndex = -1;
                    } else {
                        // Handle multi-selection
                        bool ctrlHeld = ImGui::GetIO().KeyCtrl;
                        bool shiftHeld = ImGui::GetIO().KeyShift;
                        int clickedIdx = static_cast<int>(i);

                        if (ctrlHeld) {
                            // Ctrl+click: toggle selection
                            auto it = std::find(m_selectedAINodeIndices.begin(), m_selectedAINodeIndices.end(), clickedIdx);
                            if (it != m_selectedAINodeIndices.end()) {
                                m_selectedAINodeIndices.erase(it);
                            } else {
                                m_selectedAINodeIndices.push_back(clickedIdx);
                            }
                            m_selectedAINodeIndex = clickedIdx;
                            m_lastClickedNodeIndex = clickedIdx;
                        } else if (shiftHeld && m_lastClickedNodeIndex >= 0) {
                            // Shift+click: range selection
                            int start = std::min(m_lastClickedNodeIndex, clickedIdx);
                            int end = std::max(m_lastClickedNodeIndex, clickedIdx);
                            m_selectedAINodeIndices.clear();
                            for (int j = start; j <= end; j++) {
                                m_selectedAINodeIndices.push_back(j);
                            }
                            m_selectedAINodeIndex = clickedIdx;
                        } else {
                            // Normal click: single selection
                            m_selectedAINodeIndices.clear();
                            m_selectedAINodeIndices.push_back(clickedIdx);
                            m_selectedAINodeIndex = clickedIdx;
                            m_lastClickedNodeIndex = clickedIdx;
                        }

                        if (m_onSelectAINode) {
                            m_onSelectAINode(clickedIdx);
                        }
                    }
                }

                if (isConnectionSource) {
                    ImGui::PopStyleColor();
                }
                if (isInMultiSelect && !isPrimarySelected) {
                    ImGui::PopStyleColor();
                }

                // Right-click context menu
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Delete")) {
                        if (m_onDeleteAINode) {
                            m_onDeleteAINode(static_cast<int>(i));
                        }
                    }
                    if (ImGui::MenuItem("Connect To...")) {
                        if (m_onBeginAIConnection) {
                            m_onBeginAIConnection();
                        }
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }

            // Clear selection button
            if (!m_selectedAINodeIndices.empty()) {
                ImGui::Separator();
                if (ImGui::Button("Clear Selection", ImVec2(-1, 0))) {
                    clearAINodeSelection();
                }
            }
        }
    }

    // Selected node properties
    if (m_selectedAINodeIndex >= 0 && m_selectedAINodeIndex < static_cast<int>(m_aiNodes.size())) {
        AINode* selected = m_aiNodes[m_selectedAINodeIndex];
        if (selected && ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool changed = false;

            // Name input with proper focus handling
            static char nameBuffer[256] = "";
            static int lastSyncedIndex = -1;
            static bool textFieldActive = false;

            // Sync buffer from node ONLY when:
            // 1. Selection changed, AND
            // 2. Text field is not currently active (user not typing)
            if (lastSyncedIndex != m_selectedAINodeIndex && !textFieldActive) {
                strncpy(nameBuffer, selected->getName().c_str(), sizeof(nameBuffer) - 1);
                nameBuffer[sizeof(nameBuffer) - 1] = '\0';
                lastSyncedIndex = m_selectedAINodeIndex;
            }

            ImGui::Text("Name");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30);
            if (ImGui::InputText("##NodeName", nameBuffer, sizeof(nameBuffer))) {
                selected->setName(nameBuffer);
                changed = true;
            }
            // Track if this text field is active (user is typing)
            textFieldActive = ImGui::IsItemActive();

            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Use building names like 'Downtown Chemicals',\n'Ore Processing', 'The After Dark' to link\nwith the economy system.");
            }

            // Type
            int typeIdx = static_cast<int>(selected->getType());
            if (ImGui::Combo("Type##prop", &typeIdx, typeNames, 9)) {
                selected->setType(static_cast<AINodeType>(typeIdx));
                changed = true;
            }

            // Position
            glm::vec3 pos = selected->getPosition();
            if (ImGui::DragFloat3("Position", &pos.x, 0.5f)) {
                selected->setPosition(pos);
                changed = true;
            }

            // Radius
            float radius = selected->getRadius();
            if (ImGui::SliderFloat("Radius", &radius, 1.0f, 50.0f)) {
                selected->setRadius(radius);
                changed = true;
            }

            // GRAPH node specific properties
            if (selected->getType() == AINodeType::GRAPH) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Graph Node Properties");

                // Category dropdown
                const char* categoryNames[] = {
                    "None", "Refuel", "Market", "Warehouse", "Dock",
                    "Factory", "Residence", "Office", "Restaurant", "Hospital", "Custom"
                };
                int catIdx = static_cast<int>(selected->getCategory());
                if (ImGui::Combo("Category", &catIdx, categoryNames, 11)) {
                    selected->setCategory(static_cast<GraphCategory>(catIdx));
                    changed = true;
                }

                // Layer checkboxes
                ImGui::Text("Layers:");
                GraphLayer layers = selected->getLayers();
                bool pedLayer = selected->hasLayer(GraphLayer::PEDESTRIAN);
                bool vehLayer = selected->hasLayer(GraphLayer::VEHICLE);
                bool flyLayer = selected->hasLayer(GraphLayer::FLYING);
                bool waterLayer = selected->hasLayer(GraphLayer::WATER);
                bool railLayer = selected->hasLayer(GraphLayer::RAIL);

                if (ImGui::Checkbox("Pedestrian", &pedLayer)) {
                    if (pedLayer) selected->addLayer(GraphLayer::PEDESTRIAN);
                    else selected->removeLayer(GraphLayer::PEDESTRIAN);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Vehicle", &vehLayer)) {
                    if (vehLayer) selected->addLayer(GraphLayer::VEHICLE);
                    else selected->removeLayer(GraphLayer::VEHICLE);
                    changed = true;
                }

                if (ImGui::Checkbox("Flying", &flyLayer)) {
                    if (flyLayer) selected->addLayer(GraphLayer::FLYING);
                    else selected->removeLayer(GraphLayer::FLYING);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Water", &waterLayer)) {
                    if (waterLayer) selected->addLayer(GraphLayer::WATER);
                    else selected->removeLayer(GraphLayer::WATER);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Rail", &railLayer)) {
                    if (railLayer) selected->addLayer(GraphLayer::RAIL);
                    else selected->removeLayer(GraphLayer::RAIL);
                    changed = true;
                }

                // Edge cost
                float edgeCost = selected->getEdgeCost();
                if (ImGui::SliderFloat("Edge Cost", &edgeCost, 0.1f, 10.0f, "%.2f")) {
                    selected->setEdgeCost(edgeCost);
                    changed = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Multiplier for pathfinding cost.\n1.0 = normal, >1 = avoid, <1 = prefer");
                }
            }

            // Connections
            ImGui::Separator();
            const auto& connections = selected->getConnections();
            ImGui::Text("Connections: %zu", connections.size());

            // Connect To button
            if (m_aiConnectionMode && m_aiConnectionSourceIndex == m_selectedAINodeIndex) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.1f, 1.0f));
                if (ImGui::Button("Cancel Connection", ImVec2(-1, 0))) {
                    m_aiConnectionMode = false;
                    m_aiConnectionSourceIndex = -1;
                }
                ImGui::PopStyleColor();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Click another node to connect");
            } else {
                if (ImGui::Button("Connect To...", ImVec2(-1, 0))) {
                    m_aiConnectionMode = true;
                    m_aiConnectionSourceIndex = m_selectedAINodeIndex;
                }
            }

            // List existing connections with remove button
            if (!connections.empty()) {
                ImGui::Text("Connected to:");
                for (size_t c = 0; c < connections.size(); c++) {
                    uint32_t targetId = connections[c];
                    // Find target node name
                    const char* targetName = "Unknown";
                    int targetIdx = -1;
                    for (size_t j = 0; j < m_aiNodes.size(); j++) {
                        if (m_aiNodes[j] && m_aiNodes[j]->getId() == targetId) {
                            targetName = m_aiNodes[j]->getName().c_str();
                            targetIdx = static_cast<int>(j);
                            break;
                        }
                    }
                    ImGui::PushID(static_cast<int>(c));
                    ImGui::BulletText("%s (ID:%u)", targetName, targetId);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        if (m_onDisconnectAINodes && targetIdx >= 0) {
                            m_onDisconnectAINodes(m_selectedAINodeIndex, targetIdx);
                        }
                    }
                    ImGui::PopID();
                }
            }

            // Delete button
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
            if (ImGui::Button("Delete Node", ImVec2(-1, 0))) {
                if (m_onDeleteAINode) {
                    m_onDeleteAINode(m_selectedAINodeIndex);
                }
            }
            ImGui::PopStyleColor(2);

            if (changed && m_onAINodePropertyChanged) {
                m_onAINodePropertyChanged();
            }
        }
    }

    // Connection mode hint at top level
    if (m_aiConnectionMode) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "CONNECTION MODE ACTIVE");
        ImGui::Text("Select target node from list above");
    }

    // Procedural generation
    if (ImGui::CollapsingHeader("Procedural Generation")) {
        const char* patterns[] = {"Patrol Route", "Grid", "Scattered"};
        ImGui::Combo("Pattern", &m_aiGenPattern, patterns, 3);
        ImGui::SliderInt("Count", &m_aiGenCount, 3, 32);
        ImGui::SliderFloat("Radius", &m_aiGenRadius, 10.0f, 200.0f);

        if (ImGui::Button("Generate at Camera", ImVec2(-1, 0))) {
            if (m_onGenerateAINodes) {
                m_onGenerateAINodes(m_aiGenPattern, m_aiGenCount, m_aiGenRadius);
            }
        }
    }

    // Paths section
    if (ImGui::CollapsingHeader("Paths", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Create path from selected nodes
        ImGui::InputText("Name", m_newPathName, sizeof(m_newPathName));

        bool hasSelection = m_selectedAINodeIndices.size() >= 2;
        if (!hasSelection) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Create Path from Selection", ImVec2(-1, 0))) {
            if (m_onCreatePathFromNodes && hasSelection) {
                m_onCreatePathFromNodes(m_newPathName, m_selectedAINodeIndices);
                // Auto-increment name
                int num = 1;
                sscanf(m_newPathName, "Path_%d", &num);
                snprintf(m_newPathName, sizeof(m_newPathName), "Path_%d", num + 1);
                // Clear selection after creating path
                clearAINodeSelection();
            }
        }

        if (!hasSelection) {
            ImGui::EndDisabled();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Select 2+ nodes above to create path");
        } else {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%zu nodes selected", m_selectedAINodeIndices.size());
        }

        ImGui::Separator();

        // Path list
        if (m_aiPaths.empty()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No paths created");
        } else {
            ImGui::Text("%zu paths", m_aiPaths.size());

            for (size_t i = 0; i < m_aiPaths.size(); i++) {
                AIPath* path = m_aiPaths[i];
                if (!path) continue;

                ImGui::PushID(static_cast<int>(i));

                bool isSelected = (static_cast<int>(i) == m_selectedPathIndex);

                // Color indicator for path
                glm::vec3 color = path->getColor();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color.r, color.g, color.b, 1.0f));
                ImGui::Text("[%zu]", path->getWaypointCount());
                ImGui::PopStyleColor();
                ImGui::SameLine();

                if (ImGui::Selectable(path->getName().c_str(), isSelected)) {
                    m_selectedPathIndex = static_cast<int>(i);
                    if (m_onSelectPath) {
                        m_onSelectPath(static_cast<int>(i));
                    }
                }

                // Right-click context menu
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Delete")) {
                        if (m_onDeletePath) {
                            m_onDeletePath(static_cast<int>(i));
                        }
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        }

        // Selected path properties
        if (m_selectedPathIndex >= 0 && m_selectedPathIndex < static_cast<int>(m_aiPaths.size())) {
            AIPath* path = m_aiPaths[m_selectedPathIndex];
            if (path) {
                ImGui::Separator();
                ImGui::Text("Selected: %s", path->getName().c_str());
                ImGui::Text("Waypoints: %zu", path->getWaypointCount());
                ImGui::Text("Length: %.1f m", path->getTotalLength());

                bool loop = path->isLooping();
                if (ImGui::Checkbox("Loop", &loop)) {
                    path->setLooping(loop);
                    if (m_onPathPropertyChanged) {
                        m_onPathPropertyChanged();
                    }
                }

                // Delete button
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
                if (ImGui::Button("Delete Path", ImVec2(-1, 0))) {
                    if (m_onDeletePath) {
                        m_onDeletePath(m_selectedPathIndex);
                    }
                }
                ImGui::PopStyleColor();
            }
        }
    }

    ImGui::End();
}

void EditorUI::renderHelpWindow() {
    ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Keyboard Shortcuts", &m_showHelp)) {
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Camera Movement");
    ImGui::Separator();
    ImGui::BulletText("W/A/S/D - Move forward/left/back/right");
    ImGui::BulletText("Space - Move up (fly mode)");
    ImGui::BulletText("Left Shift - Move down (fly mode)");
    ImGui::BulletText("Left Ctrl - Speed boost");
    ImGui::BulletText("Right Mouse + Drag - Look around");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "File Operations");
    ImGui::Separator();
    ImGui::BulletText("Ctrl+N - New level");
    ImGui::BulletText("Ctrl+O - Open level");
    ImGui::BulletText("Ctrl+S - Save level");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Transform Tools");
    ImGui::Separator();
    ImGui::BulletText("Q - Select mode");
    ImGui::BulletText("W - Move mode");
    ImGui::BulletText("E - Rotate mode");
    ImGui::BulletText("R - Scale mode");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Editor");
    ImGui::Separator();
    ImGui::BulletText("F5 - Toggle play mode");
    ImGui::BulletText("Delete - Delete selected object");
    ImGui::BulletText("Ctrl+D - Duplicate selected object");
    ImGui::BulletText("F - Focus camera on selected object");
    ImGui::BulletText("Enter - Send message (during conversation)");
    ImGui::BulletText("Escape - End conversation / Exit play mode");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Object Snapping");
    ImGui::Separator();
    ImGui::BulletText("C - Snap selected object to terrain surface");
    ImGui::BulletText("X - Snap horizontal edges (left/right/front/back)");
    ImGui::BulletText("Y - Snap vertical (stack on top/below)");
    ImGui::BulletText("Z - Full 3D surface alignment");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Terrain Editing");
    ImGui::Separator();
    ImGui::BulletText("Left Mouse - Apply brush (when tools enabled)");
    ImGui::BulletText("[ / ] - Decrease/Increase brush size");
    ImGui::BulletText("1-9 - Select brush mode");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "AI Nodes");
    ImGui::Separator();
    ImGui::BulletText("N - Drop AI node below camera");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Help");
    ImGui::Separator();
    ImGui::BulletText("F1 - Toggle this help window");

    ImGui::End();
}

void EditorUI::saveConfig(const std::string& filepath) {
    using json = nlohmann::json;

    json config;

    // Window visibility
    config["windows"]["terrainEditor"] = m_showTerrainEditor;
    config["windows"]["skySettings"] = m_showSkySettings;
    config["windows"]["waterSettings"] = m_showWaterSettings;
    config["windows"]["models"] = m_showModels;
    config["windows"]["terrainInfo"] = m_showTerrainInfo;
    config["windows"]["aiNodes"] = m_showAINodes;

    // Brush settings
    config["brush"]["mode"] = static_cast<int>(m_brushMode);
    config["brush"]["radius"] = m_brushRadius;
    config["brush"]["strength"] = m_brushStrength;
    config["brush"]["falloff"] = m_brushFalloff;

    // Selected texture
    config["texture"]["selected"] = m_selectedTexture;

    // Camera speed
    config["camera"]["speed"] = m_cameraSpeed;

    std::ofstream file(filepath);
    if (file.is_open()) {
        file << config.dump(2);
    }
}

void EditorUI::renderTechTreeWindow() {
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Tech Tree", &m_showTechTree)) {
        ImGui::End();
        return;
    }

    // Instructions
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "MMB Wheel: Zoom | MMB Drag: Pan | Click product to expand/collapse");
    ImGui::Separator();

    // Get the canvas region
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 50.0f) canvasSize.x = 50.0f;
    if (canvasSize.y < 50.0f) canvasSize.y = 50.0f;

    // Create an invisible button for the canvas to capture input
    ImGui::InvisibleButton("tech_tree_canvas", canvasSize,
        ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonLeft);

    bool isHovered = ImGui::IsItemHovered();
    bool isActive = ImGui::IsItemActive();
    ImVec2 mousePos = ImGui::GetIO().MousePos;

    // Handle zoom with mouse wheel
    if (isHovered) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float zoomDelta = wheel * 0.1f;
            m_techTreeZoom = std::clamp(m_techTreeZoom + zoomDelta, 0.3f, 3.0f);
        }
    }

    // Handle pan with middle mouse button
    if (isActive && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        m_techTreePan.x += delta.x;
        m_techTreePan.y += delta.y;
    }

    // Get draw list for custom rendering
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Clip to canvas
    drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

    // Background
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(30, 30, 40, 255));

    // Calculate start position with pan offset (final product at top)
    float centerX = canvasPos.x + canvasSize.x * 0.5f + m_techTreePan.x;
    float startY = canvasPos.y + 60.0f + m_techTreePan.y;  // Start near top

    // Node dimensions (scaled)
    float nodeW = 120.0f * m_techTreeZoom;
    float nodeH = 40.0f * m_techTreeZoom;
    float vSpacing = 80.0f * m_techTreeZoom;
    float hSpacing = 140.0f * m_techTreeZoom;

    // Colors
    ImU32 colExists = IM_COL32(50, 180, 80, 255);      // Green - exists
    ImU32 colMissing = IM_COL32(180, 60, 60, 255);     // Red - missing
    ImU32 colBorder = IM_COL32(100, 100, 120, 255);
    ImU32 colLine = IM_COL32(80, 80, 100, 255);
    ImU32 colText = IM_COL32(0, 0, 0, 255);            // Black text
    ImU32 colFinal = IM_COL32(255, 200, 50, 255);      // Gold for final product
    ImU32 colFinalHover = IM_COL32(255, 220, 100, 255); // Brighter gold on hover

    // Track if mouse is over final product for click detection
    bool finalProductHovered = false;
    ImVec2 finalProductPos;

    // Helper to draw a node (returns true if clicked)
    auto drawNode = [&](float x, float y, const char* name, bool exists, bool isFinal = false) -> ImVec2 {
        ImVec2 p0(x - nodeW * 0.5f, y - nodeH * 0.5f);
        ImVec2 p1(x + nodeW * 0.5f, y + nodeH * 0.5f);

        // Check if mouse is over this node
        bool nodeHovered = (mousePos.x >= p0.x && mousePos.x <= p1.x &&
                           mousePos.y >= p0.y && mousePos.y <= p1.y);

        ImU32 fillCol = exists ? colExists : colMissing;
        if (isFinal) {
            fillCol = nodeHovered ? colFinalHover : colFinal;
            if (nodeHovered) finalProductHovered = true;
        }

        drawList->AddRectFilled(p0, p1, fillCol, 5.0f * m_techTreeZoom);
        drawList->AddRect(p0, p1, colBorder, 5.0f * m_techTreeZoom, 0, 2.0f);

        // For final product, draw expand/collapse indicator
        if (isFinal) {
            float indicatorSize = 14.0f * m_techTreeZoom;
            ImVec2 indPos(p0.x + 5.0f * m_techTreeZoom, p0.y + (nodeH - indicatorSize) * 0.5f);

            // Draw box
            drawList->AddRectFilled(indPos, ImVec2(indPos.x + indicatorSize, indPos.y + indicatorSize),
                IM_COL32(40, 40, 50, 255), 2.0f);
            drawList->AddRect(indPos, ImVec2(indPos.x + indicatorSize, indPos.y + indicatorSize),
                colText, 2.0f);

            // Draw +/- symbol
            float midX = indPos.x + indicatorSize * 0.5f;
            float midY = indPos.y + indicatorSize * 0.5f;
            float lineLen = indicatorSize * 0.3f;

            // Horizontal line (always)
            drawList->AddLine(ImVec2(midX - lineLen, midY), ImVec2(midX + lineLen, midY), colText, 2.0f);

            // Vertical line (only if collapsed)
            if (!m_techTreeDeathsHeadExpanded) {
                drawList->AddLine(ImVec2(midX, midY - lineLen), ImVec2(midX, midY + lineLen), colText, 2.0f);
            }
        } else {
            // Regular checkbox indicator for non-final products
            float checkSize = 12.0f * m_techTreeZoom;
            ImVec2 checkPos(p0.x + 5.0f * m_techTreeZoom, p0.y + (nodeH - checkSize) * 0.5f);
            drawList->AddRect(checkPos, ImVec2(checkPos.x + checkSize, checkPos.y + checkSize), colText, 2.0f);
            if (exists) {
                drawList->AddLine(
                    ImVec2(checkPos.x + 2, checkPos.y + checkSize * 0.5f),
                    ImVec2(checkPos.x + checkSize * 0.4f, checkPos.y + checkSize - 2),
                    colText, 2.0f);
                drawList->AddLine(
                    ImVec2(checkPos.x + checkSize * 0.4f, checkPos.y + checkSize - 2),
                    ImVec2(checkPos.x + checkSize - 2, checkPos.y + 2),
                    colText, 2.0f);
            }
        }

        // Text (centered, accounting for indicator)
        ImVec2 textSize = ImGui::CalcTextSize(name);
        float textX = x - textSize.x * 0.5f + 10.0f * m_techTreeZoom;
        float textY = y - textSize.y * 0.5f;
        drawList->AddText(ImVec2(textX, textY), colText, name);

        return ImVec2(x, y);
    };

    // Helper to draw connection line (from parent above to child below)
    auto drawLine = [&](ImVec2 parent, ImVec2 child) {
        ImVec2 start(parent.x, parent.y + nodeH * 0.5f);  // Bottom of parent
        ImVec2 end(child.x, child.y - nodeH * 0.5f);      // Top of child
        drawList->AddLine(start, end, colLine, 2.0f);
    };

    // === DRAW THE TECH TREE ===
    // Layer 0 (TOP): Final Product - Death's Head
    float y0 = startY;
    ImVec2 deathsHead = drawNode(centerX, y0, "Death's Head", true, true);
    finalProductPos = deathsHead;

    // Handle click on final product to toggle expand/collapse
    if (finalProductHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_techTreeDeathsHeadExpanded = !m_techTreeDeathsHeadExpanded;
    }

    // Only draw the rest of the tree if expanded
    if (m_techTreeDeathsHeadExpanded) {
        // Layer 1: Direct components needed for moth
        float y1 = y0 + vSpacing;
        float layer1Width = hSpacing * 6;
        float layer1Start = centerX - layer1Width * 0.5f;

        ImVec2 ore = drawNode(layer1Start, y1, "ORE", true);
        ImVec2 sheetMetal = drawNode(layer1Start + hSpacing, y1, "SHEET METAL", true);
        ImVec2 compComp = drawNode(layer1Start + hSpacing * 2, y1, "COMP COMP", true);
        ImVec2 machParts = drawNode(layer1Start + hSpacing * 3, y1, "MACH PARTS", true);
        ImVec2 plastics = drawNode(layer1Start + hSpacing * 4, y1, "PLASTICS", true);
        ImVec2 cells = drawNode(layer1Start + hSpacing * 5, y1, "CELLS 1-3", true);
        ImVec2 laser = drawNode(layer1Start + hSpacing * 6, y1, "LASER", true);

        // Connect Death's Head to layer 1
        drawLine(deathsHead, ore);
        drawLine(deathsHead, sheetMetal);
        drawLine(deathsHead, compComp);
        drawLine(deathsHead, machParts);
        drawLine(deathsHead, plastics);
        drawLine(deathsHead, cells);
        drawLine(deathsHead, laser);

        // Layer 2: Components needed for layer 1 items
        float y2 = y1 + vSpacing;

        // Sheet Metal needs ORE
        ImVec2 oreForSheet = drawNode(layer1Start + hSpacing * 0.5f, y2, "ORE", true);
        drawLine(sheetMetal, oreForSheet);

        // Comp/Mach Parts need various things
        float compStart = layer1Start + hSpacing * 1.5f;
        ImVec2 pureWater = drawNode(compStart, y2, "PURE WATER", true);
        ImVec2 chemicals = drawNode(compStart + hSpacing * 0.8f, y2, "CHEMICALS", true);
        ImVec2 gems = drawNode(compStart + hSpacing * 1.6f, y2, "GEMS", true);
        ImVec2 exMetal = drawNode(compStart + hSpacing * 2.4f, y2, "EX METAL", true);

        drawLine(compComp, pureWater);
        drawLine(compComp, chemicals);
        drawLine(compComp, gems);
        drawLine(compComp, exMetal);
        drawLine(machParts, chemicals);
        drawLine(machParts, exMetal);

        // Layer 3: Raw resources for chemicals and plastics
        float y3 = y2 + vSpacing;

        ImVec2 oil = drawNode(compStart + hSpacing * 0.3f, y3, "OIL", true);
        ImVec2 natGas = drawNode(compStart + hSpacing * 1.1f, y3, "NATURAL GAS", true);
        ImVec2 sulfur = drawNode(compStart + hSpacing * 1.9f, y3, "SULFUR", true);
        ImVec2 phosphates = drawNode(compStart + hSpacing * 2.7f, y3, "PHOSPHATES", true);

        drawLine(chemicals, oil);
        drawLine(chemicals, natGas);
        drawLine(chemicals, sulfur);
        drawLine(plastics, oil);
        drawLine(plastics, natGas);

        // Layer 4: Base resources (from rigs/wells/mines)
        float y4 = y3 + vSpacing;

        ImVec2 oilRig = drawNode(compStart, y4, "Oil Rigs", true);
        ImVec2 gasWell = drawNode(compStart + hSpacing, y4, "Gas Wells", true);
        ImVec2 mines = drawNode(compStart + hSpacing * 2, y4, "Mines", true);

        drawLine(oil, oilRig);
        drawLine(natGas, gasWell);
        drawLine(sulfur, mines);
        drawLine(phosphates, mines);
    }

    // Pop clip rect
    drawList->PopClipRect();

    // Show current zoom level and expand state
    ImGui::SetCursorScreenPos(ImVec2(canvasPos.x + 10, canvasPos.y + canvasSize.y - 25));
    ImGui::Text("Zoom: %.0f%% | %s", m_techTreeZoom * 100.0f,
        m_techTreeDeathsHeadExpanded ? "Click product to collapse" : "Click product to expand");

    ImGui::End();
}

void EditorUI::loadConfig(const std::string& filepath) {
    using json = nlohmann::json;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        return;  // No config file yet, use defaults
    }

    try {
        json config = json::parse(file);

        // Window visibility
        if (config.contains("windows")) {
            const auto& w = config["windows"];
            m_showTerrainEditor = w.value("terrainEditor", true);
            m_showSkySettings = w.value("skySettings", true);
            m_showWaterSettings = w.value("waterSettings", true);
            m_showModels = w.value("models", true);
            m_showTerrainInfo = w.value("terrainInfo", true);
            m_showAINodes = w.value("aiNodes", true);
        }

        // Brush settings
        if (config.contains("brush")) {
            const auto& b = config["brush"];
            m_brushMode = static_cast<BrushMode>(b.value("mode", 0));
            m_brushRadius = b.value("radius", 15.0f);
            m_brushStrength = b.value("strength", 20.0f);
            m_brushFalloff = b.value("falloff", 0.5f);
        }

        // Selected texture
        if (config.contains("texture")) {
            m_selectedTexture = config["texture"].value("selected", 1);
        }

        // Camera speed
        if (config.contains("camera")) {
            m_cameraSpeed = config["camera"].value("speed", 15.0f);
        }
    } catch (...) {
        // Ignore parse errors, use defaults
    }
}

void EditorUI::setGroveSource(const std::string& source) {
    size_t len = std::min(source.size(), sizeof(m_groveSource) - 1);
    memcpy(m_groveSource, source.c_str(), len);
    m_groveSource[len] = '\0';
    m_groveModified = false;
}

void EditorUI::renderGroveEditor() {
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Grove Script Editor", &m_showGroveEditor)) {
        ImGui::End();
        return;
    }

    // Header: logo + title
    if (m_groveLogoDescriptor) {
        ImGui::Image((ImTextureID)m_groveLogoDescriptor, ImVec2(32, 32));
        ImGui::SameLine();
    }
    ImGui::Text("Grove Scripting Language");
    ImGui::Separator();

    // File toolbar
    if (ImGui::Button("New")) {
        m_groveSource[0] = '\0';
        m_groveCurrentFile.clear();
        m_groveModified = false;
        m_groveOutput.clear();
        m_groveHasError = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Open...")) {
        if (m_onGroveOpen) m_onGroveOpen();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        if (m_groveCurrentFile.empty()) {
            if (m_onGroveSaveAs) m_onGroveSaveAs(std::string(m_groveSource));
        } else {
            if (m_onGroveSave) m_onGroveSave(std::string(m_groveSource), m_groveCurrentFile);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As...")) {
        if (m_onGroveSaveAs) m_onGroveSaveAs(std::string(m_groveSource));
    }
    ImGui::SameLine(0, 20);
    if (ImGui::Button("Run")) {
        if (m_onGroveRun) {
            m_onGroveRun(std::string(m_groveSource));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Output")) {
        m_groveOutput.clear();
        m_groveHasError = false;
    }

    // Current file display
    if (!m_groveCurrentFile.empty()) {
        // Show just the filename
        std::string filename = m_groveCurrentFile;
        size_t slash = filename.find_last_of("/\\");
        if (slash != std::string::npos) filename = filename.substr(slash + 1);
        ImGui::TextDisabled("%s%s", filename.c_str(), m_groveModified ? " *" : "");
    } else {
        ImGui::TextDisabled("(unsaved)%s", m_groveModified ? " *" : "");
    }

    // Script file list (collapsible)
    if (m_onGroveFileList) {
        if (ImGui::TreeNode("Scripts")) {
            auto files = m_onGroveFileList();
            for (const auto& f : files) {
                std::string name = f;
                size_t slash = name.find_last_of("/\\");
                if (slash != std::string::npos) name = name.substr(slash + 1);

                bool isCurrent = (f == m_groveCurrentFile);
                if (isCurrent) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                if (ImGui::Selectable(name.c_str(), isCurrent)) {
                    // Load this file via the open callback path
                    // Read file directly
                    std::ifstream inFile(f);
                    if (inFile.is_open()) {
                        std::string content((std::istreambuf_iterator<char>(inFile)),
                                             std::istreambuf_iterator<char>());
                        setGroveSource(content);
                        m_groveCurrentFile = f;
                        m_groveOutput.clear();
                        m_groveHasError = false;
                    }
                }
                if (isCurrent) ImGui::PopStyleColor();
            }
            ImGui::TreePop();
        }
    }

    ImGui::Separator();

    // Code editor
    float outputHeight = 100.0f;
    ImGui::InputTextMultiline("##grove_source", m_groveSource, sizeof(m_groveSource),
        ImVec2(-1, ImGui::GetContentRegionAvail().y - outputHeight),
        ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackEdit,
        [](ImGuiInputTextCallbackData* data) -> int {
            auto* self = static_cast<EditorUI*>(data->UserData);
            self->m_groveModified = true;
            return 0;
        }, this);

    ImGui::Separator();

    // Output console
    ImGui::Text("Output:");
    if (m_groveHasError) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        if (m_groveErrorLine > 0) {
            ImGui::TextWrapped("Error (line %d): %s", m_groveErrorLine, m_groveOutput.c_str());
        } else {
            ImGui::TextWrapped("Error: %s", m_groveOutput.c_str());
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::TextWrapped("%s", m_groveOutput.c_str());
    }

    ImGui::End();
}

void EditorUI::renderZonesWindow() {
    ImGui::SetNextWindowSize(ImVec2(320, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Zones", &m_showZones)) {
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Show Zone Overlay", &m_showZoneOverlay);
    ImGui::Separator();

    // Zone info at cursor
    if (m_zoneSystem && m_hasBrushPos) {
        const ZoneCell* cell = m_zoneSystem->getCell(m_brushPos.x, m_brushPos.z);
        if (cell) {
            glm::ivec2 g = m_zoneSystem->worldToGrid(m_brushPos.x, m_brushPos.z);
            ImGui::Text("Grid: %d, %d", g.x, g.y);
            ImGui::Text("Type: %s", ZoneSystem::zoneTypeName(cell->type));
            if (cell->resource != ResourceType::None) {
                ImGui::Text("Resource: %s (%.0f%%)", ZoneSystem::resourceTypeName(cell->resource),
                            cell->resourceDensity * 100.0f);
            }
            if (cell->ownerPlayerId != 0) {
                ImGui::Text("Owner: Player %u", cell->ownerPlayerId);
            }
            ImGui::Text("Price: $%.0f", cell->purchasePrice);
        } else {
            ImGui::TextDisabled("Out of bounds");
        }
    } else if (!m_zoneSystem) {
        ImGui::TextDisabled("No zone system");
    } else {
        ImGui::TextDisabled("Move cursor over terrain");
    }

    ImGui::Separator();
    ImGui::Text("Zone Painting");
    ImGui::Checkbox("Paint Mode", &m_zonePaintMode);

    if (m_zonePaintMode) {
        const char* zoneNames[] = { "Wilderness", "Battlefield", "Spawn/Safe",
                                    "Residential", "Commercial", "Industrial", "Resource" };
        ImGui::Combo("Zone Type", &m_zonePaintType, zoneNames, 7);

        if (m_zonePaintType == 6) { // Resource
            const char* resNames[] = { "None", "Wood", "Limestone", "Iron", "Oil" };
            ImGui::Combo("Resource", &m_zonePaintResource, resNames, 5);
            ImGui::SliderFloat("Density", &m_zonePaintDensity, 0.1f, 1.0f);
        }

        ImGui::TextWrapped("Left-click on terrain to paint zones");
    }

    ImGui::End();
}

void EditorUI::updateSpatialGrid(const nlohmann::json& data) {
    using json = nlohmann::json;

    m_spatialGrid = SpatialGrid{};

    // Parse grid
    if (data.contains("grid") && data["grid"].is_object()) {
        auto& g = data["grid"];
        m_spatialGrid.cellSize = g.value("cell_size", 2.0f);
        m_spatialGrid.originX = g.value("origin_x", 0.0f);
        m_spatialGrid.originZ = g.value("origin_z", 0.0f);
        m_spatialGrid.width = g.value("width", 0);
        m_spatialGrid.height = g.value("height", 0);

        if (g.contains("cells") && g["cells"].is_array()) {
            m_spatialGrid.cells.clear();
            for (auto& row : g["cells"]) {
                std::vector<int> r;
                if (row.is_array()) {
                    for (auto& cell : row) {
                        r.push_back(cell.get<int>());
                    }
                }
                m_spatialGrid.cells.push_back(r);
            }
        }
    }

    // Parse structures
    if (data.contains("structures") && data["structures"].is_array()) {
        for (auto& s : data["structures"]) {
            SpatialGrid::StructureInfo info;
            info.type = s.value("type", "");
            if (s.contains("bbox")) {
                info.minX = s["bbox"].value("min_x", 0.0f);
                info.maxX = s["bbox"].value("max_x", 0.0f);
                info.minZ = s["bbox"].value("min_z", 0.0f);
                info.maxZ = s["bbox"].value("max_z", 0.0f);
            }
            if (s.contains("dimensions") && s["dimensions"].is_array() && s["dimensions"].size() >= 2) {
                info.dimX = s["dimensions"][0].get<float>();
                info.dimZ = s["dimensions"][1].get<float>();
            }
            info.npcInside = s.value("npc_inside", false);
            info.panelCount = s.value("panel_count", 0);
            info.label = s.value("label", "");

            if (s.contains("doors") && s["doors"].is_array()) {
                for (auto& d : s["doors"]) {
                    SpatialGrid::StructureInfo::DoorInfo di;
                    di.x = d.value("x", 0.0f);
                    di.z = d.value("z", 0.0f);
                    di.face = d.value("face", "");
                    info.doors.push_back(di);
                }
            }

            m_spatialGrid.structures.push_back(info);
        }
    }
}

void EditorUI::renderMindMapWindow() {
    ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("AI Mind Map", &m_showMindMap)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Unit 42's Spatial Model");
    ImGui::Separator();

    if (m_spatialGrid.width == 0 || m_spatialGrid.height == 0 || m_spatialGrid.cells.empty()) {
        ImGui::TextWrapped("No spatial data yet. The AI needs to perceive walls/doors/panels to build a spatial model.");
        ImGui::End();
        return;
    }

    // Structure info
    for (auto& s : m_spatialGrid.structures) {
        if (s.type == "rectangular_enclosure") {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                "Enclosure: %.0fm x %.0fm, %d panels%s",
                s.dimX, s.dimZ, s.panelCount,
                s.npcInside ? " (NPC inside)" : "");
            for (auto& d : s.doors) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.8f, 1.0f), "[Door: %s]", d.face.c_str());
            }
        } else if (s.type == "wall_line") {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s (%d panels)",
                s.label.c_str(), s.panelCount);
        }
    }

    ImGui::Separator();

    // Zoom control
    ImGui::SliderFloat("Zoom", &m_mindMapZoom, 0.5f, 3.0f, "%.1fx");

    float cellPx = 16.0f * m_mindMapZoom;

    // Draw grid using ImGui draw list
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize(m_spatialGrid.width * cellPx, m_spatialGrid.height * cellPx);

    // Scrollable child region
    ImGui::BeginChild("MindMapGrid", ImVec2(0, 0), true,
        ImGuiWindowFlags_HorizontalScrollbar);

    ImVec2 gridPos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Background
    drawList->AddRectFilled(gridPos,
        ImVec2(gridPos.x + canvasSize.x, gridPos.y + canvasSize.y),
        IM_COL32(20, 20, 30, 255));

    // Draw cells
    for (int z = 0; z < m_spatialGrid.height && z < (int)m_spatialGrid.cells.size(); z++) {
        for (int x = 0; x < m_spatialGrid.width && x < (int)m_spatialGrid.cells[z].size(); x++) {
            int val = m_spatialGrid.cells[z][x];
            if (val == 0) continue;

            ImVec2 pMin(gridPos.x + x * cellPx, gridPos.y + z * cellPx);
            ImVec2 pMax(pMin.x + cellPx - 1, pMin.y + cellPx - 1);

            ImU32 color;
            switch (val) {
                case 1: color = IM_COL32(200, 200, 200, 255); break;  // Wall - white/gray
                case 2: color = IM_COL32(50, 220, 50, 255); break;    // Door - green
                case 3: color = IM_COL32(50, 220, 220, 255); break;   // NPC - cyan
                case 4: color = IM_COL32(255, 220, 50, 255); break;   // Player - yellow
                default: color = IM_COL32(100, 100, 100, 255); break;
            }

            if (val == 3 || val == 4) {
                // NPC and player as circles
                ImVec2 center((pMin.x + pMax.x) * 0.5f, (pMin.y + pMax.y) * 0.5f);
                drawList->AddCircleFilled(center, cellPx * 0.35f, color);
            } else {
                drawList->AddRectFilled(pMin, pMax, color);
            }
        }
    }

    // Draw structure bounding boxes as outlines
    for (auto& s : m_spatialGrid.structures) {
        float bx0 = (s.minX - m_spatialGrid.originX) / m_spatialGrid.cellSize * cellPx;
        float bz0 = (s.minZ - m_spatialGrid.originZ) / m_spatialGrid.cellSize * cellPx;
        float bx1 = (s.maxX - m_spatialGrid.originX) / m_spatialGrid.cellSize * cellPx;
        float bz1 = (s.maxZ - m_spatialGrid.originZ) / m_spatialGrid.cellSize * cellPx;

        ImU32 outlineColor = (s.type == "rectangular_enclosure")
            ? IM_COL32(100, 255, 100, 120) : IM_COL32(150, 150, 150, 100);

        drawList->AddRect(
            ImVec2(gridPos.x + bx0, gridPos.y + bz0),
            ImVec2(gridPos.x + bx1, gridPos.y + bz1),
            outlineColor, 0.0f, 0, 1.5f);

        // Label
        std::string label;
        if (s.type == "rectangular_enclosure") {
            label = std::to_string((int)s.dimX) + "x" + std::to_string((int)s.dimZ) + "m";
        } else if (!s.label.empty()) {
            label = s.label;
        }
        if (!label.empty()) {
            drawList->AddText(
                ImVec2(gridPos.x + bx0 + 2, gridPos.y + bz0 - 14),
                IM_COL32(200, 255, 200, 200), label.c_str());
        }
    }

    // Grid lines (subtle)
    for (int x = 0; x <= m_spatialGrid.width; x++) {
        drawList->AddLine(
            ImVec2(gridPos.x + x * cellPx, gridPos.y),
            ImVec2(gridPos.x + x * cellPx, gridPos.y + canvasSize.y),
            IM_COL32(40, 40, 50, 100));
    }
    for (int z = 0; z <= m_spatialGrid.height; z++) {
        drawList->AddLine(
            ImVec2(gridPos.x, gridPos.y + z * cellPx),
            ImVec2(gridPos.x + canvasSize.x, gridPos.y + z * cellPx),
            IM_COL32(40, 40, 50, 100));
    }

    // Dummy to make child scrollable to full grid size
    ImGui::Dummy(canvasSize);

    // Handle scroll-wheel zoom
    if (ImGui::IsWindowHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            m_mindMapZoom = std::clamp(m_mindMapZoom + wheel * 0.15f, 0.5f, 3.0f);
        }
    }

    ImGui::EndChild();

    // Legend
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Wall");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.2f, 0.86f, 0.2f, 1.0f), "Door");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.2f, 0.86f, 0.86f, 1.0f), "NPC");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.86f, 0.2f, 1.0f), "Player");

    ImGui::End();
}

void EditorUI::renderBuildingTextureWindow() {
    ImGui::SetNextWindowSize(ImVec2(280, 350), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Building Textures", &m_showBuildingTextures)) {
        ImGui::End();
        return;
    }

    if (m_buildingTextures.empty()) {
        ImGui::TextWrapped("No textures found. Place PNG/JPG files in textures/building/");
        ImGui::End();
        return;
    }

    ImGui::Text("Select a texture swatch:");
    ImGui::Separator();

    // Render texture grid (64x64 thumbnails)
    const float thumbSize = 64.0f;
    float windowWidth = ImGui::GetContentRegionAvail().x;
    int columns = std::max(1, static_cast<int>(windowWidth / (thumbSize + 8.0f)));

    for (int i = 0; i < static_cast<int>(m_buildingTextures.size()); i++) {
        if (i % columns != 0) ImGui::SameLine();

        ImGui::PushID(i);
        bool selected = (m_selectedBuildingTexture == i);

        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.9f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 1.0f, 1.0f));
        }

        if (m_buildingTextures[i].descriptor) {
            if (ImGui::ImageButton("##swatch", (ImTextureID)m_buildingTextures[i].descriptor,
                                   ImVec2(thumbSize, thumbSize))) {
                m_selectedBuildingTexture = i;
            }
        } else {
            if (ImGui::Button(m_buildingTextures[i].name.c_str(), ImVec2(thumbSize, thumbSize))) {
                m_selectedBuildingTexture = i;
            }
        }

        if (selected) {
            ImGui::PopStyleColor(2);
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s (%dx%d)", m_buildingTextures[i].name.c_str(),
                              m_buildingTextures[i].width, m_buildingTextures[i].height);
        }

        ImGui::PopID();
    }

    ImGui::Separator();

    // Show selected texture name
    if (m_selectedBuildingTexture >= 0 && m_selectedBuildingTexture < static_cast<int>(m_buildingTextures.size())) {
        ImGui::Text("Selected: %s", m_buildingTextures[m_selectedBuildingTexture].name.c_str());
    } else {
        ImGui::TextDisabled("No texture selected");
    }

    // UV scale controls
    ImGui::Separator();
    ImGui::Text("Texture Scale:");
    ImGui::DragFloat("U Scale", &m_buildingTexScaleU, 0.05f, 0.1f, 20.0f, "%.2f");
    ImGui::DragFloat("V Scale", &m_buildingTexScaleV, 0.05f, 0.1f, 20.0f, "%.2f");
    if (ImGui::Button("Reset Scale")) {
        m_buildingTexScaleU = 1.0f;
        m_buildingTexScaleV = 1.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Lock UV")) {
        m_buildingTexScaleV = m_buildingTexScaleU;
    }

    ImGui::Separator();

    // Apply button — supports face selection (Alt+click) or single object selection
    bool hasFaceSelection = !m_faceSelectedIndices.empty() && m_selectedBuildingTexture >= 0;
    bool hasSingleSelection = m_selectedBuildingTexture >= 0 &&
                              m_selectedObjectIndex >= 0 &&
                              m_selectedObjectIndex < static_cast<int>(m_sceneObjects.size());

    if (hasFaceSelection) {
        std::string label = "Apply to " + std::to_string(m_faceSelectedIndices.size()) + " face-selected blocks";
        if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) {
            if (m_onApplyFaceTexture) {
                m_onApplyFaceTexture(m_selectedBuildingTexture,
                                     m_buildingTexScaleU, m_buildingTexScaleV);
            }
        }
    } else if (hasSingleSelection) {
        const auto& objName = m_sceneObjects[m_selectedObjectIndex]->getName();
        bool isBuildingPart = (objName.find("Building_") == 0 || objName.find("Foundation_") == 0);
        if (isBuildingPart) {
            if (ImGui::Button("Apply to Selected", ImVec2(-1, 0))) {
                if (m_onApplyBuildingTexture) {
                    m_onApplyBuildingTexture(m_sceneObjects[m_selectedObjectIndex],
                                            m_selectedBuildingTexture,
                                            m_buildingTexScaleU, m_buildingTexScaleV);
                }
            }
        } else {
            ImGui::TextDisabled("Select a building part to apply");
        }
    } else {
        ImGui::TextDisabled("Select a building part to apply");
    }

    ImGui::End();
}

} // namespace eden
