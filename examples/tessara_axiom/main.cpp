// TESSARA:AXIOM -- surface walker slice
//
// A first cut at the 3D version. One creature crawling a heightfield, in Vulkan,
// with the gait ported from the LIME walker experiment.
//
// The point of this example is to answer one question by looking at it: does the
// node-standing inchworm read as a creature worth building a colony out of? So
// everything that would obscure that answer -- power, shards, sanctums, unit
// types -- is deliberately absent, and everything that affects how it MOVES is
// on a slider.
//
// You are ON FOOT, always. There were four cameras and cycling them to get to
// the one that matters was a tax on every test.
//
//   WASD           walk        (Shift to run, Space to jump)
//   mouse          look
//   Tab            cycle view: solid -> ghost -> diagram lattice
//   P              pause
//   E              talk to the biped, or work the ship's ramp control --
//                  whichever you are standing next to
//   H              biped holds still and faces you, for a proper look
//   R / B          drop the walker / the biped somewhere new
//   G              drop a fresh crate for him to fetch
//   Escape         free the cursor for the panels; Enter or a click to walk again
//   Escape again   quit
//
// Gravity and slope limits come from eden::Camera's walk mode, fed the same
// heightfield the creatures stand on -- you and they are held up by one
// surface rather than by two that have to be kept in step.

#include "Biped.hpp"
#include "Conversation.hpp"
#include "BipedMesh.hpp"
#include "CreatureMesh.hpp"
#include "Heightfield.hpp"
#include "ScenePipeline.hpp"
#include "SceneVertex.hpp"
#include "Ground.hpp"
#include "Ship.hpp"
#include "Walker.hpp"

#include <eden/Camera.hpp>
#include <eden/Input.hpp>
#include <eden/Window.hpp>

#include "Renderer/Buffer.hpp"
#include "Renderer/ModelRenderer.hpp"
#include "Renderer/ImGuiManager.hpp"
#include "Renderer/Swapchain.hpp"
#include "Renderer/VulkanApplicationBase.hpp"
#include "Renderer/VulkanContext.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <functional>
#include <random>
#include <vector>

using namespace tessara;

namespace {

constexpr int   kGridN        = 128;
constexpr float kNodeSpacing  = 2.0f;
// Chosen by measurement, not by eye: at relief 5 this terrain has effectively no
// walls in it at 45 degrees and the slope rule never fires, so he never refuses
// anything and the character disappears. At 11 it refuses about 3.6% of steps
// and still covers 92% of the field from the worst drop point. Past about 13 the
// ridge becomes a real barrier that partitions the field -- worth seeing, which
// is why the slider goes there, but not the default.
constexpr float kDefaultRelief = 11.0f;

// Generous: the creature is ~16 boxes, but the shape is still being decided.
// Room for a loaded head model on top of the hand-built parts. The sentinel
// head alone is ~6.8k vertices, which is more than everything else put together.
constexpr uint32_t kMaxCreatureVerts   = 49152;
constexpr uint32_t kMaxCreatureIndices = 98304;

// Eye height for the first-person mode. Set to roughly the biped's own eye
// level rather than a human 1.7, so you stand face to face with him instead of
// looking up at a machine three times your size -- the scale of the creatures
// against each other is most of what there is to judge here.
constexpr float kPlayerEyeHeight = 4.0f;

// How wide you are, for the sake of anything solid. Narrower than the biped:
// you are a camera and you would rather squeeze than be held off a wall you can
// see you would fit through.
constexpr float kPlayerRadius    = 0.45f;

// How high a ledge you will step onto. Same distinction the biped draws between a
// slope and a step, and the same reason.
constexpr float kPlayerStepUp    = 0.90f;
constexpr float kCrateSize       = 0.85f;
constexpr float kTalkRange       = 7.0f;
constexpr float kPanelRange      = 4.5f;
constexpr const char* kUnitName  = "SENTINEL-04";
constexpr int   kDefaultCrates   = 6;
constexpr int   kCratesPerLayer  = 4;    // a 2x2 footprint, then stack layers on it

// Solid   -- he is an object in the world.
// Ghost   -- the same object, glassy, with the silhouette carrying the shape.
// Diagram -- the LIME debug view: real 3D positions drawn as a flat overlay,
//            deliberately un-occluded, because you cannot watch a rule fire
//            through a hill.
enum class ViewMode { Solid, Ghost, Diagram };

} // namespace

class TessaraApp : public eden::VulkanApplicationBase {
public:
    TessaraApp()
        : eden::VulkanApplicationBase(1280, 800, "TESSARA:AXIOM - surface walker")
        , m_field(kGridN, kNodeSpacing, kDefaultRelief)
    {}

protected:
    // ------------------------------------------------------------------ init
    void onInit() override {
        m_pipeline = std::make_unique<ScenePipeline>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());

