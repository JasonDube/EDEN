// ModelingMode_UV.cpp - UV Editor functions for ModelingMode
// Split from ModelingMode.cpp for better organization

#include "ModelingMode.hpp"
#include "Editor/GLBLoader.hpp"
#include "Renderer/Swapchain.hpp"

#include <imgui.h>
#include <nfd.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>

#include <iostream>
#include <queue>
#include <filesystem>
#include <limits>
#include <random>
#include <algorithm>
#include <cmath>

using namespace eden;

// Helper to check if a vertex is on a UV seam (boundary edge)
static bool isSeamVertex(const EditableMesh& mesh, uint32_t vertIdx) {
    auto edges = mesh.getVertexEdges(vertIdx);
    for (uint32_t heIdx : edges) {
        const HalfEdge& he = mesh.getHalfEdge(heIdx);
        if (he.twinIndex == UINT32_MAX) {
            return true;
        }
    }
    return false;
}

void ModelingMode::renderModelingUVWindow() {
    // Use Appearing so window shows up properly after being re-enabled from menu
    ImGui::SetNextWindowSize(ImVec2(420, 520), ImGuiCond_Appearing);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (m_ctx.uvDraggingSelection || m_ctx.uvScaling || m_ctx.uvRotating || m_ctx.uvChildHovered) {
        windowFlags |= ImGuiWindowFlags_NoMove;
    }

    if (ImGui::Begin("UV Editor", &m_ctx.showUVWindow, windowFlags)) {
        if (!m_ctx.editableMesh.isValid()) {
            ImGui::Text("No mesh loaded. Load or create a model first.");
            ImGui::End();
            return;
        }

        // Selection mode toggle: 0=Island, 1=Face, 2=Edge, 3=Vertex
        ImGui::Text("Select:");
        ImGui::SameLine();
        if (ImGui::RadioButton("Island", m_ctx.uvSelectionMode == 0)) {
            m_ctx.uvSelectionMode = 0;
            m_ctx.uvEdgeSelectionMode = false;
            clearUVEdgeSelection();
            m_ctx.uvSelectedVertices.clear();
            std::cout << "[UV] Mode changed to: Island" << std::endl;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Face", m_ctx.uvSelectionMode == 1)) {
            m_ctx.uvSelectionMode = 1;
            m_ctx.uvEdgeSelectionMode = false;
            clearUVEdgeSelection();
            m_ctx.uvSelectedVertices.clear();
            std::cout << "[UV] Mode changed to: Face" << std::endl;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Edge", m_ctx.uvSelectionMode == 2)) {
            m_ctx.uvSelectionMode = 2;
            m_ctx.uvEdgeSelectionMode = true;
            m_ctx.uvSelectedFaces.clear();
            m_ctx.uvSelectedVertices.clear();
            std::cout << "[UV] Mode changed to: Edge" << std::endl;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Vertex", m_ctx.uvSelectionMode == 3)) {
            m_ctx.uvSelectionMode = 3;
            m_ctx.uvEdgeSelectionMode = false;
            m_ctx.uvSelectedFaces.clear();
            clearUVEdgeSelection();
            std::cout << "[UV] Mode changed to: Vertex" << std::endl;
        }

        // Sew/Unsew buttons (edge mode only, mode 2)
        if (m_ctx.uvSelectionMode == 2 && m_ctx.uvSelectedEdge.first != UINT32_MAX) {
            ImGui::SameLine();
            ImGui::Spacing();
            ImGui::SameLine();

            bool canSew = !m_ctx.uvTwinEdges.empty();
            if (!canSew) ImGui::BeginDisabled();
            if (ImGui::Button("Sew")) {
                std::cout << "[UV] Sew button clicked (face " << m_ctx.uvSelectedEdge.first << ", edge " << m_ctx.uvSelectedEdge.second << ")" << std::endl;
                sewSelectedEdge();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Snap twin edge to selected edge (W)");
            }
            if (!canSew) ImGui::EndDisabled();

            ImGui::SameLine();
            if (!canSew) ImGui::BeginDisabled();
            if (ImGui::Button("Move+Sew")) {
                std::cout << "[UV] Move+Sew button clicked (face " << m_ctx.uvSelectedEdge.first << ", edge " << m_ctx.uvSelectedEdge.second << ")" << std::endl;
                moveAndSewSelectedEdge();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Move & rotate twin island to align, then sew (Shift+W)");
            }
            if (!canSew) ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Unsew")) {
                std::cout << "[UV] Unsew button clicked (face " << m_ctx.uvSelectedEdge.first << ", edge " << m_ctx.uvSelectedEdge.second << ")" << std::endl;
                unsewSelectedEdge();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Separate sewn edge (U)");
            }
        }

        // Sew Vertices button (vertex mode only, mode 3)
        if (m_ctx.uvSelectionMode == 3 && m_ctx.uvSelectedVertices.size() >= 2) {
            ImGui::SameLine();
            ImGui::Spacing();
            ImGui::SameLine();

            if (ImGui::Button("Sew Verts")) {
                // Calculate midpoint of all selected vertices
                glm::vec2 midpoint(0.0f);
                for (uint32_t vertIdx : m_ctx.uvSelectedVertices) {
                    midpoint += m_ctx.editableMesh.getVertex(vertIdx).uv;
                }
                midpoint /= static_cast<float>(m_ctx.uvSelectedVertices.size());

                // Move all selected vertices to midpoint
                m_ctx.editableMesh.saveState();
                for (uint32_t vertIdx : m_ctx.uvSelectedVertices) {
                    m_ctx.editableMesh.getVertex(vertIdx).uv = midpoint;
                }
                m_ctx.meshDirty = true;

                std::cout << "[UV] Sewn " << m_ctx.uvSelectedVertices.size()
                          << " vertices to midpoint (" << midpoint.x << ", " << midpoint.y << ")" << std::endl;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Move selected vertices to their midpoint");
            }
        }

        ImGui::SameLine();
        ImGui::ColorEdit3("##Wireframe", &m_ctx.uvWireframeColor.x, ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        if (ImGui::Button("Fit All")) {
            // Calculate UV bounds
            glm::vec2 uvMin(FLT_MAX), uvMax(-FLT_MAX);
            for (uint32_t i = 0; i < m_ctx.editableMesh.getVertexCount(); ++i) {
                glm::vec2 uv = m_ctx.editableMesh.getVertex(i).uv;
                uvMin = glm::min(uvMin, uv);
                uvMax = glm::max(uvMax, uv);
            }

            if (uvMin.x < FLT_MAX && uvMax.x > -FLT_MAX) {
                glm::vec2 uvRange = uvMax - uvMin;
                float maxRange = std::max(uvRange.x, uvRange.y);

                if (maxRange > 0.001f) {
                    // Set zoom to fit UVs with some padding
                    m_ctx.uvZoom = 0.9f / maxRange;
                    m_ctx.uvZoom = std::clamp(m_ctx.uvZoom, 0.1f, 16.0f);

                    // Center the view on UV center
                    glm::vec2 uvCenter = (uvMin + uvMax) * 0.5f;
                    m_ctx.uvPan = glm::vec2(0.5f - uvCenter.x, uvCenter.y - 0.5f);

                    std::cout << "[UV] Fit view: bounds (" << uvMin.x << "," << uvMin.y
                              << ") to (" << uvMax.x << "," << uvMax.y << "), zoom=" << m_ctx.uvZoom << std::endl;
                }
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Fit view to show all UVs");
        }
        ImGui::SameLine();
        bool canBake = m_ctx.selectedObject && m_ctx.selectedObject->hasTextureData();
        if (!canBake) ImGui::BeginDisabled();
        if (ImGui::Button("Bake Edges")) {
            // Use the wireframe color for the baked edges
            bakeUVEdgesToTexture(m_ctx.uvWireframeColor, 1);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Draw UV edges onto texture (uses wireframe color)");
        }
        if (!canBake) ImGui::EndDisabled();
        ImGui::Separator();

        ImVec2 available = ImGui::GetContentRegionAvail();
        float viewWidth = available.x;
        float viewHeight = available.y - 30;  // Leave room for status text
        if (viewWidth < 64) viewWidth = 64;
        if (viewHeight < 64) viewHeight = 64;

        ImGui::BeginChild("UVViewModeling", ImVec2(viewWidth, viewHeight), ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        bool isHovered = ImGui::IsWindowHovered();

        m_ctx.uvChildHovered = isHovered;

        // Use the smaller dimension as the base UV space size to maintain aspect ratio
        float uvBaseSize = std::min(viewWidth, viewHeight);
        float texSize = uvBaseSize * m_ctx.uvZoom;
        float offsetX = canvasPos.x + (viewWidth - texSize) * 0.5f + m_ctx.uvPan.x * texSize;
        float offsetY = canvasPos.y + (viewHeight - texSize) * 0.5f + m_ctx.uvPan.y * texSize;

        auto screenToUV = [&](const ImVec2& screen) -> glm::vec2 {
            float u = (screen.x - offsetX) / texSize;
            float v = 1.0f - (screen.y - offsetY) / texSize;
            return glm::vec2(u, v);
        };

        // Handle zoom/pan
        if (isHovered) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0) {
                float oldZoom = m_ctx.uvZoom;
                float zoomDelta = wheel * 0.2f * m_ctx.uvZoom;
                m_ctx.uvZoom = std::clamp(m_ctx.uvZoom + zoomDelta, 0.25f, 8.0f);
                std::cout << "[UV] Zoom: " << oldZoom << " -> " << m_ctx.uvZoom << std::endl;
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
                m_ctx.uvPanning = true;
                m_ctx.uvPanStart = glm::vec2(mousePos.x, mousePos.y);
                std::cout << "[UV] Started panning" << std::endl;
            }

            // Hotkeys synchronized with 3D viewport (Q, W, E, R)
            // Q - Select mode (no gizmo)
            if (Input::isKeyPressed(Input::KEY_Q)) {
                m_ctx.gizmoMode = GizmoMode::None;
            }
            // W - Move mode
            if (Input::isKeyPressed(Input::KEY_W)) {
                m_ctx.gizmoMode = GizmoMode::Move;
            }
            // E - Rotate mode
            if (Input::isKeyPressed(Input::KEY_E)) {
                m_ctx.gizmoMode = GizmoMode::Rotate;
            }
            // R - Scale mode
            if (Input::isKeyPressed(Input::KEY_R)) {
                m_ctx.gizmoMode = GizmoMode::Scale;
            }
            // A - Vertex mode (uvSelectionMode 3, modelingSelectionMode Vertex)
            if (Input::isKeyPressed(Input::KEY_A) && !Input::isKeyDown(Input::KEY_LEFT_CONTROL)) {
                m_ctx.uvSelectionMode = 3;
                m_ctx.uvEdgeSelectionMode = false;
                m_ctx.modelingSelectionMode = ModelingSelectionMode::Vertex;
                m_ctx.editableMesh.clearSelection();
                m_ctx.uvSelectedFaces.clear();
                clearUVEdgeSelection();
            }
            // S - Edge mode (uvSelectionMode 2, modelingSelectionMode Edge)
            if (Input::isKeyPressed(Input::KEY_S) && !Input::isKeyDown(Input::KEY_LEFT_CONTROL)) {
                m_ctx.uvSelectionMode = 2;
                m_ctx.uvEdgeSelectionMode = true;
                m_ctx.modelingSelectionMode = ModelingSelectionMode::Edge;
                m_ctx.editableMesh.clearSelection();
                m_ctx.uvSelectedFaces.clear();
                m_ctx.uvSelectedVertices.clear();
            }
            // D - Face mode (uvSelectionMode 1, modelingSelectionMode Face)
            if (Input::isKeyPressed(Input::KEY_D)) {
                m_ctx.uvSelectionMode = 1;
                m_ctx.uvEdgeSelectionMode = false;
                m_ctx.modelingSelectionMode = ModelingSelectionMode::Face;
                m_ctx.editableMesh.clearSelection();
                clearUVEdgeSelection();
                m_ctx.uvSelectedVertices.clear();
            }
        }

        if (m_ctx.uvPanning) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                glm::vec2 newPos(mousePos.x, mousePos.y);
                glm::vec2 delta = newPos - m_ctx.uvPanStart;
                m_ctx.uvPan += delta / texSize;
                m_ctx.uvPanStart = newPos;
            } else {
                m_ctx.uvPanning = false;
                std::cout << "[UV] Stopped panning. Pan offset: (" << m_ctx.uvPan.x << ", " << m_ctx.uvPan.y << ")" << std::endl;
            }
        }

        // Handle UV painting when paint mode is active (same toggle as 3D painting)
        // Skip painting when Alt is held (Alt+click is for color sampling)
        // Note: these statics are used both for painting and for line preview drawing
        static bool uvPaintingLastFrame = false;
        static glm::vec2 lastUVPaintPos(0.0f);
        static bool hasLastUVPaintPos = false;

        // Store screen conversion params for line preview (used later in drawing)
        float previewTexSize = texSize;
        float previewOffsetX = offsetX;
        float previewOffsetY = offsetY;

        if (m_ctx.isPainting && m_ctx.selectedObject && isHovered && !m_ctx.uvPanning && !ImGui::GetIO().KeyAlt) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                glm::vec2 paintUV = screenToUV(mousePos);

                // Save texture state at start of stroke
                if (!uvPaintingLastFrame) {
                    m_ctx.selectedObject->saveTextureState();
                }
                uvPaintingLastFrame = true;

                // Check if UV is within valid range (0-1)
                if (paintUV.x >= 0.0f && paintUV.x <= 1.0f && paintUV.y >= 0.0f && paintUV.y <= 1.0f) {
                    bool shiftHeld = ImGui::GetIO().KeyShift;

                    // Shift+Click: Draw line from last position
                    if (shiftHeld && hasLastUVPaintPos && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        // Draw line from last paint position to current position
                        glm::vec2 startUV = lastUVPaintPos;
                        glm::vec2 endUV = paintUV;

                        // Calculate distance and steps for smooth coverage
                        float distance = glm::length(endUV - startUV);
                        float stepSize = m_ctx.paintRadius * 0.5f;
                        int steps = static_cast<int>(distance / stepSize) + 1;
                        if (steps < 2) steps = 2;

                        // Interpolate and paint along the line
                        for (int i = 0; i <= steps; i++) {
                            float t = static_cast<float>(i) / static_cast<float>(steps);
                            glm::vec2 uv = glm::mix(startUV, endUV, t);
                            m_ctx.selectedObject->paintAt(uv, m_ctx.paintColor, m_ctx.paintRadius, m_ctx.paintStrength, m_ctx.squareBrush);
                        }
                    } else {
                        // Normal paint
                        m_ctx.selectedObject->paintAt(paintUV, m_ctx.paintColor, m_ctx.paintRadius, m_ctx.paintStrength, m_ctx.squareBrush);
                    }

                    // Store position for line tool
                    lastUVPaintPos = paintUV;
                    hasLastUVPaintPos = true;

                    m_ctx.selectedObject->markTextureModified();

                    // Upload modified texture to GPU for live feedback
                    uint32_t handle = m_ctx.selectedObject->getBufferHandle();
                    auto& texData = m_ctx.selectedObject->getTextureData();
                    int w = m_ctx.selectedObject->getTextureWidth();
                    int h = m_ctx.selectedObject->getTextureHeight();
                    m_ctx.modelRenderer.updateTexture(handle, texData.data(), w, h);
                    m_ctx.selectedObject->clearTextureModified();
                }
            } else {
                uvPaintingLastFrame = false;
            }
        } else {
            uvPaintingLastFrame = false;
        }

        // Color sampling in UV editor: Alt + Click
        if (m_ctx.isPainting && m_ctx.selectedObject && m_ctx.selectedObject->hasTextureData() &&
            isHovered && !m_ctx.uvPanning && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::GetIO().KeyAlt) {
            glm::vec2 sampleUV = screenToUV(mousePos);

            // Check if UV is within valid range (0-1)
            if (sampleUV.x >= 0.0f && sampleUV.x <= 1.0f && sampleUV.y >= 0.0f && sampleUV.y <= 1.0f) {
                int texW = m_ctx.selectedObject->getTextureWidth();
                int texH = m_ctx.selectedObject->getTextureHeight();
                auto& texData = m_ctx.selectedObject->getTextureData();

                int px = static_cast<int>(sampleUV.x * texW);
                int py = static_cast<int>(sampleUV.y * texH);
                px = std::clamp(px, 0, texW - 1);
                py = std::clamp(py, 0, texH - 1);

                size_t pixelIdx = (py * texW + px) * 4;
                if (pixelIdx + 2 < texData.size()) {
                    m_ctx.paintColor.r = texData[pixelIdx] / 255.0f;
                    m_ctx.paintColor.g = texData[pixelIdx + 1] / 255.0f;
                    m_ctx.paintColor.b = texData[pixelIdx + 2] / 255.0f;

                    std::cout << "[UV] Sampled color at UV (" << sampleUV.x << ", " << sampleUV.y << "): RGB("
                              << static_cast<int>(m_ctx.paintColor.r * 255) << ", "
                              << static_cast<int>(m_ctx.paintColor.g * 255) << ", "
                              << static_cast<int>(m_ctx.paintColor.b * 255) << ")" << std::endl;
                }
            }
        }

        // Handle selection
        bool shiftHeld = ImGui::GetIO().KeyShift;

        // Check for gizmo clicks based on current mode
        if (!m_ctx.uvSelectedFaces.empty() && isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !m_ctx.uvScaling && !m_ctx.uvRotating && !m_ctx.uvDraggingSelection && !m_ctx.isPainting) {

            glm::vec2 selMin, selMax;
            getUVSelectionBounds(selMin, selMax);
            glm::vec2 center = (selMin + selMax) * 0.5f;
            glm::vec2 clickUV = screenToUV(mousePos);
            float handleRadius = 8.0f;
            float threshold = handleRadius / (texSize * m_ctx.uvZoom);

            bool gizmoClicked = false;

            if (m_ctx.gizmoMode == GizmoMode::Scale) {
                // Scale handle click detection
                glm::vec2 corners[4] = {
                    glm::vec2(selMin.x, selMax.y),  // TL
                    glm::vec2(selMax.x, selMax.y),  // TR
                    glm::vec2(selMax.x, selMin.y),  // BR
                    glm::vec2(selMin.x, selMin.y)   // BL
                };
                glm::vec2 sides[4] = {
                    glm::vec2((selMin.x + selMax.x) * 0.5f, selMax.y),  // T
                    glm::vec2(selMax.x, (selMin.y + selMax.y) * 0.5f),  // R
                    glm::vec2((selMin.x + selMax.x) * 0.5f, selMin.y),  // B
                    glm::vec2(selMin.x, (selMin.y + selMax.y) * 0.5f)   // L
                };

                int clickedHandle = -1;
                for (int i = 0; i < 4; ++i) {
                    if (glm::length(clickUV - corners[i]) < threshold) {
                        clickedHandle = i;
                        break;
                    }
                }
                if (clickedHandle < 0) {
                    for (int i = 0; i < 4; ++i) {
                        if (glm::length(clickUV - sides[i]) < threshold) {
                            clickedHandle = i + 4;
                            break;
                        }
                    }
                }

                if (clickedHandle >= 0) {
                    m_ctx.uvScaling = true;
                    m_ctx.uvScaleHandle = clickedHandle;
                    m_ctx.uvScaleOriginalMin = selMin;
                    m_ctx.uvScaleOriginalMax = selMax;
                    m_ctx.uvScaleStart = clickUV;

                    if (clickedHandle < 4) {
                        m_ctx.uvScaleAnchor = corners[(clickedHandle + 2) % 4];
                    } else {
                        m_ctx.uvScaleAnchor = sides[(clickedHandle - 4 + 2) % 4];
                    }

                    m_ctx.editableMesh.saveState();
                    storeOriginalUVs();
                    gizmoClicked = true;
                }
            } else if (m_ctx.gizmoMode == GizmoMode::Move) {
                // Move gizmo - check if clicking on arrows or center
                float arrowLength = 0.08f / m_ctx.uvZoom;
                float arrowThreshold = 0.015f / m_ctx.uvZoom;

                // Check X axis (right arrow)
                glm::vec2 xEnd = center + glm::vec2(arrowLength, 0.0f);
                float distToX = glm::length(clickUV - glm::mix(center, xEnd, 0.5f));

                // Check Y axis (up arrow)
                glm::vec2 yEnd = center + glm::vec2(0.0f, arrowLength);
                float distToY = glm::length(clickUV - glm::mix(center, yEnd, 0.5f));

                // Check center square
                float distToCenter = glm::length(clickUV - center);

                if (distToCenter < arrowThreshold || distToX < arrowThreshold || distToY < arrowThreshold) {
                    m_ctx.uvDraggingSelection = true;
                    m_ctx.uvDragStart = clickUV;
                    m_ctx.editableMesh.saveState();
                    storeOriginalUVs();
                    gizmoClicked = true;
                }
            } else if (m_ctx.gizmoMode == GizmoMode::Rotate) {
                // Rotate gizmo - check if clicking on circle
                float rotateRadius = 0.06f / m_ctx.uvZoom;
                float ringThickness = 0.012f / m_ctx.uvZoom;
                float distToCenter = glm::length(clickUV - center);

                if (std::abs(distToCenter - rotateRadius) < ringThickness) {
                    m_ctx.uvRotating = true;
                    m_ctx.uvScaleCenter = center;
                    glm::vec2 toMouse = clickUV - center;
                    m_ctx.uvRotateStartAngle = std::atan2(toMouse.y, toMouse.x);
                    m_ctx.editableMesh.saveState();
                    storeOriginalUVs();
                    gizmoClicked = true;
                }
            }

            // If gizmo wasn't clicked, don't block selection below
            if (gizmoClicked) {
                // Skip normal selection handling
            }
        }

        // Skip selection handling if we're painting
        if (m_ctx.isPainting && m_ctx.selectedObject && isHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // Don't process selection clicks while painting
        } else if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_ctx.uvPanning && !m_ctx.uvScaling) {
            glm::vec2 clickUV = screenToUV(mousePos);

            if (m_ctx.uvSelectionMode == 3) {
                // Vertex selection mode
                float threshold = 0.015f / m_ctx.uvZoom;
                int clickedVert = findUVVertexAtPoint(clickUV, threshold);

                if (clickedVert >= 0) {
                    uint32_t vertIdx = static_cast<uint32_t>(clickedVert);
                    if (m_ctx.uvSelectedVertices.count(vertIdx) > 0) {
                        // Start dragging selected vertices
                        m_ctx.uvDraggingVertex = true;
                        m_ctx.uvDragStart = clickUV;
                        m_ctx.editableMesh.saveState();
                        storeOriginalUVsForVertices();
                    } else {
                        if (!shiftHeld) {
                            m_ctx.uvSelectedVertices.clear();
                        }
                        m_ctx.uvSelectedVertices.insert(vertIdx);
                    }
                } else {
                    // Start rectangle selection
                    m_uvRectSelecting = true;
                    m_uvRectStart = clickUV;
                    m_uvRectEnd = clickUV;
                    if (!shiftHeld) {
                        m_ctx.uvSelectedVertices.clear();
                    }
                }
            } else if (m_ctx.uvSelectionMode == 2) {
                // Edge selection mode
                float threshold = 0.02f / m_ctx.uvZoom;
                auto [faceIdx, edgeIdx] = findUVEdgeAtPoint(clickUV, threshold);

                if (faceIdx != UINT32_MAX) {
                    m_ctx.uvSelectedEdge = {faceIdx, edgeIdx};
                    findTwinUVEdges(faceIdx, edgeIdx);
                    std::cout << "[UV] Selected edge on face " << faceIdx << ", edge " << edgeIdx << std::endl;
                } else {
                    clearUVEdgeSelection();
                    std::cout << "[UV] Cleared edge selection" << std::endl;
                }
            } else if (m_ctx.uvSelectionMode == 1) {
                // Face selection mode (individual faces only)
                int clickedFace = findUVFaceAtPoint(clickUV);

                if (clickedFace >= 0) {
                    uint32_t faceIdx = static_cast<uint32_t>(clickedFace);
                    if (m_ctx.uvSelectedFaces.count(faceIdx) > 0) {
                        m_ctx.uvDraggingSelection = true;
                        m_ctx.uvDragStart = clickUV;
                        m_ctx.editableMesh.saveState();
                        storeOriginalUVs();
                    } else {
                        if (!shiftHeld) {
                            m_ctx.uvSelectedFaces.clear();
                        }
                        m_ctx.uvSelectedFaces.insert(faceIdx);
                    }
                } else {
                    // Start rectangle selection
                    m_uvRectSelecting = true;
                    m_uvRectStart = clickUV;
                    m_uvRectEnd = clickUV;
                    if (!shiftHeld) {
                        m_ctx.uvSelectedFaces.clear();
                    }
                }
            } else {
                // Island selection mode (mode 0)
                int clickedFace = findUVFaceAtPoint(clickUV);

                if (clickedFace >= 0) {
                    if (m_ctx.uvSelectedFaces.count(static_cast<uint32_t>(clickedFace)) > 0) {
                        m_ctx.uvDraggingSelection = true;
                        m_ctx.uvDragStart = clickUV;
                        m_ctx.editableMesh.saveState();
                        storeOriginalUVs();
                    } else {
                        if (!shiftHeld) {
                            m_ctx.uvSelectedFaces.clear();
                        }
                        selectUVIsland(static_cast<uint32_t>(clickedFace));
                    }
                } else {
                    // Start rectangle selection
                    m_uvRectSelecting = true;
                    m_uvRectStart = clickUV;
                    m_uvRectEnd = clickUV;
                    if (!shiftHeld) {
                        m_ctx.uvSelectedFaces.clear();
                    }
                }
            }
        }

        // Handle vertex dragging
        if (m_ctx.uvDraggingVertex) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                glm::vec2 currentUV = screenToUV(mousePos);
                glm::vec2 delta = currentUV - m_ctx.uvDragStart;
                moveSelectedUVVertices(delta);
            } else {
                m_ctx.uvDraggingVertex = false;
                m_ctx.meshDirty = true;
                m_ctx.uvOriginalCoords.clear();  // Clear to prevent jump on next operation
                // Print final positions
                std::cout << "[UV] Finished dragging vertices. Final positions:" << std::endl;
                for (uint32_t vIdx : m_ctx.uvSelectedVertices) {
                    glm::vec2 uv = m_ctx.editableMesh.getVertex(vIdx).uv;
                    bool isSeam = isSeamVertex(m_ctx.editableMesh, vIdx);
                    if (isSeam) {
                        std::cout << "  Moved SEAM vertex " << vIdx << ": (" << uv.x << ", " << uv.y << ")" << std::endl;
                    } else {
                        std::cout << "  Moved vertex " << vIdx << ": (" << uv.x << ", " << uv.y << ")" << std::endl;
                    }
                }
            }
        }

        // Handle face dragging
        if (m_ctx.uvDraggingSelection) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                glm::vec2 currentUV = screenToUV(mousePos);
                glm::vec2 delta = currentUV - m_ctx.uvDragStart;
                moveSelectedUVs(delta);
            } else {
                m_ctx.uvDraggingSelection = false;
                m_ctx.meshDirty = true;
                m_ctx.uvOriginalCoords.clear();
            }
        }

        // Handle rectangle selection
        if (m_uvRectSelecting) {
            m_uvRectEnd = screenToUV(mousePos);

            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                // Complete rectangle selection
                glm::vec2 rectMin = glm::min(m_uvRectStart, m_uvRectEnd);
                glm::vec2 rectMax = glm::max(m_uvRectStart, m_uvRectEnd);

                if (m_ctx.uvSelectionMode == 3) {
                    // Vertex mode - select vertices within rectangle
                    for (uint32_t faceIdx = 0; faceIdx < m_ctx.editableMesh.getFaceCount(); ++faceIdx) {
                        auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
                        for (uint32_t vertIdx : faceVerts) {
                            glm::vec2 uv = m_ctx.editableMesh.getVertex(vertIdx).uv;
                            if (uv.x >= rectMin.x && uv.x <= rectMax.x &&
                                uv.y >= rectMin.y && uv.y <= rectMax.y) {
                                m_ctx.uvSelectedVertices.insert(vertIdx);
                            }
                        }
                    }
                } else if (m_ctx.uvSelectionMode == 1 || m_ctx.uvSelectionMode == 0) {
                    // Face or Island mode - select faces with center in rectangle
                    for (uint32_t faceIdx = 0; faceIdx < m_ctx.editableMesh.getFaceCount(); ++faceIdx) {
                        auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
                        if (faceVerts.empty()) continue;

                        // Calculate face center
                        glm::vec2 center(0.0f);
                        for (uint32_t vertIdx : faceVerts) {
                            center += m_ctx.editableMesh.getVertex(vertIdx).uv;
                        }
                        center /= static_cast<float>(faceVerts.size());

                        if (center.x >= rectMin.x && center.x <= rectMax.x &&
                            center.y >= rectMin.y && center.y <= rectMax.y) {
                            if (m_ctx.uvSelectionMode == 0) {
                                // Island mode - select entire island
                                selectUVIsland(faceIdx);
                            } else {
                                // Face mode - select just this face
                                m_ctx.uvSelectedFaces.insert(faceIdx);
                            }
                        }
                    }
                }

                m_uvRectSelecting = false;
            }
        }

        // Handle-based scaling (drag handles on selection bounding box)
        if (m_ctx.uvScaling) {
            glm::vec2 currentUV = screenToUV(mousePos);

            if (m_ctx.uvScaleHandle < 4) {
                // Corner handle - uniform scaling from opposite corner
                glm::vec2 startOffset = m_ctx.uvScaleStart - m_ctx.uvScaleAnchor;
                glm::vec2 currentOffset = currentUV - m_ctx.uvScaleAnchor;
                float startDist = glm::length(startOffset);
                float currentDist = glm::length(currentOffset);
                float scale = (startDist > 0.001f) ? (currentDist / startDist) : 1.0f;
                scaleSelectedUVsFromAnchor(m_ctx.uvScaleAnchor, scale, scale);
            } else {
                // Side handle - scale in one axis only
                int side = m_ctx.uvScaleHandle - 4;  // 0=T, 1=R, 2=B, 3=L
                glm::vec2 startOffset = m_ctx.uvScaleStart - m_ctx.uvScaleAnchor;
                glm::vec2 currentOffset = currentUV - m_ctx.uvScaleAnchor;

                float scaleX = 1.0f, scaleY = 1.0f;
                if (side == 1 || side == 3) {
                    // Left/Right - scale X
                    scaleX = (std::abs(startOffset.x) > 0.001f) ? (currentOffset.x / startOffset.x) : 1.0f;
                } else {
                    // Top/Bottom - scale Y
                    scaleY = (std::abs(startOffset.y) > 0.001f) ? (currentOffset.y / startOffset.y) : 1.0f;
                }
                scaleSelectedUVsFromAnchor(m_ctx.uvScaleAnchor, scaleX, scaleY);
            }

            // Confirm on mouse release
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_ctx.uvScaling = false;
                m_ctx.uvScaleHandle = -1;
                m_ctx.meshDirty = true;
                m_ctx.uvOriginalCoords.clear();
            }
            // Cancel with Escape
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                for (auto& [vertIdx, origUV] : m_ctx.uvOriginalCoords) {
                    m_ctx.editableMesh.getVertex(vertIdx).uv = origUV;
                }
                m_ctx.uvScaling = false;
                m_ctx.uvScaleHandle = -1;
                m_ctx.uvOriginalCoords.clear();
                m_ctx.editableMesh.undo();
            }
        }

        // Handle active rotation (click on rotate gizmo starts this)
        if (m_ctx.uvRotating) {
            glm::vec2 currentUV = screenToUV(mousePos);
            glm::vec2 toMouse = currentUV - m_ctx.uvScaleCenter;
            float currentAngle = std::atan2(toMouse.y, toMouse.x);
            float angleDelta = glm::degrees(currentAngle - m_ctx.uvRotateStartAngle);
            rotateSelectedUVs(m_ctx.uvScaleCenter, angleDelta);

            // Confirm on mouse release
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_ctx.uvRotating = false;
                m_ctx.meshDirty = true;
                m_ctx.uvOriginalCoords.clear();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                for (auto& [vertIdx, origUV] : m_ctx.uvOriginalCoords) {
                    m_ctx.editableMesh.getVertex(vertIdx).uv = origUV;
                }
                m_ctx.uvRotating = false;
                m_ctx.uvOriginalCoords.clear();
                m_ctx.editableMesh.undo();
                std::cout << "[UV] Cancelled rotation" << std::endl;
            }
        }

        // Handle sewing keys (edge mode = 2)
        if (isHovered && m_ctx.uvSelectionMode == 2 && m_ctx.uvSelectedEdge.first != UINT32_MAX) {
            bool shiftHeldForSew = ImGui::GetIO().KeyShift;

            if (ImGui::IsKeyPressed(ImGuiKey_W) && !m_ctx.uvTwinEdges.empty()) {
                if (shiftHeldForSew) {
                    std::cout << "[UV] Move+Sew on edge (face " << m_ctx.uvSelectedEdge.first << ", edge " << m_ctx.uvSelectedEdge.second << ")" << std::endl;
                    moveAndSewSelectedEdge();
                } else {
                    std::cout << "[UV] Sew on edge (face " << m_ctx.uvSelectedEdge.first << ", edge " << m_ctx.uvSelectedEdge.second << ")" << std::endl;
                    sewSelectedEdge();
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_U)) {
                std::cout << "[UV] Unsew on edge (face " << m_ctx.uvSelectedEdge.first << ", edge " << m_ctx.uvSelectedEdge.second << ")" << std::endl;
                unsewSelectedEdge();
            }
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + viewWidth, canvasPos.y + viewHeight), true);

        // Background
        drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + viewWidth, canvasPos.y + viewHeight),
            IM_COL32(40, 40, 40, 255));

        ImVec2 texMin(offsetX, offsetY);
        ImVec2 texMax(offsetX + texSize, offsetY + texSize);

        // Checkerboard
        int checkerCount = 8;
        float checkerSize = texSize / checkerCount;
        for (int cy = 0; cy < checkerCount; cy++) {
            for (int cx = 0; cx < checkerCount; cx++) {
                bool isLight = ((cx + cy) % 2 == 0);
                ImU32 color = isLight ? IM_COL32(80, 80, 80, 255) : IM_COL32(60, 60, 60, 255);
                ImVec2 cMin(texMin.x + cx * checkerSize, texMin.y + cy * checkerSize);
                ImVec2 cMax(texMin.x + (cx + 1) * checkerSize, texMin.y + (cy + 1) * checkerSize);
                drawList->AddRectFilled(cMin, cMax, color);
            }
        }

        // Draw texture if available (flip Y to match UV convention)
        if (m_ctx.selectedObject) {
            ModelGPUData* gpuData = m_ctx.modelRenderer.getModelData(m_ctx.selectedObject->getBufferHandle());
            if (gpuData && gpuData->descriptorSet) {
                // Flip texture vertically: uv0=(0,1), uv1=(1,0)
                drawList->AddImage((ImTextureID)gpuData->descriptorSet, texMin, texMax,
                                   ImVec2(0, 1), ImVec2(1, 0));
            }
        }

        // Use black wireframe for selected faces so lines show through selection overlay
        bool hasSelection = !m_ctx.uvSelectedFaces.empty();
        ImU32 wireColor = IM_COL32(
            static_cast<int>(m_ctx.uvWireframeColor.x * 255),
            static_cast<int>(m_ctx.uvWireframeColor.y * 255),
            static_cast<int>(m_ctx.uvWireframeColor.z * 255),
            255
        );
        ImU32 selectFillColor = IM_COL32(51, 102, 255, 100);
        ImU32 selectEdgeColor = IM_COL32(0, 0, 0, 255);  // Always black for selected edges

        // Draw UV wireframe
        for (uint32_t faceIdx = 0; faceIdx < m_ctx.editableMesh.getFaceCount(); ++faceIdx) {
            auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
            if (faceVerts.size() < 3) continue;

            bool isSelected = m_ctx.uvSelectedFaces.count(faceIdx) > 0;

            std::vector<ImVec2> screenPts;
            for (uint32_t vertIdx : faceVerts) {
                glm::vec2 uv = m_ctx.editableMesh.getVertex(vertIdx).uv;
                ImVec2 pt(offsetX + uv.x * texSize, offsetY + (1.0f - uv.y) * texSize);
                screenPts.push_back(pt);
            }

            if (isSelected && screenPts.size() >= 3) {
                for (size_t i = 1; i + 1 < screenPts.size(); ++i) {
                    drawList->AddTriangleFilled(screenPts[0], screenPts[i], screenPts[i + 1], selectFillColor);
                }
            }

            ImU32 edgeColor = isSelected ? selectEdgeColor : wireColor;
            float thickness = isSelected ? 2.0f : 1.0f;

            for (size_t i = 0; i < screenPts.size(); ++i) {
                size_t next = (i + 1) % screenPts.size();
                drawList->AddLine(screenPts[i], screenPts[next], edgeColor, thickness);
            }
        }

        // Draw selected edge and twins (UV editor selection)
        if (m_ctx.uvEdgeSelectionMode && m_ctx.uvSelectedEdge.first != UINT32_MAX) {
            ImU32 selectedEdgeCol = IM_COL32(255, 200, 50, 255);
            ImU32 twinEdgeCol = IM_COL32(50, 255, 150, 255);

            auto [selUV0, selUV1] = getEdgeUVs(m_ctx.uvSelectedEdge.first, m_ctx.uvSelectedEdge.second);
            ImVec2 selPt0(offsetX + selUV0.x * texSize, offsetY + (1.0f - selUV0.y) * texSize);
            ImVec2 selPt1(offsetX + selUV1.x * texSize, offsetY + (1.0f - selUV1.y) * texSize);
            drawList->AddLine(selPt0, selPt1, selectedEdgeCol, 3.0f);
            drawList->AddCircleFilled(selPt0, 4.0f, selectedEdgeCol);
            drawList->AddCircleFilled(selPt1, 4.0f, selectedEdgeCol);

            for (const auto& [twinFaceIdx, twinEdgeIdx] : m_ctx.uvTwinEdges) {
                auto [twinUV0, twinUV1] = getEdgeUVs(twinFaceIdx, twinEdgeIdx);
                ImVec2 twinPt0(offsetX + twinUV0.x * texSize, offsetY + (1.0f - twinUV0.y) * texSize);
                ImVec2 twinPt1(offsetX + twinUV1.x * texSize, offsetY + (1.0f - twinUV1.y) * texSize);
                drawList->AddLine(twinPt0, twinPt1, twinEdgeCol, 3.0f);
                drawList->AddCircleFilled(twinPt0, 4.0f, twinEdgeCol);
                drawList->AddCircleFilled(twinPt1, 4.0f, twinEdgeCol);
            }
        }

        // Draw 3D model selected edges in UV space (magenta/pink for visibility)
        auto modelSelectedEdges = m_ctx.editableMesh.getSelectedEdges();
        if (!modelSelectedEdges.empty()) {
            ImU32 modelEdgeCol = IM_COL32(255, 50, 200, 255);  // Magenta/pink

            for (uint32_t heIdx : modelSelectedEdges) {
                auto edgeVerts = m_ctx.editableMesh.getEdgeVertices(heIdx);
                if (edgeVerts.first == UINT32_MAX) continue;

                glm::vec2 uv0 = m_ctx.editableMesh.getVertex(edgeVerts.first).uv;
                glm::vec2 uv1 = m_ctx.editableMesh.getVertex(edgeVerts.second).uv;

                ImVec2 pt0(offsetX + uv0.x * texSize, offsetY + (1.0f - uv0.y) * texSize);
                ImVec2 pt1(offsetX + uv1.x * texSize, offsetY + (1.0f - uv1.y) * texSize);

                drawList->AddLine(pt0, pt1, modelEdgeCol, 2.5f);
            }
        }

        // Draw 3D model selected faces in UV space (cyan fill with outline)
        auto modelSelectedFaces = m_ctx.editableMesh.getSelectedFaces();
        if (!modelSelectedFaces.empty()) {
            ImU32 modelFaceFill = IM_COL32(50, 200, 255, 80);   // Cyan semi-transparent fill
            ImU32 modelFaceEdge = IM_COL32(50, 200, 255, 255);  // Cyan solid outline

            for (uint32_t faceIdx : modelSelectedFaces) {
                auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
                if (faceVerts.size() < 3) continue;

                std::vector<ImVec2> screenPts;
                for (uint32_t vi : faceVerts) {
                    glm::vec2 uv = m_ctx.editableMesh.getVertex(vi).uv;
                    screenPts.push_back(ImVec2(offsetX + uv.x * texSize, offsetY + (1.0f - uv.y) * texSize));
                }

                // Fill the face
                if (screenPts.size() >= 3) {
                    for (size_t i = 1; i + 1 < screenPts.size(); ++i) {
                        drawList->AddTriangleFilled(screenPts[0], screenPts[i], screenPts[i + 1], modelFaceFill);
                    }
                }

                // Draw outline
                for (size_t i = 0; i < screenPts.size(); ++i) {
                    size_t next = (i + 1) % screenPts.size();
                    drawList->AddLine(screenPts[i], screenPts[next], modelFaceEdge, 2.0f);
                }
            }
        }

        // Draw UV vertices (only in vertex selection mode = 3)
        if (m_ctx.uvSelectionMode == 3) {
            ImU32 vertColor = IM_COL32(100, 100, 100, 200);
            ImU32 selectedVertColor = IM_COL32(255, 200, 50, 255);
            ImU32 sharedVertColor = IM_COL32(50, 200, 255, 255);  // Cyan for shared vertices
            float vertRadius = 4.0f;

            // Find shared vertices (same 3D position as selected, different UV)
            std::set<uint32_t> sharedVerts;
            if (!m_ctx.uvSelectedVertices.empty()) {
                for (uint32_t selVertIdx : m_ctx.uvSelectedVertices) {
                    glm::vec3 selPos = m_ctx.editableMesh.getVertex(selVertIdx).position;
                    // Find other vertices at same 3D position
                    for (uint32_t i = 0; i < m_ctx.editableMesh.getVertexCount(); ++i) {
                        if (i == selVertIdx) continue;
                        if (m_ctx.uvSelectedVertices.count(i) > 0) continue;
                        glm::vec3 pos = m_ctx.editableMesh.getVertex(i).position;
                        if (glm::length(pos - selPos) < 0.0001f) {
                            sharedVerts.insert(i);
                        }
                    }
                }
            }

            // Draw all vertices as small dots
            std::set<uint32_t> drawnVerts;
            for (uint32_t faceIdx = 0; faceIdx < m_ctx.editableMesh.getFaceCount(); ++faceIdx) {
                auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
                for (uint32_t vertIdx : faceVerts) {
                    if (drawnVerts.count(vertIdx) > 0) continue;
                    drawnVerts.insert(vertIdx);

                    glm::vec2 uv = m_ctx.editableMesh.getVertex(vertIdx).uv;
                    ImVec2 pt(offsetX + uv.x * texSize, offsetY + (1.0f - uv.y) * texSize);

                    bool isSelected = m_ctx.uvSelectedVertices.count(vertIdx) > 0;
                    bool isShared = sharedVerts.count(vertIdx) > 0;
                    if (isSelected) {
                        drawList->AddCircleFilled(pt, vertRadius + 2.0f, selectedVertColor);
                    } else if (isShared) {
                        drawList->AddCircleFilled(pt, vertRadius + 1.0f, sharedVertColor);
                        drawList->AddCircle(pt, vertRadius + 3.0f, sharedVertColor, 0, 1.5f);
                    } else {
                        drawList->AddCircleFilled(pt, vertRadius, vertColor);
                    }
                }
            }
        }

        // Selection bounding box and gizmos
        if (!m_ctx.uvSelectedFaces.empty()) {
            glm::vec2 selMin, selMax;
            getUVSelectionBounds(selMin, selMax);
            glm::vec2 center = (selMin + selMax) * 0.5f;
            ImVec2 boxMin(offsetX + selMin.x * texSize, offsetY + (1.0f - selMax.y) * texSize);
            ImVec2 boxMax(offsetX + selMax.x * texSize, offsetY + (1.0f - selMin.y) * texSize);
            ImVec2 centerScreen(offsetX + center.x * texSize, offsetY + (1.0f - center.y) * texSize);

            // Always draw selection bounding box
            drawList->AddRect(boxMin, boxMax, IM_COL32(255, 200, 50, 255), 0.0f, 0, 2.0f);

            // Draw gizmo based on current mode
            if (m_ctx.gizmoMode == GizmoMode::Scale && !m_ctx.uvRotating && !m_ctx.uvDraggingSelection) {
                // Scale handles
                float handleSize = 6.0f;
                ImU32 handleColor = IM_COL32(255, 255, 255, 255);
                ImU32 handleBorder = IM_COL32(0, 0, 0, 255);
                ImU32 activeColor = IM_COL32(255, 150, 50, 255);

                ImVec2 corners[4] = {
                    ImVec2(boxMin.x, boxMin.y),  // TL
                    ImVec2(boxMax.x, boxMin.y),  // TR
                    ImVec2(boxMax.x, boxMax.y),  // BR
                    ImVec2(boxMin.x, boxMax.y)   // BL
                };
                for (int i = 0; i < 4; ++i) {
                    ImU32 color = (m_ctx.uvScaleHandle == i) ? activeColor : handleColor;
                    drawList->AddRectFilled(
                        ImVec2(corners[i].x - handleSize, corners[i].y - handleSize),
                        ImVec2(corners[i].x + handleSize, corners[i].y + handleSize),
                        color);
                    drawList->AddRect(
                        ImVec2(corners[i].x - handleSize, corners[i].y - handleSize),
                        ImVec2(corners[i].x + handleSize, corners[i].y + handleSize),
                        handleBorder);
                }

                ImVec2 midX = ImVec2((boxMin.x + boxMax.x) * 0.5f, 0);
                ImVec2 midY = ImVec2(0, (boxMin.y + boxMax.y) * 0.5f);
                ImVec2 sides[4] = {
                    ImVec2(midX.x, boxMin.y),  // T
                    ImVec2(boxMax.x, midY.y),  // R
                    ImVec2(midX.x, boxMax.y),  // B
                    ImVec2(boxMin.x, midY.y)   // L
                };
                for (int i = 0; i < 4; ++i) {
                    ImU32 color = (m_ctx.uvScaleHandle == i + 4) ? activeColor : handleColor;
                    float sideW = (i == 0 || i == 2) ? handleSize : handleSize * 0.6f;
                    float sideH = (i == 1 || i == 3) ? handleSize : handleSize * 0.6f;
                    drawList->AddRectFilled(
                        ImVec2(sides[i].x - sideW, sides[i].y - sideH),
                        ImVec2(sides[i].x + sideW, sides[i].y + sideH),
                        color);
                    drawList->AddRect(
                        ImVec2(sides[i].x - sideW, sides[i].y - sideH),
                        ImVec2(sides[i].x + sideW, sides[i].y + sideH),
                        handleBorder);
                }
            } else if (m_ctx.gizmoMode == GizmoMode::Move && !m_ctx.uvScaling && !m_ctx.uvRotating) {
                // Move gizmo - two arrows (X and Y)
                float arrowLength = 0.08f / m_ctx.uvZoom * texSize;
                float arrowHeadSize = 8.0f;
                ImU32 xColor = IM_COL32(255, 80, 80, 255);   // Red for X
                ImU32 yColor = IM_COL32(80, 255, 80, 255);   // Green for Y
                ImU32 centerColor = IM_COL32(255, 255, 100, 255);

                // X axis arrow (pointing right)
                ImVec2 xEnd(centerScreen.x + arrowLength, centerScreen.y);
                drawList->AddLine(centerScreen, xEnd, xColor, 3.0f);
                // Arrow head
                drawList->AddTriangleFilled(
                    ImVec2(xEnd.x + arrowHeadSize, xEnd.y),
                    ImVec2(xEnd.x - arrowHeadSize * 0.5f, xEnd.y - arrowHeadSize * 0.5f),
                    ImVec2(xEnd.x - arrowHeadSize * 0.5f, xEnd.y + arrowHeadSize * 0.5f),
                    xColor);

                // Y axis arrow (pointing up - note: screen Y is inverted)
                ImVec2 yEnd(centerScreen.x, centerScreen.y - arrowLength);
                drawList->AddLine(centerScreen, yEnd, yColor, 3.0f);
                // Arrow head
                drawList->AddTriangleFilled(
                    ImVec2(yEnd.x, yEnd.y - arrowHeadSize),
                    ImVec2(yEnd.x - arrowHeadSize * 0.5f, yEnd.y + arrowHeadSize * 0.5f),
                    ImVec2(yEnd.x + arrowHeadSize * 0.5f, yEnd.y + arrowHeadSize * 0.5f),
                    yColor);

                // Center square
                float centerSize = 6.0f;
                drawList->AddRectFilled(
                    ImVec2(centerScreen.x - centerSize, centerScreen.y - centerSize),
                    ImVec2(centerScreen.x + centerSize, centerScreen.y + centerSize),
                    centerColor);
                drawList->AddRect(
                    ImVec2(centerScreen.x - centerSize, centerScreen.y - centerSize),
                    ImVec2(centerScreen.x + centerSize, centerScreen.y + centerSize),
                    IM_COL32(0, 0, 0, 255));
            } else if (m_ctx.gizmoMode == GizmoMode::Rotate && !m_ctx.uvScaling && !m_ctx.uvDraggingSelection) {
                // Rotate gizmo - circle
                float rotateRadius = 0.06f / m_ctx.uvZoom * texSize;
                ImU32 rotateColor = IM_COL32(100, 150, 255, 255);

                drawList->AddCircle(centerScreen, rotateRadius, rotateColor, 32, 3.0f);
                drawList->AddCircleFilled(centerScreen, 5.0f, rotateColor);
            }

            // Show rotation feedback when actively rotating
            if (m_ctx.uvRotating) {
                ImVec2 rotCenter(offsetX + m_ctx.uvScaleCenter.x * texSize,
                                 offsetY + (1.0f - m_ctx.uvScaleCenter.y) * texSize);
                ImU32 pivotColor = IM_COL32(100, 200, 255, 255);
                drawList->AddCircleFilled(rotCenter, 5.0f, pivotColor);

                glm::vec2 currentUV = screenToUV(mousePos);
                ImVec2 mouseScreen(offsetX + currentUV.x * texSize, offsetY + (1.0f - currentUV.y) * texSize);
                drawList->AddLine(rotCenter, mouseScreen, IM_COL32(100, 200, 255, 200), 2.0f);
            }
        }

        // Draw rectangle selection
        if (m_uvRectSelecting) {
            glm::vec2 rectMin = glm::min(m_uvRectStart, m_uvRectEnd);
            glm::vec2 rectMax = glm::max(m_uvRectStart, m_uvRectEnd);
            ImVec2 screenMin(offsetX + rectMin.x * texSize, offsetY + (1.0f - rectMax.y) * texSize);
            ImVec2 screenMax(offsetX + rectMax.x * texSize, offsetY + (1.0f - rectMin.y) * texSize);
            drawList->AddRectFilled(screenMin, screenMax, IM_COL32(100, 150, 255, 50));
            drawList->AddRect(screenMin, screenMax, IM_COL32(100, 150, 255, 200), 0.0f, 0, 2.0f);
        }

        // UV space border
        drawList->AddRect(texMin, texMax, IM_COL32(200, 200, 200, 255), 0.0f, 0, 2.0f);

        // Brush preview when paint mode is active
        if (m_ctx.isPainting && isHovered && m_ctx.selectedObject) {
            glm::vec2 brushUV = screenToUV(mousePos);
            float brushScreenX = offsetX + brushUV.x * texSize;
            float brushScreenY = offsetY + (1.0f - brushUV.y) * texSize;
            float brushScreenRadius = m_ctx.paintRadius * texSize;

            ImU32 brushColor = IM_COL32(
                static_cast<int>(m_ctx.paintColor.r * 255),
                static_cast<int>(m_ctx.paintColor.g * 255),
                static_cast<int>(m_ctx.paintColor.b * 255),
                100
            );
            ImU32 brushOutline = IM_COL32(255, 255, 255, 200);

            if (m_ctx.squareBrush) {
                ImVec2 brushMin(brushScreenX - brushScreenRadius, brushScreenY - brushScreenRadius);
                ImVec2 brushMax(brushScreenX + brushScreenRadius, brushScreenY + brushScreenRadius);
                drawList->AddRectFilled(brushMin, brushMax, brushColor);
                drawList->AddRect(brushMin, brushMax, brushOutline, 0.0f, 0, 1.5f);
            } else {
                drawList->AddCircleFilled(ImVec2(brushScreenX, brushScreenY), brushScreenRadius, brushColor, 32);
                drawList->AddCircle(ImVec2(brushScreenX, brushScreenY), brushScreenRadius, brushOutline, 32, 1.5f);
            }

            // Line preview: when Shift is held and we have a previous paint position
            if (ImGui::GetIO().KeyShift && hasLastUVPaintPos) {
                // Convert last paint position to screen
                ImVec2 lastScreen(previewOffsetX + lastUVPaintPos.x * previewTexSize,
                                  previewOffsetY + (1.0f - lastUVPaintPos.y) * previewTexSize);
                ImVec2 currentScreen(brushScreenX, brushScreenY);

                // Draw preview line
                ImU32 lineColor = IM_COL32(
                    static_cast<int>(m_ctx.paintColor.r * 255),
                    static_cast<int>(m_ctx.paintColor.g * 255),
                    static_cast<int>(m_ctx.paintColor.b * 255),
                    200
                );
                drawList->AddLine(lastScreen, currentScreen, lineColor, 2.0f);

                // Draw endpoint markers
                drawList->AddCircleFilled(lastScreen, 4.0f, lineColor);
                drawList->AddCircle(lastScreen, 6.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
            }
        }

        drawList->PopClipRect();
        ImGui::EndChild();

        // Status
        ImGui::Text("Zoom: %.1fx | Faces: %zu", m_ctx.uvZoom, m_ctx.editableMesh.getFaceCount());
        if (m_ctx.uvEdgeSelectionMode) {
            if (m_ctx.uvSelectedEdge.first != UINT32_MAX) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "| Edge selected");
                if (!m_ctx.uvTwinEdges.empty()) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.6f, 1.0f), "| %zu twin(s)", m_ctx.uvTwinEdges.size());
                }
            }
        } else if (!m_ctx.uvSelectedFaces.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f, 0.6f, 1.0f, 1.0f), "| UV Selected: %zu", m_ctx.uvSelectedFaces.size());
        }
        if (m_ctx.uvScaling) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "SCALING - Release to confirm, Esc to cancel");
        } else if (m_ctx.uvRotating) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "ROTATING - Click to confirm, Esc to cancel");
        } else if (m_ctx.uvDraggingSelection) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "MOVING...");
        }
    }
    ImGui::End();
}
bool ModelingMode::pointInUVTriangle(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
    glm::vec2 v0 = c - a, v1 = b - a, v2 = p - a;
    float dot00 = glm::dot(v0, v0);
    float dot01 = glm::dot(v0, v1);
    float dot02 = glm::dot(v0, v2);
    float dot11 = glm::dot(v1, v1);
    float dot12 = glm::dot(v1, v2);
    float invDenom = 1.0f / (dot00 * dot11 - dot01 * dot01);
    float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
    float v = (dot00 * dot12 - dot01 * dot02) * invDenom;
    return (u >= 0) && (v >= 0) && (u + v <= 1);
}

