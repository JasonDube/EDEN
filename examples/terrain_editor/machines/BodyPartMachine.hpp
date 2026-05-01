#pragma once
/**
 * BodyPartMachine.hpp — Robot body part assembly via control point stitching.
 *
 * When a body part is placed from the hotbar onto another part (via port snap),
 * this machine finds matching numbered control points between the two parts
 * and lerps each vertex pair to the midpoint, creating a seamless seam.
 *
 * CP naming convention:
 *   Part A: "torso_to_head_1", "torso_to_head_2", ...
 *   Part B: "head_to_torso_1", "head_to_torso_2", ...
 *   Match: swap "X_to_Y" ↔ "Y_to_X", same number suffix.
 *
 * Each part also has a port for coarse alignment (handled by PortSnapSystem).
 * The port prevents inside-out orientation; CPs handle vertex-level precision.
 */

#include "../Machine.hpp"
#include "../MachineHost.hpp"
#include "Editor/SceneObject.hpp"
#include "Renderer/ModelRenderer.hpp"

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <iostream>
#include <regex>

using eden::SceneObject;
using eden::ModelVertex;
using eden::ModelRenderer;

class BodyPartMachine : public Machine {
public:
    // ── Result from a stitch operation ────────────────────────

    struct StitchResult {
        bool success = false;
        int verticesMoved = 0;       // number of CP pairs stitched
        std::string error;
    };

    // ── Tracked assembly (for undo on pickup) ─────────────────

    struct StitchedPair {
        SceneObject* partA = nullptr;
        SceneObject* partB = nullptr;
        // Original vertex positions before stitching (for undo)
        struct OriginalVertex {
            uint32_t vertexIndex;
            glm::vec3 originalPos;
        };
        std::vector<OriginalVertex> partAOriginals;
        std::vector<OriginalVertex> partBOriginals;
    };

    // ── Machine interface ─────────────────────────────────────

    bool onInteract(SceneObject* obj, MachineHost& host) override {
        // Body parts are assembled via hotbar placement, not E-key interaction
        (void)obj; (void)host;
        return false;
    }

    void update(float deltaTime, MachineHost& host) override {
        // No per-frame behavior needed (stitching is instant on placement)
        (void)deltaTime; (void)host;
    }

    void onPickup(SceneObject* obj, MachineHost& host) override {
        // When a stitched part is picked up, restore original vertices on the
        // remaining part so it looks clean again
        for (auto it = m_stitched.begin(); it != m_stitched.end(); ) {
            if (it->partA == obj || it->partB == obj) {
                // Restore the OTHER part's vertices
                SceneObject* remaining = (it->partA == obj) ? it->partB : it->partA;
                auto& originals = (it->partA == obj) ? it->partBOriginals : it->partAOriginals;

                if (remaining && remaining->hasMeshData()) {
                    auto verts = remaining->getVertices();
                    for (auto& ov : originals) {
                        if (ov.vertexIndex < verts.size()) {
                            verts[ov.vertexIndex].position = ov.originalPos;
                        }
                    }
                    remaining->setMeshData(verts, remaining->getIndices());

                    // Re-upload to GPU
                    auto* renderer = host.getModelRenderer();
                    if (renderer && remaining->getBufferHandle() != UINT32_MAX) {
                        renderer->updateModelBuffer(remaining->getBufferHandle(), verts);
                    }
                }

                it = m_stitched.erase(it);
            } else {
                ++it;
            }
        }
    }

    void onDelete(SceneObject* obj) override {
        // Remove any stitch records referencing this object
        m_stitched.erase(
            std::remove_if(m_stitched.begin(), m_stitched.end(),
                [obj](const StitchedPair& sp) { return sp.partA == obj || sp.partB == obj; }),
            m_stitched.end());
    }