        // Brought in solely for the head: it owns the sampler, the image and the
        // descriptor set, which this example's own pipeline deliberately has
        // none of.
        m_models = std::make_unique<eden::ModelRenderer>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());

        createCreatureBuffers();
        dropWalker();

        // The ship LEVELS the ground it lands on, so it has to be sited before
        // the terrain mesh is built -- otherwise the pad is carved into the
        // heightfield and the thing you can see is still the old hillside.
        glm::vec3 walkerAt = m_walker.bodyCentre(m_ground);
        m_ship.place(m_field, glm::vec2(walkerAt.x + 34.0f, walkerAt.z - 18.0f), 34.0f);

        // Down on arrival. A shut ramp is a sealed hold, and the biped's whole
        // job is now inside it -- he would walk to the door and time out.
        m_ship.toggleRamp();

        uploadTerrain();
        dropBiped();

        // A missing or broken model costs him his head, not the run: he falls
        // back to the box he was born with and the panel says why.
        if (!loadHeadModel("assets/sentinel_biped_head.glb", m_head, m_headError)) {
            std::printf("head model not loaded (%s) - using the box head\n",
                        m_headError.c_str());
        } else {
            // Uploaded once, texture and all. Only its matrix changes per frame.
            m_head.handle = m_models->createModel(
                m_head.vertices, m_head.indices,
                m_head.textured ? m_head.texture.data() : nullptr,
                m_head.texWidth, m_head.texHeight);

            std::printf("head model: %zu verts, %zu triangles, texture %s (%dx%d)\n",
                        m_head.vertices.size(), m_head.indices.size() / 3,
                        m_head.textured ? "yes" : "none",
                        m_head.texWidth, m_head.texHeight);
        }

        // Cargo goes in the hold now, which means every delivery is a walk up
        // the ramp.
        m_storage = m_ship.bayStoragePoint();
        scatterCrates();

        if (!isKmsMode()) {
            m_imgui.init(getContext(), getSwapchain(), getWindow().getHandle(),
                         "imgui_tessara_axiom.ini");
        }

        enterWalkMode();

        m_chat.start();

        std::printf("TESSARA:AXIOM - %dx%d nodes at %.1f spacing (%.0f units across)\n",
                    m_field.n(), m_field.n(), m_field.spacing(),
                    m_field.n() * m_field.spacing());
        std::printf("WASD walk, mouse look, Space jump, Shift run\n");
        std::printf("Tab: view (solid/ghost/diagram)   P: pause\n");
        std::printf("R: redrop walker   B: redrop biped   G: new crates\n");
        std::printf("H: hold him still   E: talk to him, or work the ship ramp   P: pause\n");
        std::printf("Esc: free the cursor (Enter or click to walk again), then quit\n");
    }

    void onCleanup() override {
        m_chat.shutdown();
        m_imgui.cleanup();
        for (auto& buffers : m_creature) {
            buffers.vertex.reset();
            buffers.index.reset();
        }
        m_models.reset();
        m_pipeline.reset();
    }

    void onSwapchainRecreated() override {
        // The pipeline bakes a static viewport at creation, so it has to be
        // rebuilt at the new size or the scene renders into a stale rectangle.
        m_pipeline = std::make_unique<ScenePipeline>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());

        // Bakes a static viewport too, and keeps its uploaded models.
        if (m_models) {
            m_models->recreatePipeline(getSwapchain().getRenderPass(),
                                       getSwapchain().getExtent());
        }
    }

    // ---------------------------------------------------------------- update
    void update(float deltaTime) override {
        handleKeys();

        if (!m_paused) {
            m_ship.update(deltaTime);

            // Republished each frame because the ramp swings, and BEFORE anything
            // walks -- both creatures read this now, and a frame where the ship
            // has moved but its surfaces have not been re-published is a frame
            // where they are standing on where it used to be.
            // Two patches today; when the interior becomes real geometry it is
            // more of the same.
            m_ground.clearPatches();
            m_ground.addPatch(m_ship.deckPatch());
            m_ground.addPatch(m_ship.rampPatch());

            // The solids do not move -- but they are republished on the same
            // line as the patches anyway, because the alternative is remembering
            // to rebuild them from the two places that set the ship down, and
            // that is the kind of thing nobody remembers the second time.
            std::vector<Blocker> solids;
            m_ship.appendBlockers(solids);

            m_ground.clearBlockers();
            for (const Blocker& b : solids) m_ground.addBlocker(b);

            // And the hold as a room with one door, so anything walking between
            // the bay and the field goes round to the ramp rather than at the
            // nearest wall.
            m_ground.clearEnclosures();
            m_ground.addEnclosure(m_ship.enclosure());

            m_walker.update(m_ground, deltaTime);

            if (m_showBiped) {
                // He can only notice somebody who is actually in the world.
                const glm::vec3 eye = cameraPosition();
                m_biped.update(m_ground, deltaTime, &eye);

                updateHauling();
            }
        }

        updateCamera(deltaTime);
        m_chat.update();

        // Standing in front of somebody is the whole conversation trigger: hold
        // him while you are talking, let him get back to work when you stop.
        if (m_chat.isOpen()) {
            m_biped.setHold(true);

            // Walking off ends it, the way it would if you turned and left.
            if (distanceToBiped() > kTalkRange * 1.7f) endConversation();
        }

        if (!isKmsMode()) {
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            drawTuningPanel();
            if (m_view == ViewMode::Diagram) drawDiagnosticOverlay();
            if (m_chat.draw()) endConversation();
            if (!m_mouseLook && !m_chat.isOpen()) drawResumeHint();
            if (!m_chat.isOpen()) drawTalkPrompt();

            ImGui::Render();
        }
    }

    // ---------------------------------------------------------------- record
    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) override {
        // Built and uploaded HERE rather than in update(), because this runs
        // after the fence for this frame index has been waited on. Writing the
        // host-visible buffer any earlier could land while the GPU is still
        // reading the previous submission that used it.
        buildCreatureMesh(m_ground, m_walker, m_creatureVertices, m_creatureIndices);
        if (m_showBiped) appendBipedMesh(m_biped, m_head, m_creatureVertices, m_creatureIndices);
        appendScenery();
        appendShipMesh(m_ship, m_creatureVertices, m_creatureIndices);
        if (m_view == ViewMode::Ghost) sortCreatureBackToFront();
        uploadCreature();

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkRenderPassBeginInfo passInfo{};
        passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        passInfo.renderPass = getSwapchain().getRenderPass();
        passInfo.framebuffer = getSwapchain().getFramebuffers()[imageIndex];
        passInfo.renderArea.offset = {0, 0};
        passInfo.renderArea.extent = getSwapchain().getExtent();

        std::array<VkClearValue, 2> clears{};
        // Deep liminal blue rather than sky blue: this is a battlefield in
        // reality's margins, not a flight sim.
        clears[0].color = {{0.043f, 0.055f, 0.078f, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};
        passInfo.clearValueCount = static_cast<uint32_t>(clears.size());
        passInfo.pClearValues = clears.data();

        vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getHandle());

        const glm::mat4 vp = viewProjection(true);
        const glm::vec4 opaque(cameraPosition(), 0.0f);

        // Terrain. Dimmed under the diagram view so the lattice overlay reads
        // against it, but still drawn -- an overlay with nothing behind it has no
        // depth, and depth is most of what makes the ridge legible.
        if (auto* terrain = getBufferManager().getMeshBuffers(m_terrainHandle)) {
            if (terrain->vertexBuffer && terrain->indexBuffer) {
                ScenePush push{vp,
                               m_view == ViewMode::Diagram ? glm::vec4(0.30f, 0.32f, 0.38f, 1.0f)
                                                           : glm::vec4(1.0f),
                               opaque};
                bindAndDraw(cmd, terrain->vertexBuffer->getHandle(),
                            terrain->indexBuffer->getHandle(), terrain->indexCount, push);
            }
        }

        // Creature. In the diagram view the overlay draws the body instead.
        if (m_view != ViewMode::Diagram && !m_creatureIndices.empty()) {
            const CreatureBuffers& buffers = m_creature[getCurrentFrame()];
            if (buffers.vertex && buffers.index) {
                const bool ghost = m_view == ViewMode::Ghost;
                if (ghost) {
                    // Drawn after the terrain, so the opaque world is already in
                    // the depth buffer and he blends against it correctly.
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      m_pipeline->getGhostHandle());
                }

                ScenePush push{vp,
                               glm::vec4(1.0f, 1.0f, 1.0f, ghost ? m_ghostAlpha : 1.0f),
                               glm::vec4(cameraPosition(), ghost ? 1.0f : 0.0f)};

                bindAndDraw(cmd, buffers.vertex->getHandle(), buffers.index->getHandle(),
                            static_cast<uint32_t>(m_creatureIndices.size()), push);
            }
        }

        // The head, last of the geometry: it binds its own pipeline and its own
        // descriptor set, so anything drawn after it would have to rebind.
        if (m_showBiped && m_head.loaded && m_head.handle != UINT32_MAX) {
            glm::mat4 vpNoFlip = viewProjection(true);
            float lit = m_biped.isWatching() ? 1.45f : 1.0f;
            // twoSided is NOT "disable culling" in ModelRenderer -- it is x-ray
            // mode, and it forces alpha to 0.4 with depth writes off. Passing it
            // is what made the head see-through.
            m_models->render(cmd, vpNoFlip, m_head.handle, m_head.transform(m_biped),
                             0.0f, 1.0f, lit,
                             /*twoSided*/ false, /*indoor*/ false,
                             /*transparent*/ m_view == ViewMode::Ghost);
        }

        if (!isKmsMode()) {
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        }

        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
    }