int ModelingMode::findUVFaceAtPoint(const glm::vec2& uvPoint) {
    for (uint32_t faceIdx = 0; faceIdx < m_ctx.editableMesh.getFaceCount(); ++faceIdx) {
        auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
        if (faceVerts.size() < 3) continue;

        std::vector<glm::vec2> uvs;
        for (uint32_t vertIdx : faceVerts) {
            uvs.push_back(m_ctx.editableMesh.getVertex(vertIdx).uv);
        }

        for (size_t i = 1; i + 1 < uvs.size(); ++i) {
            if (pointInUVTriangle(uvPoint, uvs[0], uvs[i], uvs[i + 1])) {
                return static_cast<int>(faceIdx);
            }
        }
    }
    return -1;
}

int ModelingMode::findUVVertexAtPoint(const glm::vec2& uvPoint, float threshold) {
    int closestVert = -1;
    float closestDist = threshold;

    for (uint32_t faceIdx = 0; faceIdx < m_ctx.editableMesh.getFaceCount(); ++faceIdx) {
        auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
        for (uint32_t vertIdx : faceVerts) {
            glm::vec2 uv = m_ctx.editableMesh.getVertex(vertIdx).uv;
            float dist = glm::length(uv - uvPoint);
            if (dist < closestDist) {
                closestDist = dist;
                closestVert = static_cast<int>(vertIdx);
            }
        }
    }
    return closestVert;
}