    void shutdown(MachineHost& host) override {
        // Restore all stitched vertices on shutdown
        for (auto& sp : m_stitched) {
            restoreOriginals(sp.partA, sp.partAOriginals, host);
            restoreOriginals(sp.partB, sp.partBOriginals, host);
        }
        m_stitched.clear();
    }

    bool isRunning(SceneObject* obj) const override {
        for (auto& sp : m_stitched) {
            if (sp.partA == obj || sp.partB == obj) return true;
        }
        return false;
    }

    // ── Body part stitching API ───────────────────────────────

    /**
     * Attempt to stitch two body parts together at their matching CPs.
     * Call this after port snap has aligned the parts.
     *
     * @param newPart   The part just placed from hotbar
     * @param target    The part already in the world that was snapped to
     * @param host      MachineHost for renderer access
     * @return StitchResult with success/failure and count
     */
    StitchResult stitchParts(SceneObject* newPart, SceneObject* target, MachineHost& host) {
        StitchResult result;

        if (!newPart || !target) {
            result.error = "null object";
            return result;
        }
        if (!newPart->hasControlPoints() || !target->hasControlPoints()) {
            result.error = "missing control points";
            return result;
        }
        if (!newPart->hasMeshData() || !target->hasMeshData()) {
            result.error = "missing mesh data";
            return result;
        }

        const auto& newCPs = newPart->getControlPoints();
        const auto& targetCPs = target->getControlPoints();

        // Build matching pairs by number suffix
        // Parse: "X_to_Y_N" → extract N, then find reverse "Y_to_X_N"
        struct CPMatch {
            int number;
            uint32_t newVertIdx;
            uint32_t targetVertIdx;
        };
        std::vector<CPMatch> matches;

        for (const auto& ncp : newCPs) {
            int num = extractCPNumber(ncp.name);
            if (num < 0) continue;

            std::string reverseName = reverseCP(ncp.name);
            if (reverseName.empty()) continue;

            // Find matching CP on target
            for (const auto& tcp : targetCPs) {
                if (tcp.name == reverseName) {
                    matches.push_back({num, ncp.vertexIndex, tcp.vertexIndex});
                    break;
                }
            }
        }

        if (matches.empty()) {
            result.error = "no matching CP pairs found";
            return result;
        }

        // Get mesh data (copies we'll modify)
        auto newVerts = newPart->getVertices();
        auto targetVerts = target->getVertices();

        // Get world transforms
        glm::mat4 newModel = newPart->getTransform().getMatrix();
        glm::mat4 targetModel = target->getTransform().getMatrix();
        glm::mat4 invNewModel = glm::inverse(newModel);
        glm::mat4 invTargetModel = glm::inverse(targetModel);

        // Track originals for undo
        StitchedPair sp;
        sp.partA = newPart;
        sp.partB = target;

        for (auto& m : matches) {
            if (m.newVertIdx >= newVerts.size() || m.targetVertIdx >= targetVerts.size()) {
                std::cout << "[BodyPart] CP #" << m.number << " vertex index out of range, skipping" << std::endl;
                continue;
            }

            // Save originals before modifying
            sp.partAOriginals.push_back({m.newVertIdx, newVerts[m.newVertIdx].position});
            sp.partBOriginals.push_back({m.targetVertIdx, targetVerts[m.targetVertIdx].position});

            // Get world positions of both vertices
            glm::vec3 newWorldPos = glm::vec3(newModel * glm::vec4(newVerts[m.newVertIdx].position, 1.0f));
            glm::vec3 targetWorldPos = glm::vec3(targetModel * glm::vec4(targetVerts[m.targetVertIdx].position, 1.0f));

            // Compute midpoint in world space
            glm::vec3 midpoint = (newWorldPos + targetWorldPos) * 0.5f;

            // Convert midpoint back to each part's local space
            glm::vec3 newLocalMid = glm::vec3(invNewModel * glm::vec4(midpoint, 1.0f));
            glm::vec3 targetLocalMid = glm::vec3(invTargetModel * glm::vec4(midpoint, 1.0f));

            newVerts[m.newVertIdx].position = newLocalMid;
            targetVerts[m.targetVertIdx].position = targetLocalMid;

            result.verticesMoved++;
        }

        if (result.verticesMoved == 0) {
            result.error = "all CP pairs had invalid vertex indices";
            return result;
        }

        // Update CPU mesh data on both objects
        newPart->setMeshData(newVerts, newPart->getIndices());
        target->setMeshData(targetVerts, target->getIndices());

        // Re-upload to GPU
        auto* renderer = host.getModelRenderer();
        if (renderer) {
            if (newPart->getBufferHandle() != UINT32_MAX) {
                renderer->updateModelBuffer(newPart->getBufferHandle(), newVerts);
            }
            if (target->getBufferHandle() != UINT32_MAX) {
                renderer->updateModelBuffer(target->getBufferHandle(), targetVerts);
            }
        }

        m_stitched.push_back(sp);

        std::cout << "[BodyPart] Stitched " << result.verticesMoved << " vertex pairs between "
                  << newPart->getName() << " and " << target->getName() << std::endl;

        result.success = true;
        return result;
    }