private:
    struct CreatureBuffers {
        std::unique_ptr<eden::Buffer> vertex;
        std::unique_ptr<eden::Buffer> index;
    };

    // ------------------------------------------------------------- resources
    void uploadTerrain() {
        std::vector<SceneVertex> vertices;
        std::vector<uint32_t> indices;
        m_field.buildMesh(vertices, indices);

        if (m_terrainHandle != UINT32_MAX) {
            getContext().waitIdle();
            getBufferManager().destroyMeshBuffers(m_terrainHandle);
        }

        m_terrainHandle = getBufferManager().createMeshBuffers(
            vertices.data(), static_cast<uint32_t>(vertices.size()), sizeof(SceneVertex),
            indices.data(), static_cast<uint32_t>(indices.size()));
    }

    // Host-visible and one set per frame in flight: the creature is rebuilt every
    // frame, and writing a buffer the previous frame is still reading would tear.
    void createCreatureBuffers() {
        for (auto& buffers : m_creature) {
            buffers.vertex = std::make_unique<eden::Buffer>(
                getContext(), kMaxCreatureVerts * sizeof(SceneVertex),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            buffers.index = std::make_unique<eden::Buffer>(
                getContext(), kMaxCreatureIndices * sizeof(uint32_t),
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        }
    }

    void uploadCreature() {
        if (m_creatureVertices.size() > kMaxCreatureVerts ||
            m_creatureIndices.size() > kMaxCreatureIndices) {
            if (!m_warnedCreatureSize) {
                std::cerr << "tessara: creature mesh outgrew its buffers ("
                          << m_creatureVertices.size() << " verts, "
                          << m_creatureIndices.size() << " indices) - not drawn\n";
                m_warnedCreatureSize = true;
            }
            m_creatureIndices.clear();
            return;
        }

        CreatureBuffers& buffers = m_creature[getCurrentFrame()];
        buffers.vertex->upload(m_creatureVertices.data(),
                               m_creatureVertices.size() * sizeof(SceneVertex));
        buffers.index->upload(m_creatureIndices.data(),
                              m_creatureIndices.size() * sizeof(uint32_t));
    }

    // Painter's algorithm over the creature's own triangles.
    //
    // With depth writes off, whichever translucent triangle is drawn last wins,
    // so his far side can paint over his near side and he turns inside out as he
    // rotates. Sorting fixes it exactly, and at ~400 triangles it is far cheaper
    // than any of the order-independent techniques -- this is the rare case
    // where the brute-force answer is also the right one.
    void sortCreatureBackToFront() {
        const size_t triCount = m_creatureIndices.size() / 3;
        if (triCount < 2) return;

        const glm::vec3 eye = cameraPosition();

        m_sortOrder.resize(triCount);
        m_sortDepth.resize(triCount);
        for (size_t t = 0; t < triCount; ++t) {
            const glm::vec3& a = m_creatureVertices[m_creatureIndices[t * 3 + 0]].pos;
            const glm::vec3& b = m_creatureVertices[m_creatureIndices[t * 3 + 1]].pos;
            const glm::vec3& c = m_creatureVertices[m_creatureIndices[t * 3 + 2]].pos;

            glm::vec3 centroid = (a + b + c) * (1.0f / 3.0f);
            glm::vec3 toEye = centroid - eye;

            m_sortOrder[t] = static_cast<uint32_t>(t);
            m_sortDepth[t] = glm::dot(toEye, toEye);   // squared: the order is all that matters
        }

        std::sort(m_sortOrder.begin(), m_sortOrder.end(),
                  [this](uint32_t lhs, uint32_t rhs) {
                      return m_sortDepth[lhs] > m_sortDepth[rhs];   // farthest first
                  });

        m_sortedIndices.resize(m_creatureIndices.size());
        for (size_t t = 0; t < triCount; ++t) {
            const uint32_t src = m_sortOrder[t];
            m_sortedIndices[t * 3 + 0] = m_creatureIndices[src * 3 + 0];
            m_sortedIndices[t * 3 + 1] = m_creatureIndices[src * 3 + 1];
            m_sortedIndices[t * 3 + 2] = m_creatureIndices[src * 3 + 2];
        }
        m_creatureIndices.swap(m_sortedIndices);
    }

    glm::vec3 cameraPosition() const { return m_camera.getPosition(); }

    // Where crate number `n` sits in the pile: a 2x2 footprint per layer, with
    // each layer stacked on the one below.
    glm::vec3 stackSlot(int n) const {
        int layer  = n / kCratesPerLayer;
        int within = n % kCratesPerLayer;

        float sx = (within & 1) ? 0.5f : -0.5f;
        float sz = (within & 2) ? 0.5f : -0.5f;
        float gap = kCrateSize * 1.04f;

        return m_storage + glm::vec3(sx * gap,
                                     kCrateSize * (0.5f + layer * 1.02f),
                                     sz * gap);
    }

    void appendScenery() {
        appendStoragePad(m_storage, 1.6f, m_stored > 0,
                         m_creatureVertices, m_creatureIndices);

        // The foot of the ramp, marked. Only worth drawing while there is a way
        // up to muster for -- with the ramp shut it is a circle on the dirt in
        // front of a closed door.
        if (m_ship.rampProgress() > 0.15f) {
            appendApproachMark(m_ship.rampApproachPoint(), 1.5f, m_biped.hasCargo(),
                               m_creatureVertices, m_creatureIndices);
        }

        for (const Crate& crate : m_crates) {
            // Carried, it turns with him; otherwise it keeps the facing it was
            // put down with.
            int index = static_cast<int>(&crate - m_crates.data());

            glm::vec3 right(std::cos(crate.yaw), 0, -std::sin(crate.yaw));
            glm::vec3 up(0, 1, 0);
            glm::vec3 fwd(std::sin(crate.yaw), 0, std::cos(crate.yaw));

            if (index == m_haul[0].crate && m_biped.hasCargo()) {
                right = m_biped.right();
                fwd   = m_biped.forward();
            } else if (index == m_haul[1].crate && m_walker.hasCargo()) {
                // Riding the shell, so it tilts with him on a slope.
                up    = m_walker.bodyUp(m_ground);
                fwd   = m_walker.bodyForward(m_ground);
                right = glm::normalize(glm::cross(up, fwd));   // up x forward: right-handed
            }

            appendCrate(crate.position, kCrateSize, right, up, fwd,
                        m_creatureVertices, m_creatureIndices);
        }
    }

    // Scatter a fresh set and start the haul over.
    void scatterCrates() {
        m_crates.clear();
        m_haul[0] = m_haul[1] = Hauler{};   // clears the refusal lists too
        m_freeSlots.clear();
        m_nextSlot = 0;
        m_stored = 0;
        m_biped.abandonTask();
        m_walker.abandonTask();

        std::uniform_real_distribution<float> spread(-46.0f, 46.0f);
        std::uniform_real_distribution<float> spin(0.0f, 6.2831853f);
        const float half = m_field.n() * 0.5f * m_field.spacing() - 12.0f;

        while (static_cast<int>(m_crates.size()) < m_crateCount) {
            float x = m_storage.x + spread(m_rng);
            float z = m_storage.z + spread(m_rng);
            if (std::fabs(x) > half || std::fabs(z) > half) continue;

            // Not on a slope too steep to stand on and pick something up from,
            // and not on top of the pile.
            if (m_field.normalAtWorld(x, z).y < 0.90f) continue;
            if (glm::length(glm::vec2(x - m_storage.x, z - m_storage.z)) < 6.0f) continue;

            // Not inside or under the ship: a crate there is unreachable and
            // looks like a bug even though it is only bad placement.
            glm::vec3 shipTo = glm::vec3(x, m_ship.origin().y, z) - m_ship.origin();
            if (std::fabs(glm::dot(shipTo, m_ship.forward())) < m_ship.params.length * 0.5f + 8.0f &&
                std::fabs(glm::dot(shipTo, m_ship.right()))   < m_ship.params.width * 0.5f + 5.0f) continue;

            Crate crate;
            crate.position = glm::vec3(x, m_field.heightAtWorld(x, z) + kCrateSize * 0.5f, z);
            crate.yaw = spin(m_rng);
            m_crates.push_back(crate);
        }
    }

    int takeSlot() {
        if (!m_freeSlots.empty()) {
            int slot = m_freeSlots.back();
            m_freeSlots.pop_back();
            return slot;
        }
        return m_nextSlot++;
    }

    // Nearest crate nobody else has claimed.
    int claimNearestCrate(const glm::vec3& from, int self) const {
        int best = -1;
        float bestDistance = 1e9f;
        for (size_t i = 0; i < m_crates.size(); ++i) {
            if (m_crates[i].stored) continue;
            if (m_haul[1 - self].crate == static_cast<int>(i)) continue;   // taken
            if (std::find(m_haul[self].refused.begin(), m_haul[self].refused.end(),
                          static_cast<int>(i)) != m_haul[self].refused.end()) continue;
            float d = glm::length(glm::vec2(m_crates[i].position.x - from.x,
                                            m_crates[i].position.z - from.z));
            if (d < bestDistance) { bestDistance = d; best = static_cast<int>(i); }
        }
        return best;
    }

    // One pass of the haul loop, driven by whichever creature `self` is. Both
    // work the same way; only the queries differ, which is the point -- a
    // node-locked crawler and a walking biped end up sharing one job.
    void runHauler(int self, bool active, bool carrying,
                   const std::function<bool()>& busyNow,
                   const glm::vec3& position, float dropYaw,
                   const std::function<void(const glm::vec3&, const glm::vec3&)>& assign,
                   const std::function<glm::vec3()>& cargo)
    {
        const bool busy = busyNow();
        Hauler& h = m_haul[self];

        if (!active) {
            if (h.crate >= 0) { m_freeSlots.push_back(h.slot); h.crate = h.slot = -1; }
            return;
        }

        if (h.crate >= 0) {
            if (carrying) {
                m_crates[h.crate].position = cargo();
            } else if (!busy) {
                if (h.wasCarrying) {
                    // Delivered. Snap to the reserved slot -- both get within a
                    // third of a unit, and a tidy pile beats an honest one.
                    m_crates[h.crate].position = stackSlot(h.slot);
                    m_crates[h.crate].yaw = dropYaw;
                    m_crates[h.crate].stored = true;
                    ++m_stored;
                } else {
                    m_freeSlots.push_back(h.slot);   // gave up before lifting it
                }
                h.crate = h.slot = -1;
            }
        }
        h.wasCarrying = carrying;

        if (h.crate < 0 && !busy && !carrying) {
            int next = claimNearestCrate(position, self);
            if (next >= 0) {
                int slot = takeSlot();
                assign(m_crates[next].position, stackSlot(slot));

                if (busyNow()) {
                    h.crate = next;
                    h.slot  = slot;
                } else {
                    // He looked at it and could not get there. Remember, hand the
                    // slot back, and let the other one have it.
                    h.refused.push_back(next);
                    m_freeSlots.push_back(slot);
                }
            }
        }
    }

    void updateHauling() {
        runHauler(0, m_showBiped, m_biped.hasCargo(),
                  [this] { return m_biped.hasTask(); },
                  m_biped.hipCentre(), m_biped.yawDegrees() * 0.0174533f,
                  [this](const glm::vec3& c, const glm::vec3& s) {
                      // Just the two ends of the job. Which side of the hull he is
                      // on, and which side the crate is on, change while he walks
                      // -- so they are his to keep asking, not the scene's to
                      // decide once and hand him.
                      m_biped.assignFetch(c, s);
                  },
                  [this] { return m_biped.cargoPosition(); });

        runHauler(1, m_walkerHauls, m_walker.hasCargo(),
                  [this] { return m_walker.hasTask(); },
                  m_walker.bodyCentre(m_ground), 0.0f,
                  [this](const glm::vec3& c, const glm::vec3& s) {
                      m_walker.assignFetch(m_ground, c, s);
                  },
                  [this] { return m_walker.cargoPosition(m_ground); });
    }

    void bindAndDraw(VkCommandBuffer cmd, VkBuffer vertexBuffer, VkBuffer indexBuffer,
                     uint32_t indexCount, const ScenePush& push)
    {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(cmd, m_pipeline->getLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(ScenePush), &push);
        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
    }

    // ------------------------------------------------------------- the world
    void dropWalker() {
        // Somewhere walkable, chosen by trying: on a field with a pit and a ridge,
        // a fixed spawn is either always interesting or always not.
        std::uniform_int_distribution<int> pick(2, m_field.n() - 4);

        for (int attempt = 0; attempt < 512; ++attempt) {
            glm::ivec2 block(pick(m_rng), pick(m_rng));
            m_walker.reset(m_ground, block, attempt % 4);
            if (m_walker.canAdvance(m_ground, m_walker.heading())) return;
        }
        m_walker.reset(m_ground, glm::ivec2(m_field.n() / 2, m_field.n() / 3), 0);
    }

    // Put him down near the walker, so you do not have to go looking for two
    // creatures at opposite ends of a 256-unit field.
    void dropBiped() {
        glm::vec3 near = m_walker.bodyCentre(m_ground);
        std::uniform_real_distribution<float> offset(-16.0f, 16.0f);
        std::uniform_real_distribution<float> heading(0.0f, 360.0f);

        m_biped.reset(m_ground,
                      glm::vec2(near.x + offset(m_rng), near.z + offset(m_rng)),
                      heading(m_rng),
                      m_rng());
    }

    void handleKeys() {
        if (isKmsMode()) return;

        if (eden::Input::isKeyPressed(eden::Input::KEY_ESCAPE)) {
            // Escape unwinds one layer at a time. Talking is the innermost, and
            // it has to be checked FIRST: a conversation always runs with the
            // cursor already free, so without this the very next branch reads
            // "cursor is free, therefore quit" and drops the whole app the
            // moment you try to end a chat.
            if (m_chat.isOpen())  endConversation();
            else if (m_mouseLook) setMouseLook(false);
            else                  glfwSetWindowShouldClose(getWindow().getHandle(), GLFW_TRUE);
        }

        // Getting back to walking. Clicking the world does it, and so does
        // Enter -- the click is easy to miss if the cursor happens to be over a
        // panel, and there was no way to tell from looking.
        if (!m_mouseLook) {
            bool clickedWorld = eden::Input::isMouseButtonPressed(eden::Input::MOUSE_LEFT) &&
                                !ImGui::GetIO().WantCaptureMouse;
            if (clickedWorld || eden::Input::isKeyPressed(eden::Input::KEY_ENTER)) {
                setMouseLook(true);
            }
        }

        // Keys the UI would otherwise swallow while a slider has focus.
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureKeyboard) return;

        if (eden::Input::isKeyPressed(eden::Input::KEY_TAB)) {
            m_view = m_view == ViewMode::Solid ? ViewMode::Ghost
                   : m_view == ViewMode::Ghost ? ViewMode::Diagram
                                               : ViewMode::Solid;
        }
        // Space is the jump key, so pause is P.
        if (eden::Input::isKeyPressed(eden::Input::KEY_P)) m_paused = !m_paused;
        if (eden::Input::isKeyPressed(eden::Input::KEY_R))     dropWalker();
        if (eden::Input::isKeyPressed(eden::Input::KEY_B))     dropBiped();
        if (eden::Input::isKeyPressed(eden::Input::KEY_G))     scatterCrates();
        if (eden::Input::isKeyPressed(eden::Input::KEY_H)) {
            m_biped.setHold(!m_biped.holding());
        }
        if (eden::Input::isKeyPressed(eden::Input::KEY_E)) {
            switch (nearestInteraction()) {
                case Interact::Ramp:  m_ship.toggleRamp(); break;
                case Interact::Biped: beginConversation();  break;
                default: break;
            }
        }
    }

    // Put the player on the ground, standing a little way off from the walker
    // and looking straight at him.
    void enterWalkMode() {
        const glm::vec3 subject = m_walker.bodyCentre(m_ground);

        const float back = 14.0f;
        glm::vec3 stand = subject + glm::vec3(back * 0.7f, 0.0f, back * 0.7f);
        stand.y = m_field.heightAtWorld(stand.x, stand.z) + kPlayerEyeHeight;

        // Seeded here, because a tracked floor has to start somewhere and this is
        // the one place he is put down rather than walking.
        m_playerFloor = stand.y - kPlayerEyeHeight;

        m_camera.setPosition(stand);
        m_camera.setEyeHeight(kPlayerEyeHeight);
        m_camera.setSpeed(14.0f);
        m_camera.setMaxSlopeAngle(52.0f);
        m_camera.setMovementMode(eden::MovementMode::Walk);
        m_camera.setNoClip(false);

        // atan2(dz, dx) is the yaw convention this camera uses: front.x is
        // cos(yaw) and front.z is sin(yaw).
        glm::vec3 toSubject = subject - stand;
        m_camera.setYaw(glm::degrees(std::atan2(toSubject.z, toSubject.x)));
        m_camera.setPitch(-6.0f);

        setMouseLook(true);
    }

    // Captured mouse means ImGui must stop reacting to a cursor that is being
    // warped back to the centre every frame, or the panels flicker and grab
    // focus at random.
    void setMouseLook(bool on) {
        if (isKmsMode()) return;
        m_mouseLook = on;
        eden::Input::setMouseCaptured(on);

        ImGuiIO& io = ImGui::GetIO();
        if (on) io.ConfigFlags |=  ImGuiConfigFlags_NoMouse;
        else    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }

    void updateWalkCamera(float deltaTime) {
        if (m_mouseLook) {
            glm::vec2 delta = eden::Input::getMouseDelta();
            m_camera.processMouse(delta.x, -delta.y);
        }

        const bool blocked = !isKmsMode() && !m_mouseLook && ImGui::GetIO().WantCaptureKeyboard;
        float speedMult = eden::Input::isKeyDown(eden::Input::KEY_LEFT_SHIFT) ? 2.4f : 1.0f;

        m_camera.updateMovement(
            deltaTime * speedMult,
            !blocked && eden::Input::isKeyDown(eden::Input::KEY_W),
            !blocked && eden::Input::isKeyDown(eden::Input::KEY_S),
            !blocked && eden::Input::isKeyDown(eden::Input::KEY_A),
            !blocked && eden::Input::isKeyDown(eden::Input::KEY_D),
            !blocked && eden::Input::isKeyDown(eden::Input::KEY_SPACE),
            !blocked && eden::Input::isKeyDown(eden::Input::KEY_LEFT_CONTROL),
            // The one line that lets the player walk into the ship. He asks what
            // is under his feet, not what the terrain is doing.
            [this](float x, float z) {
                return m_ground.heightAt(x, z, playerFloorReference(), kPlayerStepUp);
            });

        // Then advance the tracked floor to wherever he has ended up.
        {
            const glm::vec3 eye = m_camera.getPosition();
            m_playerFloor = m_ground.heightAt(eye.x, eye.z, playerFloorReference(),
                                              kPlayerStepUp);
        }

        // The camera's walk mode asks what is under its feet and nothing else,
        // so being stopped by a wall is not something it can do -- it is done to
        // it, here, after the fact. Pushing the eye back out of a solid reads
        // exactly like being stopped by it, because a frame is short enough that
        // the overlap never gets deep enough to see.
        //
        // Done out here rather than inside eden::Camera on purpose: every other
        // example is happy with a height function, and a collision world is a
        // much bigger thing to ask the engine's camera to know about than this
        // example has earned yet.
        {
            glm::vec3 eye = m_camera.getPosition();

            glm::vec2 fixed = m_ground.resolve(glm::vec2(eye.x, eye.z), m_playerFloor,
                                               kPlayerEyeHeight + 0.5f, kPlayerRadius);
            if (fixed != glm::vec2(eye.x, eye.z)) {
                m_camera.setPosition(glm::vec3(fixed.x, eye.y, fixed.y));
            }
        }
    }

    // What the player is standing on, measured from -- and this is the point --
    // the floor he was standing on a moment ago, not from where his eye is.
    //
    // eden::Camera smooths its height toward the ground and never snaps, which is
    // what stops a walk over rough terrain juddering. The cost is that the eye
    // TRAILS the floor whenever it climbs: at 14 units a second up a 21-degree
    // ramp it trails by about 1.2, which is most of the lower half of the ramp.
    //
    // So `eye.y - eyeHeight` is not an answer to "what am I standing on". It says
    // the floor is a unit below where the floor is -- and a body a unit below the
    // ramp is a body UNDER the ramp, which is how the one surface you are trying
    // to walk up becomes the one surface that keeps throwing you off it. It also
    // fed the camera's own slope check, which then compared the terrain against
    // the terrain, found a two-unit cliff where the ramp meets the deck, and
    // refused to climb it.
    //
    // Chained from the last known floor instead -- the same trick as the walker's
    // per-foot heights -- and the eye is left free to follow at its own pace.
    // Airborne, the reference reverts to the feet, or a jump could never find
    // anything to land on that was higher than the floor it left.
    float playerFloorReference() const {
        return std::max(m_playerFloor, m_camera.getPosition().y - kPlayerEyeHeight);
    }

    void updateCamera(float deltaTime) {
        updateWalkCamera(deltaTime);
    }

    float aspect() const {
        VkExtent2D extent = getSwapchain().getExtent();
        return extent.height == 0 ? 1.0f
                                  : static_cast<float>(extent.width) / static_cast<float>(extent.height);
    }

    glm::mat4 viewMatrix() const { return m_camera.getViewMatrix(); }

    // `forVulkan` flips Y for the clip-space convention. The diagnostic overlay
    // projects into ImGui's screen space instead and must NOT have the flip.
    glm::mat4 viewProjection(bool forVulkan) const {
        glm::mat4 proj = m_camera.getProjectionMatrix(aspect(), 0.5f, 3000.0f);
        if (forVulkan) proj[1][1] *= -1.0f;
        return proj * viewMatrix();
    }

    // ----------------------------------------------------------------- panel
    void drawTuningPanel() {
        ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
        ImGui::Begin("Walker");

        const char* viewName = m_view == ViewMode::Solid ? "solid"
                             : m_view == ViewMode::Ghost ? "ghost"
                                                         : "diagram";
        ImGui::Text("view: %s", viewName);
        ImGui::SameLine();
        ImGui::TextDisabled("(Tab)");

        if (m_view == ViewMode::Ghost) {
            ImGui::SliderFloat("ghost alpha", &m_ghostAlpha, 0.05f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("How glassy he is face-on. The silhouette stays opaque\n"
                                  "whatever this says - that is the Fresnel term, and it\n"
                                  "is what keeps the shape readable when the body is not.");
            }
        }
        if (m_mouseLook) {
            ImGui::TextDisabled("WASD walk, Space jump, Shift run, Esc frees the cursor");
        } else {
            ImGui::TextColored(ImVec4(0.72f, 0.88f, 0.29f, 1.0f),
                               "cursor free - ENTER or click the world to walk again");
        }

        ImGui::Separator();

        Walker::Params& p = m_walker.params;

        ImGui::SliderFloat("slope limit", &p.maxSlopeDeg, 15.0f, 75.0f, "%.0f deg");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How steep a step he will accept. Whether it bites at all\n"
                              "depends on the relief below: past about 42 degrees this\n"
                              "terrain has no walls left and he stops refusing anything.");
        }

        ImGui::SliderInt("lookahead", &p.lookahead, 0, 8);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How far ahead each candidate heading is scored for\n"
                              "unwalked ground. At 0 he covers about 10%% of the field\n"
                              "however the bias is set - he traces lines, not area.\n"
                              "Past 4 he starts thrashing on partitioned terrain.");
        }

        ImGui::SliderInt("straight bias", &p.straightBias, 0, 6);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How much carrying straight on is worth. At 0 with random\n"
                              "tie-breaking he turns on two thirds of his ticks and\n"
                              "reads as twitchy rather than purposeful.");
        }

        ImGui::Checkbox("random tie-break", &p.randomTieBreak);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Pick at random between headings that score EXACTLY equal.\n"
                              "Off, the walk is deterministic and falls into limit cycles:\n"
                              "at these knobs it costs 13 points of worst-case coverage,\n"
                              "and elsewhere on the sliders it can collapse to 19%%.");
        }

        ImGui::SliderFloat("gait speed", &p.stepsPerSecond, 0.5f, 30.0f, "%.1f steps/s");
        ImGui::SliderFloat("leg lift", &p.legLiftFrac, 0.0f, 1.0f, "%.2f spacing");
        ImGui::SliderFloat("body hover", &p.bodyHoverFrac, 0.1f, 1.5f, "%.2f spacing");

        ImGui::Separator();

        float relief = m_field.relief();
        if (ImGui::SliderFloat("terrain relief", &relief, 1.0f, 22.0f, "%.1f")) {
            m_field.regenerate(relief);

            // Regenerating wipes the landing pad, so the ship has to re-level
            // before the mesh is rebuilt or it ends up buried in the new hills.
            dropWalker();
            glm::vec3 walkerAt = m_walker.bodyCentre(m_ground);
            m_ship.place(m_field, glm::vec2(walkerAt.x + 34.0f, walkerAt.z - 18.0f), 34.0f);

            uploadTerrain();
            scatterCrates();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How steep the world is, which is what decides whether the\n"
                              "slope limit means anything. Past about 13 the ridge stops\n"
                              "being a slope and becomes a barrier that cuts the field in\n"
                              "two: coverage falls to a third because he can only reach\n"
                              "his own side, which is the terrain being read correctly.");
        }

        if (m_view == ViewMode::Diagram) {
            ImGui::SliderInt("lattice stride", &m_gridStride, 1, 8);
            ImGui::Checkbox("show trail", &m_showTrail);
        }

        ImGui::Separator();

        int total = m_walker.steps() + m_walker.turns() + m_walker.stuckTicks();
        float turnRate = total > 0 ? 100.0f * m_walker.turns() / total : 0.0f;

        ImGui::Text("coverage   %5.1f %%", m_walker.coverage() * 100.0f);
        ImGui::Text("steps      %d", m_walker.steps());
        ImGui::Text("turns      %d  (%.1f %% of ticks)", m_walker.turns(), turnRate);
        ImGui::Text("boxed in   %d ticks", m_walker.stuckTicks());
        ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);

        ImGui::Separator();

        if (ImGui::Button(m_paused ? "run" : "pause")) m_paused = !m_paused;
        ImGui::SameLine();
        if (ImGui::Button("drop somewhere new")) dropWalker();

        ImGui::End();

        drawBipedPanel();
    }

    void drawBipedPanel() {
        ImGui::SetNextWindowPos(ImVec2(16, 470), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
        ImGui::Begin("Biped");

        ImGui::Checkbox("show him", &m_showBiped);
        ImGui::SameLine();
        if (ImGui::Button("drop him nearby")) dropBiped();
        ImGui::SameLine();
        ImGui::TextDisabled("(B)");

        if (!m_showBiped) { ImGui::End(); return; }

        Biped::Params& p = m_biped.params;

        ImGui::SeparatorText("gait");
        ImGui::SliderFloat("walk speed", &p.walkSpeed, 0.5f, 18.0f, "%.1f u/s");
        ImGui::SliderFloat("step time",  &p.stepTime, 0.12f, 1.2f, "%.2f s");
        ImGui::SliderFloat("stride",     &p.strideScale, 0.5f, 2.2f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How far ahead of himself he plants a foot, as a multiple\n"
                              "of the distance he covers during one step. Below 1 he walks\n"
                              "on his heels; far above it he lunges.");
        }
        ImGui::SliderFloat("foot lift", &p.footLift, 0.0f, 1.6f, "%.2f");
        ImGui::SliderFloat("bob",       &p.bobAmount, 0.0f, 0.5f, "%.2f");
        ImGui::SliderFloat("hip sway",  &p.swayAmount, 0.0f, 0.5f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How far the hips lean over the planted foot. At 0 he walks\n"
                              "like a pair of scissors - this is most of what makes two\n"
                              "legs look like they are carrying something.");
        }
        ImGui::SliderFloat("arm swing", &p.armSwing, 0.0f, 80.0f, "%.0f deg");

        ImGui::SeparatorText("arms");
        ImGui::SliderFloat("upper arm", &p.upperArm, 0.3f, 2.5f, "%.2f");
        ImGui::SliderFloat("forearm",   &p.forearm, 0.3f, 2.5f, "%.2f");
        ImGui::SliderFloat("arm reach", &p.armExtend, 0.45f, 0.99f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How far down the arm the hand sits, as a fraction of full\n"
                              "extension. At 0.99 the arm locks straight and the elbow\n"
                              "stops reading - the same trap as 'stand' on the legs.");
        }
        ImGui::SliderFloat("shoulders", &p.shoulderWidthFrac, 0.3f, 1.4f, "%.2f");

        ImGui::SeparatorText("running");
        ImGui::SliderFloat("run above",  &p.runSpeed, 2.0f, 20.0f, "%.1f u/s");
        ImGui::SliderFloat("walk duty",  &p.walkDuty, 0.50f, 0.85f, "%.2f");
        ImGui::SliderFloat("run duty",   &p.runDuty, 0.15f, 0.50f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Fraction of each foot's cycle spent on the ground.\n"
                              "Above 0.50 the two stances overlap and a foot is always\n"
                              "down: that is a walk. Below it they leave a gap where\n"
                              "neither foot is down, and THAT gap is the flight phase.");
        }
        ImGui::SliderFloat("push off",   &p.pushAmount, 0.0f, 0.09f, "%.3f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How much further the support leg extends through the back\n"
                              "half of stance. The hips travel up away from a foot that\n"
                              "is not moving, so the leg straightens because it has to -\n"
                              "the propulsion is the trajectory, not a force.");
        }
        ImGui::SliderFloat("run stand",  &p.runStandFrac, 0.55f, 0.95f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How low he carries himself running. Must be BELOW the walk's\n"
                              "stand height or the leg is already straight and the push-off\n"
                              "has nowhere to extend into - takeoff velocity comes out zero.");
        }
        ImGui::SliderFloat("gravity",    &p.gravity, 5.0f, 60.0f, "%.0f");
        ImGui::Text("%s%s   airborne %.0f%% of the time",
                    m_biped.isRunning() ? "RUNNING" : "walking",
                    m_biped.isAirborne() ? " - in the air" : "",
                    m_biped.airTimeFraction() * 100.0f);

        ImGui::SeparatorText("lean");
        ImGui::SliderFloat("lean per speed", &p.leanPerSpeed, 0.0f, 3.0f, "%.2f deg/u");
        ImGui::SliderFloat("max lean",       &p.maxLean, 0.0f, 45.0f, "%.0f deg");
        ImGui::SliderFloat("lean rate",      &p.leanRate, 0.5f, 15.0f, "%.1f");
        ImGui::SliderFloat("torso twist", &p.torsoTwistRatio, -0.6f, 0.6f, "%.2f x arm");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Counter-rotation about the spine, absorbing the angular\n"
                              "momentum the legs throw every stride.\n\n"
                              "POSITIVE: each shoulder goes forward with its own arm, the\n"
                              "shoulders opposing the HIPS. That is what a body does.\n"
                              "NEGATIVE: the shoulders oppose the arms instead.\n"
                              "0: no twist at all. Drag it and pick.");
        }
        ImGui::Text("leaning    %.1f deg   twisting %+.1f deg",
                    m_biped.torsoPitch(), m_biped.torsoYaw());

        ImGui::SeparatorText("body");
        ImGui::SliderFloat("thigh",     &p.thigh, 0.5f, 3.0f, "%.2f");
        ImGui::SliderFloat("shin",      &p.shin, 0.5f, 3.0f, "%.2f");
        ImGui::SliderFloat("hip width", &p.hipWidth, 0.3f, 2.2f, "%.2f");
        ImGui::SliderFloat("stand",     &p.standFrac, 0.55f, 0.99f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Hip height as a fraction of full leg extension. Push it to\n"
                              "0.99 and the legs lock straight and the knees stop reading;\n"
                              "drop it to 0.6 and he creeps around in a deep crouch.");
        }

        ImGui::SeparatorText("head");
        bool hold = m_biped.holding();
        if (ImGui::Checkbox("hold still and face me (H)", &hold)) m_biped.setHold(hold);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("He stops and turns to look at you, but stays running -\n"
                              "head tracking, ankles and arms all still live. P pauses\n"
                              "everything instead, which shows you a statue.");
        }

        if (m_head.loaded) {
            ImGui::SliderFloat("head scale", &m_head.scale, 0.3f, 6.0f, "%.2f");
            ImGui::SliderFloat("head yaw",   &m_head.yawOffset, -180.0f, 180.0f, "%.0f deg");
            ImGui::SliderFloat("head pitch", &m_head.pitchOffset, -180.0f, 180.0f, "%.0f deg");
            ImGui::SliderFloat("head roll",  &m_head.rollOffset, -180.0f, 180.0f, "%.0f deg");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Which way the MODEL sits. Exporters disagree about which\n"
                                  "axis is up and which way is forward, and the file does not\n"
                                  "say - so rather than guess, turn it until it looks right.\n"
                                  "This one arrived upside down, hence pitch 180.");
            }
            ImGui::SliderFloat("head rise",  &m_head.rise, -0.6f, 1.2f, "%.2f");
            ImGui::TextDisabled(m_head.textured ? "textured (%dx%d)" : "no texture in file",
                                m_head.texWidth, m_head.texHeight);
        } else {
            ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "no model: %s", m_headError.c_str());
        }

        ImGui::SeparatorText("ankles");
        ImGui::SliderFloat("ankle limit", &p.ankleLimitDeg, 0.0f, 70.0f, "%.0f deg");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How far the servo can tilt the foot from level. Past its\n"
                              "stop the foot stays as far over as it goes and the ground\n"
                              "wins - which is what standing on something too steep to\n"
                              "stand on looks like. At 0 the feet stay flat, which is how\n"
                              "they were before this existed.");
        }
        ImGui::SliderFloat("ankle rate", &p.ankleRate, 1.0f, 40.0f, "%.0f");

        ImGui::SeparatorText("steering");
        ImGui::SliderFloat("slope limit", &p.maxSlopeDeg, 10.0f, 70.0f, "%.0f deg");
        ImGui::SliderFloat("turn rate",   &p.turnRate, 20.0f, 360.0f, "%.0f deg/s");
        ImGui::SliderFloat("wander",      &p.wanderRate, 0.0f, 200.0f, "%.0f deg/s");

        ImGui::Separator();
        ImGui::SeparatorText("noticing you");
        ImGui::SliderFloat("field of view", &p.fovDegrees, 20.0f, 180.0f, "%.0f deg");
        ImGui::SliderFloat("look range",    &p.lookRange, 5.0f, 200.0f, "%.0f");
        ImGui::SliderFloat("neck limit",    &p.neckYawLimit, 10.0f, 110.0f, "%.0f deg");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Kept below the field of view on purpose: something at the\n"
                              "edge of vision gets watched out of the corner of his eye\n"
                              "rather than by snapping his head round to it.");
        }
        ImGui::SliderFloat("head speed", &p.headTurnRate, 1.0f, 25.0f, "%.1f");
        ImGui::Checkbox("notice any camera", &m_watchAlways);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Off, he only notices you when you are on foot (F to the\n"
                              "last camera). On, he watches wherever you are looking from.");
        }
        ImGui::TextUnformatted(m_biped.isWatching() ? "he has you"
                                                     : "he has not seen you");

        ImGui::Separator();
        ImGui::Text("steps      %d", m_biped.steps());
        ImGui::Text("refusals   %d", m_biped.refusals());
        ImGui::TextUnformatted(m_biped.refusedLastProbe() ? "currently: refusing, turning away"
                                                          : "currently: walking");

        ImGui::SeparatorText("fetching");
        ImGui::Text("%s", m_biped.activityName());
        ImGui::SliderFloat("stop within",  &p.reachDistance, 0.8f, 4.0f, "%.2f");
        ImGui::SliderFloat("bend angle",   &p.bendAngle, 0.0f, 80.0f, "%.0f deg");
        ImGui::SliderFloat("reach stand",  &p.reachStandFrac, 0.35f, 0.90f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How far he drops his hips to reach the ground. Bending at\n"
                              "the hip alone cannot do it - the shoulder sits 1.1 above\n"
                              "the hips and the arm is 2.2, so he is half a unit short\n"
                              "until he squats.");
        }
        ImGui::SliderFloat("bend rate",    &p.bendRate, 0.5f, 12.0f, "%.1f");
        ImGui::SliderFloat("carry ahead",  &p.carryAhead, 0.2f, 1.6f, "%.2f");
        ImGui::Text("pile: %d of %d", m_stored, static_cast<int>(m_crates.size()));
        ImGui::Text("biped:  %s", m_biped.activityName());
        ImGui::Text("walker: %s", m_walker.activityName());
        ImGui::Checkbox("walker hauls too", &m_walkerHauls);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Off, because the hold is up a ramp. The walker stands on\n"
                              "lattice NODES with terrain heights -- a ramp at an angle to\n"
                              "the grid is not something his world model can express, so he\n"
                              "would drive under the ship and drop crates beneath the deck.");
        }
        ImGui::SliderInt("crates", &m_crateCount, 1, 16);
        if (ImGui::Button("scatter again (G)")) scatterCrates();

        ImGui::End();
    }

    // ------------------------------------------------------------------ chat
    float distanceToBiped() const {
        glm::vec3 eye = cameraPosition();
        glm::vec3 chest = m_biped.chestCentre();
        return glm::length(glm::vec2(eye.x - chest.x, eye.z - chest.z));
    }

    float distanceToPanel() const {
        glm::vec3 eye = cameraPosition();
        glm::vec3 panel = m_ship.controlPosition();
        return glm::length(glm::vec2(eye.x - panel.x, eye.z - panel.z));
    }

    // E means one thing at a time. Whichever you are actually standing next to
    // wins, so there is never a hidden rule about which took priority.
    enum class Interact { None, Biped, Ramp };

    Interact nearestInteraction() const {
        if (m_chat.isOpen()) return Interact::None;

        float toBiped = m_showBiped ? distanceToBiped() : 1e9f;
        float toPanel = distanceToPanel();

        bool biped = toBiped < kTalkRange;
        bool panel = toPanel < kPanelRange;

        if (biped && panel) return toPanel < toBiped ? Interact::Ramp : Interact::Biped;
        if (panel) return Interact::Ramp;
        if (biped) return Interact::Biped;
        return Interact::None;
    }

    void beginConversation() {
        m_biped.setHold(true);          // stop, and turn to face whoever is talking
        m_chat.open(kUnitName);
        setMouseLook(false);            // you need the cursor to type
    }

    void endConversation() {
        m_chat.close();
        m_biped.setHold(false);         // back to the crates
        setMouseLook(true);
    }

    // Only shown when you are close enough for E to do anything, so the prompt
    // is the range indicator rather than a separate thing to explain.
    void drawTalkPrompt() {
        Interact what = nearestInteraction();
        if (what == Interact::None || !m_mouseLook) return;

        VkExtent2D extent = getSwapchain().getExtent();
        char text[96];
        if (what == Interact::Ramp) {
            std::snprintf(text, sizeof(text), "E  -  %s cargo ramp",
                          m_ship.isOpening() ? "close" : "open");
        } else {
            std::snprintf(text, sizeof(text), "E  -  talk to %s", kUnitName);
        }

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 size = ImGui::CalcTextSize(text);
        ImVec2 at((extent.width - size.x) * 0.5f, extent.height * 0.62f);

        dl->AddRectFilled(ImVec2(at.x - 12, at.y - 7), ImVec2(at.x + size.x + 12, at.y + size.y + 7),
                          IM_COL32(12, 16, 22, 200), 5.0f);
        dl->AddRect(ImVec2(at.x - 12, at.y - 7), ImVec2(at.x + size.x + 12, at.y + size.y + 7),
                    IM_COL32(255, 191, 0, 190), 5.0f, 0, 1.5f);
        dl->AddText(at, IM_COL32(240, 235, 215, 255), text);
    }

    // A cursor sitting free with no way back is a dead end. Say so, on screen,
    // where it cannot be missed.
    void drawResumeHint() {
        VkExtent2D extent = getSwapchain().getExtent();
        const char* text = "cursor free  -  press ENTER or click the world to walk again";

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 size = ImGui::CalcTextSize(text);
        ImVec2 at((extent.width - size.x) * 0.5f, extent.height - size.y - 28.0f);

        dl->AddRectFilled(ImVec2(at.x - 14, at.y - 8), ImVec2(at.x + size.x + 14, at.y + size.y + 8),
                          IM_COL32(12, 16, 22, 210), 6.0f);
        dl->AddRect(ImVec2(at.x - 14, at.y - 8), ImVec2(at.x + size.x + 14, at.y + size.y + 8),
                    IM_COL32(183, 224, 74, 190), 6.0f, 0, 1.5f);
        dl->AddText(at, IM_COL32(235, 245, 225, 255), text);
    }

    // -------------------------------------------------------------- overlay
    // Ported from the LIME experiment, because that diagram is what made the
    // gait legible in the first place: the probe rays show the rule being
    // applied, which no amount of solid shading can.
    void drawDiagnosticOverlay() {
        VkExtent2D extent = getSwapchain().getExtent();
        const float vpW = static_cast<float>(extent.width);
        const float vpH = static_cast<float>(extent.height);
        if (vpW <= 0.0f || vpH <= 0.0f) return;

        const glm::mat4 vp = viewProjection(false);

        auto toScreen = [&](const glm::vec3& world, ImVec2& out) -> bool {
            glm::vec4 clip = vp * glm::vec4(world, 1.0f);
            if (clip.w <= 0.0f) return false;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            out = ImVec2((ndc.x + 1.0f) * 0.5f * vpW, (1.0f - ndc.y) * 0.5f * vpH);
            return true;
        };
        auto onScreen = [&](const ImVec2& p) {
            return p.x >= -32.0f && p.y >= -32.0f && p.x <= vpW + 32.0f && p.y <= vpH + 32.0f;
        };

        ImDrawList* dl = ImGui::GetBackgroundDrawList();

        // The lattice, thinned -- 128x128 is 32k edges and only the shape reads.
        m_field.buildGridLines(m_gridStride, m_gridSegments);
        const ImU32 gridCol = IM_COL32(90, 105, 120, 90);
        for (size_t i = 0; i + 1 < m_gridSegments.size(); i += 2) {
            ImVec2 a, b;
            if (toScreen(m_gridSegments[i], a) && toScreen(m_gridSegments[i + 1], b)) {
                if (onScreen(a) || onScreen(b)) dl->AddLine(a, b, gridCol, 1.0f);
            }
        }

        // Where he has already been. This is the coverage claim, drawn.
        if (m_showTrail) {
            const ImU32 trailCol = IM_COL32(90, 200, 255, 80);
            const auto& visited = m_walker.visitedMap();
            for (int y = 0; y < m_field.n(); ++y) {
                for (int x = 0; x < m_field.n(); ++x) {
                    if (!visited[static_cast<size_t>(y) * m_field.n() + x]) continue;
                    ImVec2 s;
                    if (toScreen(m_field.worldAt({x, y}), s) && onScreen(s)) {
                        dl->AddCircleFilled(s, 1.8f, trailCol, 6);
                    }
                }
            }
        }

        // The body: the quad between the four feet.
        ImVec2 s[4];
        bool allVisible = true;
        for (int i = 0; i < 4; ++i) {
            if (!toScreen(m_walker.footWorld(m_ground, i), s[i])) allVisible = false;
        }
        if (allVisible) {
            dl->AddQuadFilled(s[0], s[1], s[2], s[3], IM_COL32(183, 224, 74, 70));
            dl->AddQuad(s[0], s[1], s[2], s[3], IM_COL32(183, 224, 74, 230), 2.0f);
        }

        // Feet. Front ones bright, so which end is leading is never in doubt.
        auto drawFoot = [&](int i, ImU32 col, float r) {
            ImVec2 p;
            if (!toScreen(m_walker.footWorld(m_ground, i), p)) return;
            dl->AddCircleFilled(p, r, col, 12);
            dl->AddCircle(p, r + 2.5f, IM_COL32(255, 255, 255, 120), 12, 1.2f);
        };
        drawFoot(0, IM_COL32(255, 240, 120, 255), 5.0f);
        drawFoot(1, IM_COL32(255, 240, 120, 255), 5.0f);
        drawFoot(2, IM_COL32(255, 150, 90, 230), 4.0f);
        drawFoot(3, IM_COL32(255, 150, 90, 230), 4.0f);

        // What he is considering next, and whether he would accept it.
        const glm::ivec2 d = Walker::dirVec(m_walker.heading());
        for (int i = 0; i < 2; ++i) {
            const glm::ivec2 from = m_walker.footNode(i);
            const glm::ivec2 to   = from + d;
            // Asked from where that foot actually is, so the probe drawn on the
            // ramp is the same question he is asking himself there.
            const bool ok = m_walker.goodStep(m_ground, from, to, m_walker.footHeight(i));
            const ImU32 col = ok ? IM_COL32(140, 255, 160, 220) : IM_COL32(255, 80, 80, 220);

            ImVec2 a, b;
            if (toScreen(m_walker.footWorld(m_ground, i), a) &&
                toScreen(m_ground.worldAt(to, m_walker.footHeight(i), 2.0f), b)) {
                dl->AddLine(a, b, col, 2.0f);
                dl->AddCircle(b, 4.0f, col, 10, 1.6f);
            }
        }
    }

    // ----------------------------------------------------------------- state
    Heightfield m_field;
    Ground      m_ground{m_field};

    // See playerFloorReference().
    float m_playerFloor = 0.0f;
    Walker      m_walker;
    Biped       m_biped;
    HeadModel   m_head;
    Ship        m_ship;
    Conversation m_chat;
    std::string m_headError;

    struct Crate {
        glm::vec3 position{0.0f};
        float     yaw = 0.0f;      // facing it was set down with
        bool      stored = false;
    };
    std::vector<Crate> m_crates;
    glm::vec3 m_storage{0.0f};

    // Two haulers on the same pile. Each holds a claim on one crate and a
    // reservation on one slot, so they cannot both set down in the same place.
    struct Hauler {
        int  crate = -1;
        int  slot  = -1;
        bool wasCarrying = false;

        // Crates this hauler has been proved unable to reach. The walker refuses
        // slopes the biped walks up, so "unreachable" is per-creature, not per
        // crate -- and without remembering it he claims the same impossible crate
        // every frame forever.
        std::vector<int> refused;
    };
    Hauler m_haul[2];              // 0 = biped, 1 = walker
    std::vector<int> m_freeSlots;  // slots handed back when a hauler gives up
    int  m_nextSlot = 0;
    int  m_stored = 0;
    int  m_crateCount = kDefaultCrates;
    // On, now that a route to the pile can actually be planned. It defaulted off
    // for as long as the pile was somewhere he could not be sent.
    bool m_walkerHauls = true;
    bool        m_showBiped = true;
    bool        m_watchAlways = false;

    std::unique_ptr<ScenePipeline> m_pipeline;
    std::unique_ptr<eden::ModelRenderer> m_models;
    uint32_t m_terrainHandle = UINT32_MAX;

    std::array<CreatureBuffers, MAX_FRAMES_IN_FLIGHT> m_creature;
    std::vector<SceneVertex> m_creatureVertices;
    std::vector<uint32_t>    m_creatureIndices;
    bool m_warnedCreatureSize = false;

    // Scratch for the ghost pass's depth sort, kept as members so a frame does
    // not allocate.
    std::vector<uint32_t> m_sortOrder;
    std::vector<float>    m_sortDepth;
    std::vector<uint32_t> m_sortedIndices;
    float m_ghostAlpha = 0.34f;

    std::vector<glm::vec3> m_gridSegments;

    eden::ImGuiManager m_imgui;
    eden::Camera m_camera;

    ViewMode m_view = ViewMode::Solid;
    bool  m_paused     = false;
    bool  m_showTrail  = true;
    int   m_gridStride = 3;

    bool  m_mouseLook        = false;  // mouse captured for first-person look


    std::mt19937 m_rng{0xA71011};   // fixed: a run should be repeatable
};

int main() {
    try {
        TessaraApp app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "tessara_axiom: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