void ModelingMode::storeOriginalUVsForVertices() {
    m_ctx.uvOriginalCoords.clear();
    for (uint32_t vertIdx : m_ctx.uvSelectedVertices) {
        m_ctx.uvOriginalCoords[vertIdx] = m_ctx.editableMesh.getVertex(vertIdx).uv;
    }
}

void ModelingMode::moveSelectedUVVertices(const glm::vec2& delta) {
    for (auto& [vertIdx, origUV] : m_ctx.uvOriginalCoords) {
        m_ctx.editableMesh.getVertex(vertIdx).uv = origUV + delta;
    }
}

void ModelingMode::selectUVIsland(uint32_t startFace) {
    std::set<uint32_t> visited;
    std::queue<uint32_t> toVisit;
    toVisit.push(startFace);
    visited.insert(startFace);

    auto uvKey = [](const glm::vec2& uv) {
        return std::make_pair(static_cast<int>(uv.x * 10000), static_cast<int>(uv.y * 10000));
    };

    std::map<std::pair<int, int>, std::vector<uint32_t>> uvToFaces;
    for (uint32_t faceIdx = 0; faceIdx < m_ctx.editableMesh.getFaceCount(); ++faceIdx) {
        auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
        for (uint32_t vertIdx : faceVerts) {
            glm::vec2 uv = m_ctx.editableMesh.getVertex(vertIdx).uv;
            uvToFaces[uvKey(uv)].push_back(faceIdx);
        }
    }

    while (!toVisit.empty()) {
        uint32_t faceIdx = toVisit.front();
        toVisit.pop();
        m_ctx.uvSelectedFaces.insert(faceIdx);

        auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
        for (uint32_t vertIdx : faceVerts) {
            glm::vec2 uv = m_ctx.editableMesh.getVertex(vertIdx).uv;
            auto& adjacentFaces = uvToFaces[uvKey(uv)];
            for (uint32_t adjFace : adjacentFaces) {
                if (visited.find(adjFace) == visited.end()) {
                    visited.insert(adjFace);
                    toVisit.push(adjFace);
                }
            }
        }
    }
}