    /**
     * Check if a SceneObject is a body part (has CPs with the X_to_Y naming pattern).
     */
    static bool isBodyPart(SceneObject* obj) {
        if (!obj || !obj->hasControlPoints()) return false;
        const auto& cps = obj->getControlPoints();
        for (const auto& cp : cps) {
            if (cp.name.find("_to_") != std::string::npos) {
                int num = extractCPNumber(cp.name);
                if (num >= 0) return true;
            }
        }
        return false;
    }

private:
    std::vector<StitchedPair> m_stitched;

    // ── Helpers ───────────────────────────────────────────────

    /**
     * Extract the trailing number from a CP name like "torso_to_head_5" → 5
     * Returns -1 if no number suffix found.
     */
    static int extractCPNumber(const std::string& name) {
        auto lastUS = name.rfind('_');
        if (lastUS == std::string::npos || lastUS + 1 >= name.size()) return -1;
        std::string tail = name.substr(lastUS + 1);
        for (char c : tail) {
            if (!isdigit(c)) return -1;
        }
        return std::stoi(tail);
    }

    /**
     * Reverse a CP name: "torso_to_head_5" → "head_to_torso_5"
     * Returns empty string if the name doesn't match the X_to_Y_N pattern.
     */
    static std::string reverseCP(const std::string& name) {
        // Find "_to_" in the name
        auto toPos = name.find("_to_");
        if (toPos == std::string::npos) return "";

        std::string source = name.substr(0, toPos);
        std::string rest = name.substr(toPos + 4);  // after "_to_"

        // Split rest into destination and number: "head_5" → dest="head", num="_5"
        auto lastUS = rest.rfind('_');
        if (lastUS == std::string::npos) return "";

        std::string dest = rest.substr(0, lastUS);
        std::string numSuffix = rest.substr(lastUS);  // includes the underscore

        // Verify the suffix is actually a number
        if (numSuffix.size() < 2) return "";
        for (size_t i = 1; i < numSuffix.size(); i++) {
            if (!isdigit(numSuffix[i])) return "";
        }

        return dest + "_to_" + source + numSuffix;
    }

    void restoreOriginals(SceneObject* obj, const std::vector<StitchedPair::OriginalVertex>& originals, MachineHost& host) {
        if (!obj || !obj->hasMeshData() || originals.empty()) return;
        auto verts = obj->getVertices();
        for (auto& ov : originals) {
            if (ov.vertexIndex < verts.size()) {
                verts[ov.vertexIndex].position = ov.originalPos;
            }
        }
        obj->setMeshData(verts, obj->getIndices());
        auto* renderer = host.getModelRenderer();
        if (renderer && obj->getBufferHandle() != UINT32_MAX) {
            renderer->updateModelBuffer(obj->getBufferHandle(), verts);
        }
    }
};