std::set<uint32_t> ModelingMode::getUVSelectedVertices() {
    std::set<uint32_t> verts;
    for (uint32_t faceIdx : m_ctx.uvSelectedFaces) {
        auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
        for (uint32_t v : faceVerts) {
            verts.insert(v);
        }
    }
    return verts;
}

void ModelingMode::getUVSelectionBounds(glm::vec2& outMin, glm::vec2& outMax) {
    outMin = glm::vec2(FLT_MAX);
    outMax = glm::vec2(-FLT_MAX);
    auto verts = getUVSelectedVertices();
    for (uint32_t vertIdx : verts) {
        glm::vec2 uv = m_ctx.editableMesh.getVertex(vertIdx).uv;
        outMin = glm::min(outMin, uv);
        outMax = glm::max(outMax, uv);
    }
}

void ModelingMode::storeOriginalUVs() {
    m_ctx.uvOriginalCoords.clear();
    auto verts = getUVSelectedVertices();
    for (uint32_t vertIdx : verts) {
        m_ctx.uvOriginalCoords[vertIdx] = m_ctx.editableMesh.getVertex(vertIdx).uv;
    }
}

void ModelingMode::moveSelectedUVs(const glm::vec2& delta) {
    for (auto& [vertIdx, origUV] : m_ctx.uvOriginalCoords) {
        m_ctx.editableMesh.getVertex(vertIdx).uv = origUV + delta;
    }
}

void ModelingMode::scaleSelectedUVs(const glm::vec2& center, float scale) {
    for (auto& [vertIdx, origUV] : m_ctx.uvOriginalCoords) {
        glm::vec2 offset = origUV - center;
        m_ctx.editableMesh.getVertex(vertIdx).uv = center + offset * scale;
    }
}

void ModelingMode::scaleSelectedUVsFromAnchor(const glm::vec2& anchor, float scaleX, float scaleY) {
    for (auto& [vertIdx, origUV] : m_ctx.uvOriginalCoords) {
        glm::vec2 offset = origUV - anchor;
        m_ctx.editableMesh.getVertex(vertIdx).uv = anchor + glm::vec2(offset.x * scaleX, offset.y * scaleY);
    }
}

void ModelingMode::rotateSelectedUVs(const glm::vec2& center, float angleDegrees) {
    float rad = glm::radians(angleDegrees);
    float cosA = std::cos(rad);
    float sinA = std::sin(rad);
    for (auto& [vertIdx, origUV] : m_ctx.uvOriginalCoords) {
        glm::vec2 offset = origUV - center;
        glm::vec2 rotated;
        rotated.x = offset.x * cosA - offset.y * sinA;
        rotated.y = offset.x * sinA + offset.y * cosA;
        m_ctx.editableMesh.getVertex(vertIdx).uv = center + rotated;
    }
}

float ModelingMode::pointToLineSegmentDistUV(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    glm::vec2 ab = b - a;
    float len2 = glm::dot(ab, ab);
    if (len2 < 0.00001f) return glm::length(p - a);
    float t = glm::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f);
    glm::vec2 proj = a + t * ab;
    return glm::length(p - proj);
}

std::pair<uint32_t, uint32_t> ModelingMode::findUVEdgeAtPoint(const glm::vec2& uvPoint, float threshold) {
    float bestDist = threshold;
    std::pair<uint32_t, uint32_t> bestEdge = {UINT32_MAX, UINT32_MAX};

    for (uint32_t faceIdx = 0; faceIdx < m_ctx.editableMesh.getFaceCount(); ++faceIdx) {
        auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
        if (faceVerts.size() < 3) continue;

        for (size_t i = 0; i < faceVerts.size(); ++i) {
            size_t next = (i + 1) % faceVerts.size();
            glm::vec2 uv0 = m_ctx.editableMesh.getVertex(faceVerts[i]).uv;
            glm::vec2 uv1 = m_ctx.editableMesh.getVertex(faceVerts[next]).uv;

            float dist = pointToLineSegmentDistUV(uvPoint, uv0, uv1);
            if (dist < bestDist) {
                bestDist = dist;
                bestEdge = {faceIdx, static_cast<uint32_t>(i)};
            }
        }
    }
    return bestEdge;
}

std::pair<glm::vec3, glm::vec3> ModelingMode::getEdge3DPositions(uint32_t faceIdx, uint32_t localEdgeIdx) {
    auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
    uint32_t v0 = faceVerts[localEdgeIdx];
    uint32_t v1 = faceVerts[(localEdgeIdx + 1) % faceVerts.size()];
    return {m_ctx.editableMesh.getVertex(v0).position, m_ctx.editableMesh.getVertex(v1).position};
}

std::pair<glm::vec2, glm::vec2> ModelingMode::getEdgeUVs(uint32_t faceIdx, uint32_t localEdgeIdx) {
    auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
    uint32_t v0 = faceVerts[localEdgeIdx];
    uint32_t v1 = faceVerts[(localEdgeIdx + 1) % faceVerts.size()];
    return {m_ctx.editableMesh.getVertex(v0).uv, m_ctx.editableMesh.getVertex(v1).uv};
}

bool ModelingMode::positions3DEqual(const glm::vec3& a, const glm::vec3& b, float tol) {
    return glm::length(a - b) < tol;
}

void ModelingMode::findTwinUVEdges(uint32_t selectedFaceIdx, uint32_t selectedEdgeIdx) {
    m_ctx.uvTwinEdges.clear();

    if (selectedFaceIdx == UINT32_MAX) return;

    auto [pos0, pos1] = getEdge3DPositions(selectedFaceIdx, selectedEdgeIdx);
    auto [selUV0, selUV1] = getEdgeUVs(selectedFaceIdx, selectedEdgeIdx);

    for (uint32_t faceIdx = 0; faceIdx < m_ctx.editableMesh.getFaceCount(); ++faceIdx) {
        auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
        if (faceVerts.size() < 3) continue;

        for (size_t i = 0; i < faceVerts.size(); ++i) {
            if (faceIdx == selectedFaceIdx && i == selectedEdgeIdx) continue;

            auto [edgePos0, edgePos1] = getEdge3DPositions(faceIdx, static_cast<uint32_t>(i));
            auto [edgeUV0, edgeUV1] = getEdgeUVs(faceIdx, static_cast<uint32_t>(i));

            bool match = (positions3DEqual(pos0, edgePos0) && positions3DEqual(pos1, edgePos1)) ||
                         (positions3DEqual(pos0, edgePos1) && positions3DEqual(pos1, edgePos0));

            if (match) {
                bool uvSame = (glm::length(selUV0 - edgeUV0) < 0.001f && glm::length(selUV1 - edgeUV1) < 0.001f) ||
                              (glm::length(selUV0 - edgeUV1) < 0.001f && glm::length(selUV1 - edgeUV0) < 0.001f);

                if (!uvSame) {
                    m_ctx.uvTwinEdges.push_back({faceIdx, static_cast<uint32_t>(i)});
                }
            }
        }
    }
}

void ModelingMode::clearUVEdgeSelection() {
    m_ctx.uvSelectedEdge = {UINT32_MAX, UINT32_MAX};
    m_ctx.uvTwinEdges.clear();
}

std::set<uint32_t> ModelingMode::getUVIslandFaces(uint32_t startFace) {
    std::set<uint32_t> island;
    std::queue<uint32_t> toVisit;
    toVisit.push(startFace);
    island.insert(startFace);

    auto uvKey = [](const glm::vec2& uv) {
        return std::make_pair(static_cast<int>(uv.x * 10000), static_cast<int>(uv.y * 10000));
    };

    std::map<std::pair<int, int>, std::vector<uint32_t>> uvToFaces;
    for (uint32_t faceIdx = 0; faceIdx < m_ctx.editableMesh.getFaceCount(); ++faceIdx) {
        auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
        for (uint32_t vertIdx : faceVerts) {
            glm::vec2 uv = m_ctx.editableMesh.getVertex(vertIdx).uv;
            uvToFaces[uvKey(uv)].push_back(faceIdx);
        }
    }

    while (!toVisit.empty()) {
        uint32_t faceIdx = toVisit.front();
        toVisit.pop();

        auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
        for (uint32_t vertIdx : faceVerts) {
            glm::vec2 uv = m_ctx.editableMesh.getVertex(vertIdx).uv;
            auto& adjacentFaces = uvToFaces[uvKey(uv)];
            for (uint32_t adjFace : adjacentFaces) {
                if (island.find(adjFace) == island.end()) {
                    island.insert(adjFace);
                    toVisit.push(adjFace);
                }
            }
        }
    }
    return island;
}

std::set<uint32_t> ModelingMode::getIslandVertices(const std::set<uint32_t>& faces) {
    std::set<uint32_t> verts;
    for (uint32_t faceIdx : faces) {
        auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
        for (uint32_t v : faceVerts) {
            verts.insert(v);
        }
    }
    return verts;
}

void ModelingMode::sewSelectedEdge() {
    if (m_ctx.uvSelectedEdge.first == UINT32_MAX || m_ctx.uvTwinEdges.empty()) return;

    m_ctx.editableMesh.saveState();

    auto [selUV0, selUV1] = getEdgeUVs(m_ctx.uvSelectedEdge.first, m_ctx.uvSelectedEdge.second);
    auto [selPos0, selPos1] = getEdge3DPositions(m_ctx.uvSelectedEdge.first, m_ctx.uvSelectedEdge.second);

    auto [twinFaceIdx, twinEdgeIdx] = m_ctx.uvTwinEdges[0];
    auto [twinPos0, twinPos1] = getEdge3DPositions(twinFaceIdx, twinEdgeIdx);

    bool reversed = positions3DEqual(selPos0, twinPos1) && positions3DEqual(selPos1, twinPos0);

    auto twinFaceVerts = m_ctx.editableMesh.getFaceVertices(twinFaceIdx);
    uint32_t twinV0 = twinFaceVerts[twinEdgeIdx];
    uint32_t twinV1 = twinFaceVerts[(twinEdgeIdx + 1) % twinFaceVerts.size()];

    m_ctx.editableMesh.getVertex(twinV0).uv = reversed ? selUV1 : selUV0;
    m_ctx.editableMesh.getVertex(twinV1).uv = reversed ? selUV0 : selUV1;

    m_ctx.meshDirty = true;
    clearUVEdgeSelection();

    std::cout << "Sewn edge vertices" << std::endl;
}

void ModelingMode::moveAndSewSelectedEdge() {
    if (m_ctx.uvSelectedEdge.first == UINT32_MAX || m_ctx.uvTwinEdges.empty()) return;

    m_ctx.editableMesh.saveState();

    auto [selUV0, selUV1] = getEdgeUVs(m_ctx.uvSelectedEdge.first, m_ctx.uvSelectedEdge.second);
    auto [selPos0, selPos1] = getEdge3DPositions(m_ctx.uvSelectedEdge.first, m_ctx.uvSelectedEdge.second);

    auto [twinFaceIdx, twinEdgeIdx] = m_ctx.uvTwinEdges[0];
    auto [twinUV0, twinUV1] = getEdgeUVs(twinFaceIdx, twinEdgeIdx);
    auto [twinPos0, twinPos1] = getEdge3DPositions(twinFaceIdx, twinEdgeIdx);

    bool reversed = positions3DEqual(selPos0, twinPos1) && positions3DEqual(selPos1, twinPos0);

    // Get both islands
    std::set<uint32_t> selIsland = getUVIslandFaces(m_ctx.uvSelectedEdge.first);
    std::set<uint32_t> selVerts = getIslandVertices(selIsland);
    std::set<uint32_t> twinIsland = getUVIslandFaces(twinFaceIdx);
    std::set<uint32_t> twinVerts = getIslandVertices(twinIsland);

    auto selFaceVerts = m_ctx.editableMesh.getFaceVertices(m_ctx.uvSelectedEdge.first);
    uint32_t selV0 = selFaceVerts[m_ctx.uvSelectedEdge.second];
    uint32_t selV1 = selFaceVerts[(m_ctx.uvSelectedEdge.second + 1) % selFaceVerts.size()];

    auto twinFaceVerts = m_ctx.editableMesh.getFaceVertices(twinFaceIdx);
    uint32_t twinV0 = twinFaceVerts[twinEdgeIdx];
    uint32_t twinV1 = twinFaceVerts[(twinEdgeIdx + 1) % twinFaceVerts.size()];

    // Calculate midpoints of each edge
    glm::vec2 selMid = (selUV0 + selUV1) * 0.5f;
    glm::vec2 twinMid = (twinUV0 + twinUV1) * 0.5f;

    // Calculate the global midpoint where both edges should meet
    glm::vec2 globalMid = (selMid + twinMid) * 0.5f;

    // Calculate final UV positions at the midpoint
    glm::vec2 selDir = glm::normalize(selUV1 - selUV0);
    float selLen = glm::length(selUV1 - selUV0);
    glm::vec2 finalUV0 = globalMid - selDir * (selLen * 0.5f);
    glm::vec2 finalUV1 = globalMid + selDir * (selLen * 0.5f);

    // Move selected island to the midpoint
    glm::vec2 selOffset = globalMid - selMid;
    for (uint32_t vertIdx : selVerts) {
        m_ctx.editableMesh.getVertex(vertIdx).uv += selOffset;
    }

    // Move twin island: rotate to match selected edge direction, then translate to midpoint
    glm::vec2 twinDir = glm::normalize(twinUV1 - twinUV0);
    glm::vec2 targetDir = reversed ? -selDir : selDir;

    float twinAngle = std::atan2(twinDir.y, twinDir.x);
    float targetAngle = std::atan2(targetDir.y, targetDir.x);
    float rotAngle = targetAngle - twinAngle;

    float cosA = std::cos(rotAngle);
    float sinA = std::sin(rotAngle);

    for (uint32_t vertIdx : twinVerts) {
        glm::vec2& uv = m_ctx.editableMesh.getVertex(vertIdx).uv;
        glm::vec2 offset = uv - twinMid;
        glm::vec2 rotated;
        rotated.x = offset.x * cosA - offset.y * sinA;
        rotated.y = offset.x * sinA + offset.y * cosA;
        uv = rotated + globalMid;
    }

    // Snap edge vertices to exact positions
    m_ctx.editableMesh.getVertex(selV0).uv = finalUV0;
    m_ctx.editableMesh.getVertex(selV1).uv = finalUV1;
    m_ctx.editableMesh.getVertex(twinV0).uv = reversed ? finalUV1 : finalUV0;
    m_ctx.editableMesh.getVertex(twinV1).uv = reversed ? finalUV0 : finalUV1;

    m_ctx.meshDirty = true;
    clearUVEdgeSelection();

    std::cout << "Move & Sew: both islands meet at midpoint" << std::endl;
}

void ModelingMode::unsewSelectedEdge() {
    if (m_ctx.uvSelectedEdge.first == UINT32_MAX) return;

    auto selFaceVerts = m_ctx.editableMesh.getFaceVertices(m_ctx.uvSelectedEdge.first);
    uint32_t selV0 = selFaceVerts[m_ctx.uvSelectedEdge.second];
    uint32_t selV1 = selFaceVerts[(m_ctx.uvSelectedEdge.second + 1) % selFaceVerts.size()];

    glm::vec2 selUV0 = m_ctx.editableMesh.getVertex(selV0).uv;
    glm::vec2 selUV1 = m_ctx.editableMesh.getVertex(selV1).uv;

    std::vector<std::pair<uint32_t, uint32_t>> sharedEdges;

    for (uint32_t faceIdx = 0; faceIdx < m_ctx.editableMesh.getFaceCount(); ++faceIdx) {
        if (faceIdx == m_ctx.uvSelectedEdge.first) continue;

        auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
        for (size_t i = 0; i < faceVerts.size(); ++i) {
            size_t next = (i + 1) % faceVerts.size();
            glm::vec2 uv0 = m_ctx.editableMesh.getVertex(faceVerts[i]).uv;
            glm::vec2 uv1 = m_ctx.editableMesh.getVertex(faceVerts[next]).uv;

            bool match = (glm::length(selUV0 - uv0) < 0.0001f && glm::length(selUV1 - uv1) < 0.0001f) ||
                         (glm::length(selUV0 - uv1) < 0.0001f && glm::length(selUV1 - uv0) < 0.0001f);

            if (match) {
                sharedEdges.push_back({faceIdx, static_cast<uint32_t>(i)});
            }
        }
    }

    if (sharedEdges.empty()) {
        std::cout << "Edge is not sewn (no shared UV edges found)" << std::endl;
        return;
    }

    m_ctx.editableMesh.saveState();

    glm::vec2 edgeDir = glm::normalize(selUV1 - selUV0);
    glm::vec2 offsetDir(-edgeDir.y, edgeDir.x);
    float offsetAmount = 0.02f;

    for (auto& [faceIdx, edgeIdx] : sharedEdges) {
        auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
        uint32_t v0 = faceVerts[edgeIdx];
        uint32_t v1 = faceVerts[(edgeIdx + 1) % faceVerts.size()];

        m_ctx.editableMesh.getVertex(v0).uv += offsetDir * offsetAmount;
        m_ctx.editableMesh.getVertex(v1).uv += offsetDir * offsetAmount;
    }

    m_ctx.meshDirty = true;
    findTwinUVEdges(m_ctx.uvSelectedEdge.first, m_ctx.uvSelectedEdge.second);

    std::cout << "Unsewn edge: separated " << sharedEdges.size() << " shared edge(s)" << std::endl;
}

// Bake UV edges onto the texture - draws wireframe lines on top of existing texture
void ModelingMode::bakeUVEdgesToTexture(const glm::vec3& edgeColor, int lineThickness) {
    if (!m_ctx.selectedObject || !m_ctx.selectedObject->hasTextureData()) {
        std::cout << "[UV] Cannot bake edges: no texture data" << std::endl;
        return;
    }

    // Save texture state for undo
    m_ctx.selectedObject->saveTextureState();

    auto& texData = m_ctx.selectedObject->getTextureData();
    int texW = m_ctx.selectedObject->getTextureWidth();
    int texH = m_ctx.selectedObject->getTextureHeight();

    // Convert color to bytes
    unsigned char r = static_cast<unsigned char>(std::clamp(edgeColor.x * 255.0f, 0.0f, 255.0f));
    unsigned char g = static_cast<unsigned char>(std::clamp(edgeColor.y * 255.0f, 0.0f, 255.0f));
    unsigned char b = static_cast<unsigned char>(std::clamp(edgeColor.z * 255.0f, 0.0f, 255.0f));

    // Helper lambda to draw a pixel with thickness
    auto drawPixel = [&](int cx, int cy) {
        int halfThick = lineThickness / 2;
        for (int dy = -halfThick; dy <= halfThick; ++dy) {
            for (int dx = -halfThick; dx <= halfThick; ++dx) {
                int px = cx + dx;
                int py = cy + dy;
                if (px >= 0 && px < texW && py >= 0 && py < texH) {
                    size_t idx = (static_cast<size_t>(py) * texW + px) * 4;
                    texData[idx + 0] = r;
                    texData[idx + 1] = g;
                    texData[idx + 2] = b;
                    // Keep alpha unchanged
                }
            }
        }
    };

    // Bresenham's line algorithm
    auto drawLine = [&](int x0, int y0, int x1, int y1) {
        int dx = std::abs(x1 - x0);
        int dy = std::abs(y1 - y0);
        int sx = (x0 < x1) ? 1 : -1;
        int sy = (y0 < y1) ? 1 : -1;
        int err = dx - dy;

        while (true) {
            drawPixel(x0, y0);

            if (x0 == x1 && y0 == y1) break;

            int e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x0 += sx;
            }
            if (e2 < dx) {
                err += dx;
                y0 += sy;
            }
        }
    };

    // Iterate through all faces and draw their edges
    int edgeCount = 0;
    for (uint32_t faceIdx = 0; faceIdx < m_ctx.editableMesh.getFaceCount(); ++faceIdx) {
        auto faceVerts = m_ctx.editableMesh.getFaceVertices(faceIdx);
        if (faceVerts.size() < 3) continue;

        for (size_t i = 0; i < faceVerts.size(); ++i) {
            size_t next = (i + 1) % faceVerts.size();

            glm::vec2 uv0 = m_ctx.editableMesh.getVertex(faceVerts[i]).uv;
            glm::vec2 uv1 = m_ctx.editableMesh.getVertex(faceVerts[next]).uv;

            // Convert UV (0-1) to pixel coordinates (no Y flip - matches paintAt)
            int px0 = static_cast<int>(uv0.x * texW);
            int py0 = static_cast<int>(uv0.y * texH);
            int px1 = static_cast<int>(uv1.x * texW);
            int py1 = static_cast<int>(uv1.y * texH);

            // Clamp to texture bounds
            px0 = std::clamp(px0, 0, texW - 1);
            py0 = std::clamp(py0, 0, texH - 1);
            px1 = std::clamp(px1, 0, texW - 1);
            py1 = std::clamp(py1, 0, texH - 1);

            drawLine(px0, py0, px1, py1);
            edgeCount++;
        }
    }

    // Mark texture as modified and upload to GPU
    m_ctx.selectedObject->markTextureModified();
    uint32_t handle = m_ctx.selectedObject->getBufferHandle();
    m_ctx.modelRenderer.updateTexture(handle, texData.data(), texW, texH);
    m_ctx.selectedObject->clearTextureModified();

    std::cout << "[UV] Baked " << edgeCount << " edges to texture (" << texW << "x" << texH << ")" << std::endl;
}
