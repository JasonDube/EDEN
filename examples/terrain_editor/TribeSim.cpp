#include "TribeSim.hpp"

// The bodies below are the code that was in main.cpp, moved rather than
// rewritten -- the sim behaves exactly as it did, and the only edits are the
// ones that turn reaching into the editor's members into asking the host.

#include "Editor/SceneObject.hpp"
#include "Editor/PrimitiveMeshBuilder.hpp"
#include "Renderer/ModelRenderer.hpp"
#include "Renderer/SkinnedModelRenderer.hpp"
#include <eden/Terrain.hpp>
#include <eden/Camera.hpp>
#include <eden/Input.hpp>
#include <eden/Window.hpp>

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>

using namespace eden;

// ---- lifecycle ------------------------------------------------------------

void TribeSim::setEnabled(bool on) {
    if (m_enabled == on) return;
    m_enabled = on;
    if (!on) reset();
}

// Everything the sim remembers, forgotten.
//
// This did not exist before, and its absence was the bug: main.cpp's newLevel()
// destroyed the inhabitants' scene objects but nothing cleared m_inhabitants, so
// a fresh level came up holding records that named bodies which no longer
// existed. Nothing in the binary ever called clear() on any of these.
void TribeSim::reset() {
    m_inhabitants.clear();
    m_selectedInhabitant.clear();
    m_bonds.clear();
    m_tribeCenters.clear();
    m_tribeCounts.clear();
    m_tribeStores.clear();
    m_tribeCamps.clear();
    m_resources.clear();
    m_predators.clear();
    m_pendingKills.clear();
    m_newsFeed.clear();
    m_inhabitantCounter = m_waterCounter = m_foodCounter = m_lionCounter = 0;
    m_dayNumber = 0;
    m_prevSimMinutes = -1.0f;
    // Hand the clock back the way we found it, if we were the one winding it.
    if (m_simDroveClock && m_host.gameTimeScale) *m_host.gameTimeScale = 0.0f;
    m_simDroveClock = false;
}

// ---- input ----------------------------------------------------------------

// The spawn keys, which used to sit in the editor's update() and fire in every
// level. They only exist while the sim does now, which is what frees U and L
// back to the 5-ft grid and the grass brush -- both of which they collided with.
void TribeSim::handleInput(bool isPlayMode, bool guiWantsKeys, bool guiWantsMouse) {
    if (!m_enabled) return;

    // L: spawn a dot inhabitant on the current terrain (editor or play).
    if (Input::isKeyPressed(Input::KEY_L) && !guiWantsKeys) spawnInhabitant();
    // U: drop a water source (thirsty inhabitants path to it).
    if (Input::isKeyPressed(Input::KEY_U) && !guiWantsKeys) spawnWater();
    // O: drop a food source. Plain O only -- Ctrl+O stays Open File.
    {
        const bool ctrlNow = Input::isKeyDown(Input::KEY_LEFT_CONTROL) ||
                             Input::isKeyDown(Input::KEY_RIGHT_CONTROL);
        if (Input::isKeyPressed(Input::KEY_O) && !ctrlNow && !guiWantsKeys) spawnFood();
    }
    // Left click an inhabitant to inspect them (play mode, terrain levels). Only
    // consumes the click if it actually lands on one, so nothing else changes.
    if (isPlayMode && !(*m_host.isEdenOSLevel) && !m_inhabitants.empty() &&
        Input::isMouseButtonPressed(Input::MOUSE_LEFT) && !guiWantsMouse) {
        pickInhabitantAtCursor();
    }
    // Y: release a lion (play mode only -- editor Y is object-snap).
    if (isPlayMode && Input::isKeyPressed(Input::KEY_Y) && !guiWantsKeys) spawnLion();
}

// ---- the frame ------------------------------------------------------------

void TribeSim::update(float deltaTime, bool isPlayMode) {
    if (!m_enabled) return;

    // The sim runs only when the game is playing, and not in EDEN OS (no terrain).
    if (isPlayMode && !(*m_host.isEdenOSLevel)) {
        // Wind the day/night clock, but only where the tribe lives -- akelba and
        // EDEN OS keep the frozen noon they were deliberately parked at.
        if (!m_inhabitants.empty() && !m_simDroveClock &&
            m_host.gameTimeScale && *m_host.gameTimeScale == 0.0f) {
            *m_host.gameTimeScale = kSimDayScale;
            m_simDroveClock = true;
        }
        updateResources(deltaTime);   // must run first -- seeds every source's pool
        updateInhabitants(deltaTime);
        updateReckoning(deltaTime);
        updatePredators(deltaTime);
        // Remove any inhabitants a predator caught this frame (deferred so we
        // never mutate the scene objects mid-loop).
        for (const std::string& name : m_pendingKills) {
            if (m_host.destroyObjectNamed) m_host.destroyObjectNamed(name);
            m_inhabitants.erase(name);
            if (m_selectedInhabitant == name) m_selectedInhabitant.clear();
        }
        m_pendingKills.clear();
    } else if (m_simDroveClock) {
        if (m_host.gameTimeScale) *m_host.gameTimeScale = 0.0f;  // hand the clock back
        m_simDroveClock = false;
        m_prevSimMinutes = -1.0f;
    }
}

// ---- the sim, as it was ---------------------------------------------------

void TribeSim::spawnInhabitant() {
    std::string dot = std::string(CMAKE_SOURCE_DIR) + "/agent_test/heretic.glb";
    if (!std::filesystem::exists(dot)) {
        std::cerr << "[Inhabitant] model missing: " << dot << std::endl;
        return;
    }
    size_t before = sceneObjects().size();
    m_host.importSkinnedModel(dot);   // grounds feet, plays idle, pushes + selects
    // Camps are spawned AFTER the loop — never push_back into sceneObjects()
    // while walking it.
    std::vector<std::pair<int, glm::vec3>> newCamps;
    for (size_t i = before; i < sceneObjects().size(); ++i) {
        auto* o = sceneObjects()[i].get();
        if (!o || !o->isSkinned()) continue;
        o->setBuildingType("inhabitant");
        o->setName("Inhabitant_" + std::to_string(++m_inhabitantCounter));
        glm::vec3 p = o->getTransform().getPosition();

        // Join the nearest tribe within reach, else found a new one.
        constexpr float kJoinRadius = 20.0f;
        int tribe = -1; float best = kJoinRadius;
        for (size_t t = 0; t < m_tribeCenters.size(); ++t) {
            float dx = p.x - m_tribeCenters[t].x, dz = p.z - m_tribeCenters[t].z;
            float d = std::sqrt(dx * dx + dz * dz);
            if (d < best) { best = d; tribe = static_cast<int>(t); }
        }
        if (tribe < 0) {
            tribe = static_cast<int>(m_tribeCenters.size());
            m_tribeCenters.push_back(p);
            m_tribeCounts.push_back(0);
            m_tribeStores.push_back(TribeStore{});
            m_tribeCamps.push_back("");
            newCamps.push_back({tribe, p});   // a new tribe founds a camp
        }
        // Fold into that tribe's running-average centre.
        int c = m_tribeCounts[tribe];
        m_tribeCenters[tribe] = (m_tribeCenters[tribe] * static_cast<float>(c) + p)
                                / static_cast<float>(c + 1);
        m_tribeCounts[tribe]++;

        InhabitantState& s = m_inhabitants[o->getName()];
        s = InhabitantState{};   // fresh state (rest seeded lazily in update)
        s.tribe = tribe;
        std::cout << "[Inhabitant] spawned into tribe " << tribe
                  << " (" << m_tribeCounts[tribe] << " strong)" << std::endl;
    }
    for (const auto& c : newCamps) spawnStockpile(c.first, c.second);
}

// Every tribe gets a CAMP — the crate they haul gathered water and food back to.
// It's the tribe's economy made physical, and the place they come to drink from
// once the pond has been drunk dry.
void TribeSim::spawnStockpile(int tribe, const glm::vec3& at) {
    if (!m_host.models) return;
    glm::vec4 brown(0.55f, 0.38f, 0.22f, 1.0f);
    auto mesh = PrimitiveMeshBuilder::createCube(1.0f, brown);
    uint32_t handle = m_host.models->createModel(mesh.vertices, mesh.indices, nullptr, 0, 0);
    std::string name = "Camp_" + std::string(1, static_cast<char>('A' + (tribe % 26)));
    auto obj = std::make_unique<SceneObject>(name);
    obj->setBufferHandle(handle);
    obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
    obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
    obj->setLocalBounds(mesh.bounds);
    obj->setMeshData(mesh.vertices, mesh.indices);
    obj->setPrimitiveType(PrimitiveType::Cube);
    obj->setPrimitiveSize(1.0f);
    obj->setPrimitiveColor(brown);
    obj->setBuildingType("stockpile");
    glm::vec3 s(1.6f, 1.0f, 1.6f);
    glm::vec3 p = at;
    p.y = m_host.terrain->getHeightAt(p.x, p.z) + s.y * 0.5f;
    obj->getTransform().setPosition(p);
    obj->getTransform().setScale(s);
    sceneObjects().push_back(std::move(obj));
    if (static_cast<int>(m_tribeCamps.size()) <= tribe) m_tribeCamps.resize(tribe + 1);
    m_tribeCamps[tribe] = name;
    std::cout << "[Camp] tribe " << tribe << " founded a camp" << std::endl;
}

std::string TribeSim::bondKey(const std::string& a, const std::string& b) {
    return (a < b) ? (a + "|" + b) : (b + "|" + a);
}
float TribeSim::bondValue(const std::string& a, const std::string& b) const {
    auto it = m_bonds.find(bondKey(a, b));
    return (it == m_bonds.end()) ? 0.0f : it->second;
}
const char* TribeSim::bondWord(float v) {
    return v >  0.60f ? "close friend"
         : v >  0.25f ? "friend"
         : v >  0.05f ? "friendly"
         : v > -0.05f ? "acquaintance"
         : v > -0.30f ? "wary" : "hostile";
}

// Current position of a named object, if it's still in the scene.
bool TribeSim::objectPos(const std::string& name, glm::vec3& out) {
    for (const auto& o : sceneObjects()) {
        if (o && o->getName() == name) {
            out = const_cast<SceneObject*>(o.get())->getTransform().getPosition();
            return true;
        }
    }
    return false;
}

// Where a tribe's camp stands (the haul destination). False if it has none.
bool TribeSim::tribeCamp(int tribe, glm::vec3& out) {
    if (tribe < 0 || tribe >= static_cast<int>(m_tribeCamps.size())) return false;
    const std::string& n = m_tribeCamps[tribe];
    if (n.empty()) return false;
    for (const auto& o : sceneObjects()) {
        if (o && o->getName() == n) {
            out = const_cast<SceneObject*>(o.get())->getTransform().getPosition();
            return true;
        }
    }
    return false;
}

// Draw down and trickle back. Sources shrink with what's left in them, so you can
// SEE the pond drying and the bush picked bare — the scarcity is glanceable, the
// same way the state tags are.
void TribeSim::updateResources(float deltaTime) {
    for (auto& objPtr : sceneObjects()) {
        if (!objPtr) continue;
        const std::string& type = objPtr->getBuildingType();
        bool isWater = (type == "water");
        bool isFood  = (type == "food");
        if (!isWater && !isFood) continue;

        ResourceState& r = m_resources[objPtr->getName()];
        if (!r.init) {
            r.capacity  = isWater ? 60.0f : 40.0f;
            r.amount    = r.capacity;
            r.regen     = isWater ? 1.2f : 0.5f;   // a spring refills; berries regrow slower
            r.fullScale = objPtr->getTransform().getScale();
            r.init = true;
        }
        r.amount = std::min(r.capacity, r.amount + r.regen * deltaTime);
        // Runs dry at the last drop; not worth the walk again until it's had a
        // real refill.
        if (r.amount <= 0.5f) r.exhausted = true;
        if (r.amount >= 8.0f) r.exhausted = false;

        float frac = (r.capacity > 0.0f) ? (r.amount / r.capacity) : 0.0f;
        glm::vec3 s = r.fullScale * (0.25f + 0.75f * frac);   // never vanishes entirely
        objPtr->getTransform().setScale(s);
        glm::vec3 p = objPtr->getTransform().getPosition();
        p.y = m_host.terrain->getHeightAt(p.x, p.z) + (isWater ? 0.05f : s.y * 0.5f);
        objPtr->getTransform().setPosition(p);
    }
}

// Bring inhabitants to life: a lazy wander (idle a beat → pick a nearby point →
// stroll to it → idle) with the body kept stuck to the terrain surface every
// frame, so movement never leaves the ground. Runs in play mode only.
void TribeSim::updateInhabitants(float deltaTime) {
    auto frand = [](float a, float b) {
        return a + (b - a) * (static_cast<float>(rand()) / static_cast<float>(RAND_MAX));
    };
    auto dist2D = [](const glm::vec3& a, const glm::vec3& b) {
        float dx = a.x - b.x, dz = a.z - b.z;
        return std::sqrt(dx * dx + dz * dz);
    };

    // Keep the per-tribe economy arrays in step with the tribes themselves
    // (levels saved before the economy existed come back a tribe short).
    if (m_tribeStores.size() < m_tribeCenters.size()) m_tribeStores.resize(m_tribeCenters.size());
    if (m_tribeCamps.size()  < m_tribeCenters.size()) m_tribeCamps.resize(m_tribeCenters.size());

    // Snapshot the tribe so each member can sense its neighbours this frame.
    std::vector<std::pair<SceneObject*, glm::vec3>> mob;
    for (auto& objPtr : sceneObjects()) {
        if (objPtr && objPtr->getBuildingType() == "inhabitant") {
            // Seed up front: the greeting contest reaches into the OTHER party's
            // state, and an insert mid-loop would invalidate the reference we're
            // holding for this one.
            m_inhabitants[objPtr->getName()];
            mob.push_back({objPtr.get(), objPtr->getTransform().getPosition()});
        }
    }

    constexpr float kNoticeR  = 2.5f;   // greet a neighbour once this close
    constexpr float kPersonal = 1.1f;   // don't crowd nearer than this

    for (auto& entry : mob) {
        SceneObject* obj = entry.first;
        glm::vec3 pos = entry.second;
        InhabitantState& st = m_inhabitants[obj->getName()];

        if (!st.init) {
            st.footLift = pos.y - m_host.terrain->getHeightAt(pos.x, pos.z);
            st.home = pos; st.target = pos;
            st.walking = false; st.timer = frand(1.0f, 3.0f);
            // Stagger needs so the tribe doesn't all get parched/starved in lockstep.
            st.thirst = frand(0.0f, 0.4f);
            st.thirstRate = frand(0.010f, 0.030f);
            st.hunger = frand(0.0f, 0.4f);
            // Slow enough that hunger is a DAILY thing — the old rate emptied
            // them three times a day, which made the reckoning meaningless.
            st.hungerRate = frand(0.0022f, 0.0048f);
            st.laziness = frand(0.0f, 1.0f);   // fixed for life
            st.skillForage = frand(0.0f, 1.0f); // ditto — some are hopeless
            st.skillSocial = frand(0.0f, 1.0f);
            st.displayName = nameFor(obj->getName());
            st.fatigue = frand(0.0f, 0.25f);
            st.init = true;
        }
        const glm::vec3 startPos = pos;   // for the distance actually covered

        auto play = [&](const char* name) {
            if (st.clip != name) {
                m_host.skinned->playAnimation(obj->getSkinnedModelHandle(), name, true);
                obj->setCurrentAnimation(name);
                st.clip = name;
            }
        };
        auto faceToward = [&](const glm::vec3& tp) {
            glm::vec3 f = tp - pos; f.y = 0.0f;
            if (glm::length(f) > 1e-3f)
                obj->setEulerRotation({0.0f, glm::degrees(std::atan2(f.x, f.z)), 0.0f});
        };

        // Nearest OTHER inhabitant for spacing (any tribe), and nearest KIN
        // (same tribe) for greeting — you don't fraternise with a rival tribe.
        SceneObject* near = nullptr; float nd = 1e9f; glm::vec3 nearPos(0.0f);
        SceneObject* kin  = nullptr; float kd = 1e9f; glm::vec3 kinPos(0.0f);
        for (auto& other : mob) {
            if (other.first == obj) continue;
            float d = dist2D(pos, other.second);
            if (d < nd) { nd = d; near = other.first; nearPos = other.second; }
            int ot = 0;
            auto oit = m_inhabitants.find(other.first->getName());
            if (oit != m_inhabitants.end()) ot = oit->second.tribe;
            if (ot == st.tribe && d < kd) { kd = d; kin = other.first; kinPos = other.second; }
        }

        st.timer   -= deltaTime;
        st.greetCd -= deltaTime;

        // NEEDS climb; hysteresis so they commit to a full drink/meal once they
        // set out (seek from URGENT, don't quit until SATED).
        constexpr float kUrgent = 0.65f, kSated = 0.15f;
        st.thirst = std::min(1.0f, st.thirst + st.thirstRate * deltaTime);
        st.hunger = std::min(1.0f, st.hunger + st.hungerRate * deltaTime);
        if (st.thirst > kUrgent) st.seekingWater = true;
        if (st.thirst < kSated)  st.seekingWater = false;
        if (st.hunger > kUrgent) st.seekingFood = true;
        if (st.hunger < kSated)  st.seekingFood = false;

        // REST, on the same hysteresis idea. The lazy sit down at a third of the
        // fatigue a grafter will carry, and don't get up until they're properly
        // recovered — so they rest sooner AND longer, which is the whole of the
        // difference between them.
        float tiredAt  = 0.80f - st.laziness * 0.45f;   // lazy 0.35 … grafter 0.80
        float restedAt = 0.30f - st.laziness * 0.28f;   // lazy 0.02 … grafter 0.30
        if (st.fatigue > tiredAt)  st.resting = true;
        if (st.fatigue < restedAt) st.resting = false;

        // Nearest source of a given resource tag.
        // An exhausted source is skipped — a picked-bare bush isn't worth the walk.
        auto sourceLive = [&](const std::string& n) -> bool {
            auto it = m_resources.find(n);
            return it == m_resources.end() || !it->second.exhausted;
        };
        auto nearestOf = [&](const char* tag, glm::vec3& out, std::string& outName) -> bool {
            bool found = false; float best = 1e9f;
            for (auto& objPtr : sceneObjects()) {
                if (!objPtr || objPtr->getBuildingType() != tag) continue;
                if (!sourceLive(objPtr->getName())) continue;
                glm::vec3 rp = objPtr->getTransform().getPosition();
                float d = dist2D(pos, rp);
                if (d < best) { best = d; out = rp; outName = objPtr->getName(); found = true; }
            }
            return found;
        };
        glm::vec3 waterPos(0.0f), foodPos(0.0f), predPos(0.0f);
        std::string waterName, foodName, predName;
        bool haveWater = nearestOf("water", waterPos, waterName);
        bool haveFood  = nearestOf("food",  foodPos,  foodName);
        // FEAR overrides every need: a predator within range means run.
        bool havePred  = nearestOf("predator", predPos, predName);
        // Bolt at 8m, and don't stop running until you're 12m clear. A single
        // radius means the step that carries you out of it drops you straight
        // back into wanting a drink — approach, flee, approach, flee.
        constexpr float kFearR = 8.0f, kSafeR = 12.0f;
        if (havePred) {
            float pd = dist2D(pos, predPos);
            if (pd < kFearR)      st.panicked = true;
            else if (pd > kSafeR) st.panicked = false;
        } else {
            st.panicked = false;
        }
        bool fleeing = st.panicked && havePred;

        // This tribe's camp and store — the buffer they fall back on when the
        // pond has run dry.
        TribeStore* store = (st.tribe >= 0 && st.tribe < static_cast<int>(m_tribeStores.size()))
                            ? &m_tribeStores[st.tribe] : nullptr;
        glm::vec3 campPos(0.0f);
        bool haveCamp = tribeCamp(st.tribe, campPos);
        // Live handle on a source's remaining units (updateResources has already
        // seeded every water/food object this frame, so find() is enough).
        auto poolOf = [&](const std::string& n) -> float* {
            auto it = m_resources.find(n);
            return (it == m_resources.end()) ? nullptr : &it->second.amount;
        };

        // Head to a resource and use it, drawing what's consumed out of the pool
        // it came from. Thirst wins ties (it kills faster).
        auto seekAndUse = [&](const glm::vec3& rp, float& need, bool& using_, float drain,
                              float* poolPtr, float poolRate) {
            using_ = false;
            st.greeting = false;   // a needy creature won't stop to chat
            st.hauling = false;    // nor will it keep working
            st.gathering = false;
            if (dist2D(pos, rp) < 1.6f) {
                using_ = true;
                need = std::max(0.0f, need - drain * deltaTime);
                if (poolPtr) *poolPtr = std::max(0.0f, *poolPtr - poolRate * deltaTime);
                faceToward(rp);
                play("idle");
            } else {
                glm::vec3 to = rp - pos; to.y = 0.0f;
                glm::vec3 dir = to / std::max(glm::length(to), 1e-4f);
                pos += dir * 1.6f * deltaTime;
                faceToward(rp);
                play("walk");
            }
        };

        if (fleeing) {
            // Run directly away from the predator, faster than a stroll (panic).
            st.drinking = false; st.eating = false; st.greeting = false;
            st.walking = false; st.resting = false;   // panic beats tiredness
            glm::vec3 away = pos - predPos; away.y = 0.0f;
            glm::vec3 dir = away / std::max(glm::length(away), 1e-4f);
            pos += dir * 2.4f * deltaTime;
            faceToward(pos + dir);
            play("walk");
        } else if (st.seekingWater && haveWater) {
            st.eating = false;
            seekAndUse(waterPos, st.thirst, st.drinking, 0.55f, poolOf(waterName), 1.5f);
        } else if (st.seekingWater && haveCamp && store && store->water > 0.1f) {
            // Pond's gone — fall back on what the tribe hauled home.
            st.eating = false;
            seekAndUse(campPos, st.thirst, st.drinking, 0.55f, &store->water, 1.5f);
        } else if (st.seekingFood && st.foodStock > 0.05f) {
            // You eat what you found. Standing in a berry bush is work, not a
            // meal — the bush fills your larder, the larder fills you.
            st.drinking = false; st.eating = true;
            st.greeting = false; st.hauling = false;
            st.foraging = false; st.gathering = false;
            st.hunger = std::max(0.0f, st.hunger - 0.45f * deltaTime);
            st.foodStock = std::max(0.0f, st.foodStock - 1.5f * deltaTime);
            play("idle");
        } else {
            st.drinking = false; st.eating = false;

            if (st.resting) {
                // Sat down. The trip — and whatever's in her arms — waits.
                st.walking = false; st.greeting = false; st.gathering = false;
                play("idle");
            } else {

            // ECONOMY: whoever isn't personally needy goes to work — fill up at a
            // source, carry it home, drop it in the tribe's store. No one is told
            // to; it just falls out of "I'm fine, and the crate is low."
            constexpr float kStoreTarget = 40.0f;   // enough put by, stop hauling
            constexpr float kCarryCap    = 10.0f;   // an armful
            constexpr float kLarderTarget = 4.0f;   // a day or two — a bad day bites

            // FORAGING — the day job, and the only way food happens. Aptitude
            // swings the rate tenfold, and the ground itself runs out, so the
            // gifted strip a bush before the hopeless ever get there.
            if (st.foraging && (!haveFood || st.foodStock >= kLarderTarget)) {
                st.foraging = false; st.gathering = false; st.targetSrc.clear();
            }
            if (!st.foraging && !st.hauling && st.carry <= 0.0f &&
                st.foodStock < kLarderTarget && haveFood) {
                st.foraging = true; st.walking = false; st.greeting = false;
            }
            if (st.foraging) {
                // Same commitment rule as hauling — stick with the ground you
                // set out for so two foragers don't swap each other's away.
                glm::vec3 goPos = foodPos; std::string goNm = foodName;
                glm::vec3 held(0.0f);
                if (!st.targetSrc.empty() && sourceLive(st.targetSrc) &&
                    objectPos(st.targetSrc, held)) {
                    goPos = held; goNm = st.targetSrc;
                } else {
                    st.targetSrc = foodName;
                }
                st.forageTimeToday += deltaTime;
                if (dist2D(pos, goPos) < 1.6f) {
                    st.gathering = true;
                    float* p = poolOf(goNm);
                    float rate = 3.0f * (0.15f + st.skillForage * 1.35f);
                    float take = rate * deltaTime;
                    if (p) take = std::min(take, *p);
                    take = std::min(take, kLarderTarget - st.foodStock);
                    st.foodStock += take;
                    st.foragedToday += take;
                    if (p) *p = std::max(0.0f, *p - take);
                    faceToward(goPos);
                    play("idle");
                } else {
                    st.gathering = false;
                    glm::vec3 to = goPos - pos; to.y = 0.0f;
                    pos += (to / std::max(glm::length(to), 1e-4f)) * 1.6f * deltaTime;
                    faceToward(goPos);
                    play("walk");
                }
            } else {
            // Camp gone (or no tribe) — drop the job rather than walk to nowhere.
            if (st.hauling && (!store || !haveCamp)) {
                st.hauling = false; st.gathering = false; st.targetSrc.clear();
            }
            // Interrupted mid-trip by thirst/hunger/the lion and still holding an
            // armful — resume, and take it home before fetching anything else.
            if (store && haveCamp && !st.hauling && st.carry > 0.0f && st.carryType >= 0) {
                st.hauling = true; st.walking = false; st.greeting = false;
            }
            // WATER is still communal — fetched and carried back to the crate,
            // and drunk from it by anyone. Only food is personal.
            if (store && haveCamp && !st.hauling && st.carry <= 0.0f &&
                store->water < kStoreTarget && haveWater) {
                st.carryType = 0;
                st.hauling = true; st.walking = false; st.greeting = false;
            }

            if (st.hauling) {
                bool haveSrc = haveWater;
                const glm::vec3& srcPos  = waterPos;
                const std::string& srcNm = waterName;

                if (st.carry < kCarryCap && haveSrc) {
                    // STICK WITH THE ONE YOU SET OUT FOR. Re-picking the nearest
                    // every frame lets two workers swap each other's destination
                    // out from under them, and they turn on the spot instead of
                    // going anywhere.
                    glm::vec3 goPos = srcPos; std::string goNm = srcNm;
                    glm::vec3 held(0.0f);
                    if (!st.targetSrc.empty() && sourceLive(st.targetSrc) &&
                        objectPos(st.targetSrc, held)) {
                        goPos = held; goNm = st.targetSrc;
                    } else {
                        st.targetSrc = srcNm;   // first trip, or ours ran dry
                    }

                    if (dist2D(pos, goPos) < 1.6f) {
                        // FILL UP — every unit in hand comes out of the source.
                        st.gathering = true;
                        float* p = poolOf(goNm);
                        float take = 8.0f * deltaTime;
                        if (p) take = std::min(take, *p);
                        take = std::min(take, kCarryCap - st.carry);
                        st.carry += take;
                        if (p) *p = std::max(0.0f, *p - take);
                        faceToward(goPos);
                        play("idle");
                    } else {
                        st.gathering = false;
                        glm::vec3 to = goPos - pos; to.y = 0.0f;
                        pos += (to / std::max(glm::length(to), 1e-4f)) * 1.6f * deltaTime;
                        faceToward(goPos);
                        play("walk");
                    }
                } else if (st.carry > 0.0f) {
                    // HANDS FULL (or the source died mid-trip) — take it home.
                    st.gathering = false;
                    if (dist2D(pos, campPos) < 1.8f) {
                        store->water += st.carry;
                        st.carry = 0.0f; st.hauling = false; st.carryType = -1;
                        st.targetSrc.clear();
                        st.timer = frand(0.4f, 1.2f);
                        play("idle");
                    } else {
                        glm::vec3 to = campPos - pos; to.y = 0.0f;
                        pos += (to / std::max(glm::length(to), 1e-4f)) * 1.6f * deltaTime;
                        faceToward(campPos);
                        play("walk");
                    }
                } else {
                    // Empty handed and nothing left to fetch — give it up.
                    st.hauling = false; st.gathering = false; st.carryType = -1;
                    st.targetSrc.clear();
                }
            } else {
            // NOTICE: a KIN wanders close → stop, turn to them, share a beat.
            // Whether you actually go over is the social skill talking. The shy
            // hang back rather than approach and fumble it, so a low roll shows up
            // as quiet rather than as a pile of enemies.
            if (!st.greeting && st.greetCd <= 0.0f && kin && kd < kNoticeR) {
                if (frand(0.0f, 1.0f) < 0.25f + st.skillSocial * 0.75f) {
                    st.greeting = true; st.walking = false;
                    st.timer = frand(1.2f, 2.4f);
                    faceToward(kinPos);
                    play("idle");

                    // THE CONTEST. Both sides roll skill plus real noise, so upsets
                    // are common and two awkward ones can still find each other.
                    // The MARGIN sets the size of it, and a fumble costs less than
                    // a hit gains — so persistence still slowly pays.
                    auto kit = m_inhabitants.find(kin->getName());
                    if (kit != m_inhabitants.end()) {
                        float mine   = st.skillSocial          + frand(-0.5f, 0.5f);
                        float theirs = kit->second.skillSocial + frand(-0.5f, 0.5f);
                        float margin = mine - theirs;
                        float delta  = margin * (margin > 0.0f ? 0.12f : 0.05f);
                        float& b = m_bonds[bondKey(obj->getName(), kin->getName())];
                        b = std::min(1.0f, std::max(-1.0f, b + delta));
                        st.greetOutcome = delta;
                        kit->second.greetCd = frand(1.0f, 2.0f);  // just been spoken to
                    }
                } else {
                    st.greetCd = frand(2.0f, 4.0f);   // hung back; maybe next time
                }
            }

            if (st.greeting) {
                if (st.timer <= 0.0f) {                  // done greeting
                    st.greeting = false;
                    st.greetOutcome = 0.0f;
                    // The warm seek company again sooner than the awkward do.
                    st.greetCd = frand(4.0f, 8.0f) * (1.6f - st.skillSocial);
                    st.timer   = frand(0.4f, 1.2f);
                }
                // hold position while greeting
            } else if (st.walking) {
                glm::vec3 to = st.target - pos; to.y = 0.0f;
                float dist = glm::length(to);
                if (dist < 0.6f || st.timer <= 0.0f) {   // arrived, or gave up
                    st.walking = false;
                    st.timer = frand(2.0f, 5.0f);
                    play("idle");
                } else {
                    glm::vec3 dir = to / dist;
                    glm::vec3 stepPos = pos + dir * 1.6f * deltaTime;   // gentle stroll (m/s)
                    // SPACING: take the step unless it would crowd the nearest neighbour
                    // (still allow it if we're already too close and moving away).
                    bool crowding = near && dist2D(stepPos, nearPos) < kPersonal
                                         && dist2D(stepPos, nearPos) < nd;
                    if (!crowding) pos = stepPos;
                    faceToward(st.target);
                    play("walk");
                }
            } else if (st.timer <= 0.0f) {
                // CLUSTER: wander around THIS tribe's centre so the group stays
                // loosely together (and apart from the rival tribe).
                glm::vec3 center = (st.tribe >= 0 && st.tribe < (int)m_tribeCenters.size())
                                   ? m_tribeCenters[st.tribe] : pos;
                float ang = frand(0.0f, 6.2831853f);
                float r   = frand(1.5f, 7.0f);
                st.target = center + glm::vec3(std::cos(ang) * r, 0.0f, std::sin(ang) * r);
                st.walking = true; st.timer = 12.0f;
                play("walk");
            }
            }   // end of the not-hauling (social / wander) branch
            }   // end of the not-foraging branch
            }   // end of the not-resting branch
        }

        // FATIGUE comes out of the distance actually covered, so a long haul to
        // a far pond costs more than a short one, a full load costs more than
        // empty hands, and panic — which is faster — costs most of all. The lazy
        // tire quicker at all of it, and dawdle longer over the break.
        float travelled = dist2D(pos, startPos);
        float load = (st.carry > 0.0f) ? 1.5f : 1.0f;
        if (fleeing) load *= 1.6f;
        if (st.resting) {
            st.fatigue -= (0.160f - st.laziness * 0.115f) * deltaTime;
        } else {
            st.fatigue += travelled * 0.020f * (0.8f + st.laziness * 0.6f) * load;
            st.fatigue -= 0.004f * deltaTime;   // standing about barely helps
        }
        st.fatigue = std::min(1.0f, std::max(0.0f, st.fatigue));

        // Nothing left anywhere — no live source AND an empty crate. This is the
        // tribe failing, and it should read that way at a glance.
        bool dryWater = st.seekingWater && !haveWater && !(store && store->water > 0.1f);
        bool dryFood  = st.seekingFood  && !haveFood  && st.foodStock <= 0.05f;

        // Dominant state, for the tag above the head.
        if (fleeing)              st.stateTag = "#fleeing";
        else if (st.drinking)     st.stateTag = "#drinking";
        else if (st.eating)       st.stateTag = "#eating";
        else if (dryWater)        st.stateTag = "#parched";
        else if (dryFood)         st.stateTag = "#starving";
        else if (st.resting)      st.stateTag = (st.laziness > 0.66f) ? "#lazing" : "#resting";
        else if (st.seekingWater) st.stateTag = "#thirsty";
        else if (st.seekingFood)  st.stateTag = "#hungry";
        else if (st.gathering)    st.stateTag = st.foraging ? "#foraging" : "#gathering";
        else if (st.foraging)     st.stateTag = "#searching";
        else if (st.hauling)      st.stateTag = (st.carry > 0.0f) ? "#hauling" : "#fetching";
        else if (st.greeting)     st.stateTag = st.greetOutcome > 0.0f ? "#hello+"
                                              : st.greetOutcome < 0.0f ? "#hello-" : "#hello";
        else if (st.walking)      st.stateTag = "#roaming";
        else                      st.stateTag = "#content";

        // Stick to the ground wherever she ends up this frame.
        pos.y = m_host.terrain->getHeightAt(pos.x, pos.z) + st.footLift;
        obj->getTransform().setPosition(pos);
    }

    // Bonds fade when they aren't kept up — a friendship is cultivated, not
    // banked once. Grudges fade at the same rate, which is the only mercy in it.
    for (auto it = m_bonds.begin(); it != m_bonds.end(); ) {
        float d = 0.004f * deltaTime;
        it->second = (it->second > 0.0f) ? std::max(0.0f, it->second - d)
                                         : std::min(0.0f, it->second + d);
        if (std::fabs(it->second) < 1e-3f) it = m_bonds.erase(it);
        else ++it;
    }
}

// Drop a water source (a flat blue pond) in front of the camera, on the terrain.
// Thirsty inhabitants path to the nearest one. Press U.
void TribeSim::spawnWater() {
    if (!m_host.models) return;
    glm::vec4 blue(0.20f, 0.45f, 0.85f, 1.0f);
    glm::vec3 p = m_host.camera->getPosition() + m_host.camera->getFront() * 6.0f;
    p.y = m_host.terrain->getHeightAt(p.x, p.z) + 0.05f;
    auto mesh = PrimitiveMeshBuilder::createCube(1.0f, blue);
    uint32_t handle = m_host.models->createModel(mesh.vertices, mesh.indices, nullptr, 0, 0);
    auto obj = std::make_unique<SceneObject>("Water_" + std::to_string(++m_waterCounter));
    obj->setBufferHandle(handle);
    obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
    obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
    obj->setLocalBounds(mesh.bounds);
    obj->setMeshData(mesh.vertices, mesh.indices);
    obj->setPrimitiveType(PrimitiveType::Cube);
    obj->setPrimitiveSize(1.0f);
    obj->setPrimitiveColor(blue);
    obj->setBuildingType("water");
    obj->getTransform().setPosition(p);
    obj->getTransform().setScale({4.0f, 0.15f, 4.0f}); // a flat pool
    sceneObjects().push_back(std::move(obj));
    std::cout << "[Water] placed a water source" << std::endl;
}

// Drop a food source (a green berry bush) in front of the camera. Hungry
// inhabitants path to the nearest one. Press O.
void TribeSim::spawnFood() {
    if (!m_host.models) return;
    glm::vec4 green(0.25f, 0.65f, 0.28f, 1.0f);
    glm::vec3 p = m_host.camera->getPosition() + m_host.camera->getFront() * 6.0f;
    auto mesh = PrimitiveMeshBuilder::createCube(1.0f, green);
    uint32_t handle = m_host.models->createModel(mesh.vertices, mesh.indices, nullptr, 0, 0);
    auto obj = std::make_unique<SceneObject>("Food_" + std::to_string(++m_foodCounter));
    obj->setBufferHandle(handle);
    obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
    obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
    obj->setLocalBounds(mesh.bounds);
    obj->setMeshData(mesh.vertices, mesh.indices);
    obj->setPrimitiveType(PrimitiveType::Cube);
    obj->setPrimitiveSize(1.0f);
    obj->setPrimitiveColor(green);
    obj->setBuildingType("food");
    glm::vec3 s(1.4f, 1.2f, 1.4f);         // a low bush
    p.y = m_host.terrain->getHeightAt(p.x, p.z) + s.y * 0.5f;
    obj->getTransform().setPosition(p);
    obj->getTransform().setScale(s);
    sceneObjects().push_back(std::move(obj));
    std::cout << "[Food] placed a food source" << std::endl;
}

// Release a lion (placeholder tan box, low + long) that hunts inhabitants.
// Press Y in play mode. Swap in a real Meshy lion later — behaviour is the point.
void TribeSim::spawnLion() {
    if (!m_host.models) return;
    glm::vec4 tan(0.78f, 0.58f, 0.30f, 1.0f);
    glm::vec3 p = m_host.camera->getPosition() + m_host.camera->getFront() * 8.0f;
    auto mesh = PrimitiveMeshBuilder::createCube(1.0f, tan);
    uint32_t handle = m_host.models->createModel(mesh.vertices, mesh.indices, nullptr, 0, 0);
    auto obj = std::make_unique<SceneObject>("Lion_" + std::to_string(++m_lionCounter));
    obj->setBufferHandle(handle);
    obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
    obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
    obj->setLocalBounds(mesh.bounds);
    obj->setMeshData(mesh.vertices, mesh.indices);
    obj->setPrimitiveType(PrimitiveType::Cube);
    obj->setPrimitiveSize(1.0f);
    obj->setPrimitiveColor(tan);
    obj->setBuildingType("predator");
    glm::vec3 s(1.2f, 1.1f, 2.4f);         // low and long
    p.y = m_host.terrain->getHeightAt(p.x, p.z) + s.y * 0.5f;
    obj->getTransform().setPosition(p);
    obj->getTransform().setScale(s);
    sceneObjects().push_back(std::move(obj));
    std::cout << "[Lion] released a predator" << std::endl;
}

// The lion's brain: rest after a kill, otherwise stalk the nearest inhabitant
// (faster than they walk) and take it on contact; wander if none is near.
void TribeSim::updatePredators(float deltaTime) {
    auto frand = [](float a, float b) {
        return a + (b - a) * (static_cast<float>(rand()) / static_cast<float>(RAND_MAX));
    };
    auto dist2D = [](const glm::vec3& a, const glm::vec3& b) {
        float dx = a.x - b.x, dz = a.z - b.z; return std::sqrt(dx * dx + dz * dz);
    };
    constexpr float kSenseR = 22.0f;   // notices prey within this
    constexpr float kKillR  = 1.4f;    // takes prey this close

    for (auto& objPtr : sceneObjects()) {
        if (!objPtr || objPtr->getBuildingType() != "predator") continue;
        SceneObject* lion = objPtr.get();
        PredatorState& st = m_predators[lion->getName()];
        glm::vec3 pos = lion->getTransform().getPosition();
        if (!st.init) {
            st.footLift = pos.y - m_host.terrain->getHeightAt(pos.x, pos.z);
            st.home = pos; st.target = pos; st.timer = frand(1.0f, 3.0f);
            st.init = true;
        }
        auto face = [&](const glm::vec3& tp) {
            glm::vec3 f = tp - pos; f.y = 0.0f;
            if (glm::length(f) > 1e-3f)
                lion->setEulerRotation({0.0f, glm::degrees(std::atan2(f.x, f.z)), 0.0f});
        };

        st.timer -= deltaTime;
        st.fedCd -= deltaTime;

        // Nearest living inhabitant.
        SceneObject* prey = nullptr; float pd = 1e9f; glm::vec3 preyPos(0.0f);
        for (auto& o2 : sceneObjects()) {
            if (!o2 || o2->getBuildingType() != "inhabitant") continue;
            float d = dist2D(pos, o2->getTransform().getPosition());
            if (d < pd) { pd = d; prey = o2.get(); preyPos = o2->getTransform().getPosition(); }
        }

        if (st.fedCd <= 0.0f && prey && pd < kSenseR) {
            // HUNT: run it down.
            if (pd < kKillR) {
                m_pendingKills.push_back(prey->getName());   // caught — removed after the loop
                st.fedCd = frand(20.0f, 40.0f);              // digest; the tribe gets a reprieve
                st.prowling = false; st.timer = frand(2.0f, 4.0f);
            } else {
                glm::vec3 to = preyPos - pos; to.y = 0.0f;
                glm::vec3 dir = to / std::max(glm::length(to), 1e-4f);
                pos += dir * 2.6f * deltaTime;              // faster than the 1.6 stroll
                face(preyPos);
            }
        } else {
            // PROWL: amble around home (slower when fed/resting).
            float speed = (st.fedCd > 0.0f) ? 0.6f : 1.3f;
            if (!st.prowling && st.timer <= 0.0f) {
                float ang = frand(0.0f, 6.2831853f), r = frand(4.0f, 14.0f);
                st.target = st.home + glm::vec3(std::cos(ang) * r, 0.0f, std::sin(ang) * r);
                st.prowling = true; st.timer = 15.0f;
            } else if (st.prowling) {
                glm::vec3 to = st.target - pos; to.y = 0.0f;
                float dist = glm::length(to);
                if (dist < 0.8f || st.timer <= 0.0f) { st.prowling = false; st.timer = frand(2.0f, 5.0f); }
                else { glm::vec3 dir = to / dist; pos += dir * speed * deltaTime; face(st.target); }
            }
        }

        pos.y = m_host.terrain->getHeightAt(pos.x, pos.z) + st.footLift;
        lion->getTransform().setPosition(pos);
    }
}


// They're all women, and "Inhabitant_7 found four coconut shells full of berries"
// reads like a spreadsheet. Derived from the object's own counter so it's stable
// across a save and never collides.
std::string TribeSim::nameFor(const std::string& objName) {
    static const char* kNames[] = {
        "Mira","Sela","Ondra","Tavi","Ruth","Anwe","Lissa","Bekka","Nour","Ivetta",
        "Kesh","Marta","Perri","Oda","Wren","Ysolde","Fen","Hala","Cala","Reva",
        "Tullia","Enid","Sabra","Noka","Lira","Vesna","Agda","Bri","Rhosyn","Talia",
        "Wynn","Ilka","Suri","Denna","Oona","Zora","Maeve","Cora","Nera","Silke"
    };
    constexpr int kCount = static_cast<int>(sizeof(kNames) / sizeof(kNames[0]));
    size_t us = objName.find_last_of('_');
    int idx = 0;
    if (us != std::string::npos) { try { idx = std::stoi(objName.substr(us + 1)); } catch (...) {} }
    std::string n = kNames[((idx % kCount) + kCount) % kCount];
    int wrap = idx / kCount;                     // second time round the list
    if (wrap > 0) n += " " + std::string(wrap + 1, 'I');
    return n;
}

// Dusk falls: total up what everyone actually found today and put it in the feed.
// A report for now — this is where sharing along the friendship edges will hook in.
void TribeSim::updateReckoning(float /*deltaTime*/) {
    if (m_inhabitants.empty()) { m_prevSimMinutes = -1.0f; return; }
    float now = (*m_host.gameTimeMinutes), prev = m_prevSimMinutes;
    m_prevSimMinutes = now;
    if (prev < 0.0f) return;                     // first frame: no crossing to detect
    bool crossed = (now < prev) ? (prev < kDuskMinutes)          // wrapped past midnight
                                : (prev < kDuskMinutes && now >= kDuskMinutes);
    if (crossed) runReckoning();
}

void TribeSim::runReckoning() {
    static const char* kFoods[] = {"berries","roots","seeds","greens","mushrooms","tubers"};
    static const char* kNums[]  = {"no","one","two","three","four","five","six",
                                   "seven","eight","nine","ten","eleven","twelve"};
    auto irand = [](int n) { return n > 0 ? (rand() % n) : 0; };

    ++m_dayNumber;
    m_newsFeed.push_back({true, "Day " + std::to_string(m_dayNumber)});

    // Read as a league table — best day first, empty hands last.
    std::vector<std::pair<std::string, InhabitantState*>> roll;
    for (auto& kv : m_inhabitants) roll.push_back({kv.first, &kv.second});
    std::sort(roll.begin(), roll.end(), [](const auto& a, const auto& b) {
        return a.second->foragedToday > b.second->foragedToday;
    });

    constexpr float kDayRealSeconds = 1440.0f / kSimDayScale;
    for (auto& r : roll) {
        InhabitantState& s = *r.second;
        const std::string who = s.displayName.empty() ? r.first : s.displayName;

        float effort = s.forageTimeToday / kDayRealSeconds;
        const char* eff = effort > 0.55f ? "was out all day"
                        : effort > 0.25f ? "spent a good part of the day out"
                        : effort > 0.04f ? "went out for a while"
                        : "barely left camp";

        const char* food = kFoods[irand(6)];
        std::string yield;
        if (s.foragedToday < 0.05f) {
            yield = "and found nothing at all";
        } else if (s.foragedToday < 1.2f) {
            yield = std::string("and came back with a bare handful of ") + food;
        } else if (s.foragedToday < 9.0f) {
            int n = std::max(1, static_cast<int>(std::lround(s.foragedToday / 1.2f)));
            yield = std::string("and found ") + kNums[std::min(n, 12)] +
                    (n == 1 ? " coconut shell full of " : " coconut shells full of ") + food;
        } else {
            int n = std::max(2, static_cast<int>(std::lround(s.foragedToday / 6.0f)));
            yield = std::string("filled ") + kNums[std::min(n, 12)] + " baskets with " + food;
        }

        std::string line = who + " " + eff + " " + yield + ".";
        if (s.foodStock <= 0.05f) line += " Her larder is empty.";
        m_newsFeed.push_back({false, line});

        s.foragedToday = 0.0f;
        s.forageTimeToday = 0.0f;
    }

    runSharing();   // the empty-handed go asking before anyone eats

    int wentHungry = 0;
    for (const auto& kv : m_inhabitants)
        if (kv.second.foodStock <= 0.05f) ++wentHungry;
    if (wentHungry > 0) {
        m_newsFeed.push_back({false, std::string("  ") + std::to_string(wentHungry) +
                              (wentHungry == 1 ? " of them has nothing to eat tonight."
                                               : " of them have nothing to eat tonight.")});
    }
    while (m_newsFeed.size() > 400) m_newsFeed.erase(m_newsFeed.begin());
}


// Nightfall: whoever came back empty goes round asking. Food never pools — it
// moves along the friendship edges one gift at a time, and how much you get
// depends on how close you are to whoever still has any. This is where a good
// social roll finally pays a hopeless forager, and where being nobody's friend
// stops being a social problem and becomes a food problem.
void TribeSim::runSharing() {
    constexpr float kMeal    = 1.7f;   // roughly one sitting
    constexpr float kReserve = 1.7f;   // a donor always keeps one back
    constexpr float kAskMin  = 0.05f;  // below this you're a stranger, and it's no
    auto frand = []() { return static_cast<float>(rand()) / static_cast<float>(RAND_MAX); };

    std::vector<std::pair<std::string, InhabitantState*>> all, needy;
    for (auto& kv : m_inhabitants) {
        all.push_back({kv.first, &kv.second});
        if (kv.second.foodStock < kMeal) needy.push_back({kv.first, &kv.second});
    }
    // The emptiest ask first — they're the ones who can't wait.
    std::sort(needy.begin(), needy.end(), [](const auto& a, const auto& b) {
        return a.second->foodStock < b.second->foodStock;
    });

    auto nameOf = [&](const std::string& key, const InhabitantState& st) {
        return st.displayName.empty() ? key : st.displayName;
    };

    for (auto& n : needy) {
        InhabitantState& asker = *n.second;
        if (asker.foodStock >= kMeal) continue;
        const std::string aName = nameOf(n.first, asker);

        // Your own tribe, closest first — you ask your best friend before you
        // ask anyone else.
        std::vector<std::pair<float, std::pair<std::string, InhabitantState*>>> cands;
        for (auto& c : all) {
            if (c.first == n.first || c.second->tribe != asker.tribe) continue;
            cands.push_back({bondValue(n.first, c.first), c});
        }
        std::sort(cands.begin(), cands.end(),
                  [](const auto& x, const auto& y) { return x.first > y.first; });

        bool fed = false;
        for (auto& c : cands) {
            float bond = c.first;
            InhabitantState& donor = *c.second.second;
            const std::string dName = nameOf(c.second.first, donor);
            float& b = m_bonds[bondKey(n.first, c.second.first)];

            // Sorted by closeness, so the first stranger means there's nobody
            // warmer left to try. One humiliation a night.
            if (bond < kAskMin) {
                m_newsFeed.push_back({false, "  " + aName + " went round asking, and"
                                             " nobody would give her anything."});
                b = std::max(-1.0f, b - 0.03f);
                break;
            }
            float surplus = donor.foodStock - kReserve;
            if (surplus <= 0.05f) continue;          // would help, has nothing

            // A mere acquaintance might still say no. A real friend doesn't.
            if (bond < 0.25f && frand() > bond * 3.0f) {
                m_newsFeed.push_back({false, "  " + aName + " asked " + dName +
                                             ", who said no."});
                b = std::max(-1.0f, b - 0.05f);      // being refused stings
                continue;
            }

            // The closer you are, the bigger the share.
            float want = kMeal - asker.foodStock;
            float give = std::min(surplus, want * (0.30f + bond * 0.70f));
            if (give <= 0.05f) continue;
            donor.foodStock -= give;
            asker.foodStock += give;
            b = std::min(1.0f, b + 0.05f);           // and it draws them closer
            m_newsFeed.push_back({false, "  " + dName + " gave " + aName + " " +
                                         (give < 0.8f ? "a little food"
                                                      : "a share of what she had") + "."});
            fed = true;
            if (asker.foodStock >= kMeal) break;
        }
        if (!fed && asker.foodStock <= 0.05f)
            m_newsFeed.push_back({false, "  " + aName + " goes to bed hungry."});
    }
}

// The feed itself, top-right. Collapsible, because you won't always want it.
void TribeSim::renderNewsFeed() {
    if (!m_enabled) return;
    if ((*m_host.isEdenOSLevel) || m_inhabitants.empty()) return;
    float w = static_cast<float>(m_host.window->getWidth());
    float h = static_cast<float>(m_host.window->getHeight());
    float panelW = std::max(380.0f, ImGui::GetFontSize() * 26.0f);
    float panelH = std::max(200.0f, h * 0.38f);

    ImGui::SetNextWindowPos(ImVec2(w - panelW - 16.0f, 16.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.72f);
    char title[96];
    std::snprintf(title, sizeof(title), "Day %d  -  %s###tribe_news", m_dayNumber,
                  m_host.formatGameTime((*m_host.gameTimeMinutes)).c_str());
    if (!ImGui::Begin(title, nullptr,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav)) {
        ImGui::End();
        return;
    }
    if (m_newsFeed.empty()) {
        ImGui::TextDisabled("Nothing has happened yet. The first reckoning is at dusk.");
    }
    ImGui::BeginChild("##news_scroll", ImVec2(0, 0), false);
    for (const auto& e : m_newsFeed) {
        if (e.first) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.55f, 1.0f), "%s", e.second.c_str());
            ImGui::Separator();
        } else {
            ImGui::TextWrapped("%s", e.second.c_str());
        }
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

// Click one to inspect them. Ray comes from the mouse when the cursor is free,
// and from the crosshair when it's captured, so it works either way you're
// watching. A miss clears the selection rather than leaving a stale one up.
void TribeSim::pickInhabitantAtCursor() {
    float w = static_cast<float>(m_host.window->getWidth());
    float h = static_cast<float>(m_host.window->getHeight());
    if (w <= 0.0f || h <= 0.0f) return;

    glm::vec2 mp(w * 0.5f, h * 0.5f);
    if (!Input::isMouseCaptured()) mp = Input::getMousePosition();
    float nx = (mp.x / w) * 2.0f - 1.0f;
    float ny = 1.0f - (mp.y / h) * 2.0f;

    glm::mat4 invVP = glm::inverse(m_host.camera->getProjectionMatrix(w / h, 0.1f, 5000.0f) *
                                   m_host.camera->getViewMatrix());
    glm::vec4 nearP = invVP * glm::vec4(nx, ny, -1.0f, 1.0f);
    glm::vec4 farP  = invVP * glm::vec4(nx, ny,  1.0f, 1.0f);
    nearP /= nearP.w; farP /= farP.w;
    glm::vec3 rayO = glm::vec3(nearP);
    glm::vec3 rayD = glm::normalize(glm::vec3(farP - nearP));

    std::string hit; float best = std::numeric_limits<float>::max();
    for (const auto& o : sceneObjects()) {
        if (!o || o->getBuildingType() != "inhabitant" || !o->isVisible()) continue;
        // Their bounds are slim; pad them so clicking a walking dot isn't fiddly.
        AABB b = const_cast<SceneObject*>(o.get())->getWorldBounds();
        b.min -= glm::vec3(0.25f); b.max += glm::vec3(0.25f);
        float d = b.intersect(rayO, rayD);
        if (d >= 0.0f && d < best) { best = d; hit = o->getName(); }
    }
    m_selectedInhabitant = hit;
}

// The inspector, sat to the right of the hotbar. Only what the sim actually
// tracks — which is thin today and gets fatter as they do.
void TribeSim::renderPanel() {
    if (!m_enabled) return;
    if ((*m_host.isEdenOSLevel) || m_inhabitants.empty()) return;
    float w = static_cast<float>(m_host.window->getWidth());
    float h = static_cast<float>(m_host.window->getHeight());

    constexpr float bottomMargin = 16.0f;
    float hotbarRight = m_host.hotbarRightEdge ? m_host.hotbarRightEdge() : w * 0.5f;
    // Wide and stout: fixed width scaled to the font, height left to ImGui so
    // nothing clips off the bottom at any font size, and anchored by its
    // BOTTOM-left corner so it grows upward off the hotbar line.
    float panelW = std::max(340.0f, ImGui::GetFontSize() * 21.0f);
    ImGui::SetNextWindowPos(ImVec2(hotbarRight + 8.0f, h - bottomMargin),
                            ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(panelW, 0.0f),
                                        ImVec2(panelW, std::numeric_limits<float>::max()));
    ImGui::SetNextWindowBgAlpha(0.72f);
    if (!ImGui::Begin("##inhabitant_info", nullptr,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    auto it = m_inhabitants.find(m_selectedInhabitant);
    if (m_selectedInhabitant.empty() || it == m_inhabitants.end()) {
        ImGui::TextDisabled("Nobody selected.");
        ImGui::Spacing();
        ImGui::TextWrapped("Click one of them to see who they are.");
        ImGui::End();
        return;
    }
    const InhabitantState& s = it->second;

    // Temperament is the one trait so far — read it out in words, since a bare
    // 0.73 says nothing about what she'll actually do.
    const char* temper = s.laziness < 0.20f ? "Grafter"
                      : s.laziness < 0.40f ? "Willing"
                      : s.laziness < 0.60f ? "Steady"
                      : s.laziness < 0.80f ? "Idler" : "Layabout";
    const char* doing = s.foraging  ? (s.gathering ? "Foraging" : "Looking for food")
                      : s.gathering ? "Gathering"
                      : s.hauling   ? (s.carry > 0.0f ? "Carrying it home" : "Off to fetch")
                      : s.resting   ? "Resting"
                      : s.drinking  ? "Drinking"
                      : s.eating    ? "Eating"
                      : s.greeting  ? "Talking"
                      : s.walking   ? "Wandering" : "Idle";

    static const ImVec4 kTribeTint[] = {
        ImVec4(0.47f, 0.78f, 1.00f, 1.0f), ImVec4(1.00f, 0.71f, 0.47f, 1.0f),
        ImVec4(0.67f, 1.00f, 0.59f, 1.0f), ImVec4(0.90f, 0.59f, 1.00f, 1.0f),
    };
    // Anything that can share a line does, so the box stays short and wide.
    float contentW = ImGui::GetContentRegionAvail().x;
    auto rightOf = [&](const char* left, const char* right, bool dimRight) {
        float rw = ImGui::CalcTextSize(right).x;
        ImGui::TextUnformatted(left);
        ImGui::SameLine(std::max(ImGui::CalcTextSize(left).x + 12.0f, contentW - rw));
        if (dimRight) ImGui::TextDisabled("%s", right);
        else          ImGui::TextUnformatted(right);
    };

    char tribeLbl[24];
    std::snprintf(tribeLbl, sizeof(tribeLbl), "tribe %c",
                  static_cast<char>('A' + (s.tribe % 26)));
    const std::string who = s.displayName.empty() ? m_selectedInhabitant : s.displayName;
    ImGui::TextColored(kTribeTint[s.tribe & 3], "%s", who.c_str());
    ImGui::SameLine(std::max(ImGui::CalcTextSize(who.c_str()).x + 12.0f,
                             contentW - ImGui::CalcTextSize(tribeLbl).x));
    ImGui::TextDisabled("%s", tribeLbl);
    ImGui::Separator();
    rightOf(doing, s.stateTag.c_str(), true);
    ImGui::Spacing();

    // Label and bar on the SAME row — three lines instead of six.
    float labelW = ImGui::CalcTextSize("Fatigue").x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    float barH   = ImGui::GetTextLineHeight();
    auto bar = [&](const char* label, float v, ImU32 col, const char* overlay = "") {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(labelW);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::ColorConvertU32ToFloat4(col));
        ImGui::ProgressBar(std::min(1.0f, std::max(0.0f, v)), ImVec2(-1.0f, barH), overlay);
        ImGui::PopStyleColor();
    };
    bar("Thirst",  s.thirst,  IM_COL32(80, 150, 235, 255));
    bar("Hunger",  s.hunger,  IM_COL32(95, 190, 100, 255));
    bar("Fatigue", s.fatigue, IM_COL32(225, 165, 70, 255));
    ImGui::Spacing();

    // Aptitude, named rather than numbered — a bare 0.18 doesn't tell you she
    // comes home empty-handed most days.
    const char* knack = s.skillForage < 0.20f ? "Hopeless"
                      : s.skillForage < 0.40f ? "Poor"
                      : s.skillForage < 0.60f ? "Fair"
                      : s.skillForage < 0.80f ? "Good" : "Gifted";
    const char* charm = s.skillSocial < 0.20f ? "Charmless"
                      : s.skillSocial < 0.40f ? "Awkward"
                      : s.skillSocial < 0.60f ? "Fair"
                      : s.skillSocial < 0.80f ? "Warm" : "Magnetic";
    bar("Forage", s.skillForage, IM_COL32(190, 130, 200, 255), knack);
    bar("Social", s.skillSocial, IM_COL32(215, 120, 150, 255), charm);
    ImGui::Spacing();

    char larder[48];
    std::snprintf(larder, sizeof(larder), "larder %.1f", s.foodStock);
    rightOf(temper, larder, true);
    if (s.carry > 0.0f) ImGui::TextDisabled("carrying %.0f water", s.carry);

    // Who they've got. This is the list that will decide who eats.
    std::vector<std::pair<std::string, float>> ties;
    for (const auto& kv : m_bonds) {
        size_t sep = kv.first.find('|');
        if (sep == std::string::npos) continue;
        std::string a = kv.first.substr(0, sep), b = kv.first.substr(sep + 1);
        const std::string* other = (a == m_selectedInhabitant) ? &b
                                 : (b == m_selectedInhabitant) ? &a : nullptr;
        if (!other) continue;
        auto oit = m_inhabitants.find(*other);
        ties.push_back({(oit != m_inhabitants.end() && !oit->second.displayName.empty())
                        ? oit->second.displayName : *other, kv.second});
    }
    std::sort(ties.begin(), ties.end(),
              [](const auto& x, const auto& y) { return x.second > y.second; });
    ImGui::Separator();
    if (ties.empty()) {
        ImGui::TextDisabled("Knows nobody yet.");
    } else {
        for (size_t i = 0; i < ties.size() && i < 4; ++i) {
            ImVec4 c = ties[i].second >= 0.0f ? ImVec4(0.70f, 0.90f, 0.70f, 1.0f)
                                              : ImVec4(0.95f, 0.60f, 0.55f, 1.0f);
            ImGui::TextColored(c, "%s", ties[i].first.c_str());
            const char* word = bondWord(ties[i].second);
            ImGui::SameLine(std::max(ImGui::CalcTextSize(ties[i].first.c_str()).x + 12.0f,
                                     contentW - ImGui::CalcTextSize(word).x));
            ImGui::TextDisabled("%s", word);
        }
    }
    ImGui::End();
}

// Float each inhabitant's dominant-state tag (#thirsty, #content, …) over its
// head — the glanceable readout of the state-sim. Terrain levels only.
void TribeSim::renderTags() {
    if (!m_enabled) return;
    if ((*m_host.isEdenOSLevel)) return;
    float w = static_cast<float>(m_host.window->getWidth());
    float h = static_cast<float>(m_host.window->getHeight());
    if (w <= 0.0f || h <= 0.0f) return;
    glm::mat4 vp = m_host.camera->getProjectionMatrix(w / h, 0.1f, 5000.0f) * m_host.camera->getViewMatrix();
    auto* dl = ImGui::GetForegroundDrawList();
    // One colour per tribe so the groups (and their camps) read at a glance.
    static const ImU32 kTribeCols[] = {
        IM_COL32(120, 200, 255, 255), // A  blue
        IM_COL32(255, 180, 120, 255), // B  orange
        IM_COL32(170, 255, 150, 255), // C  green
        IM_COL32(230, 150, 255, 255), // D  purple
    };
    auto floatLabel = [&](const glm::vec3& at, float topY, const std::string& label, ImU32 col) {
        glm::vec4 clip = vp * glm::vec4(at.x, topY + 0.3f, at.z, 1.0f);
        if (clip.w <= 0.0f) return;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z < -1.0f || ndc.z > 1.0f) return;
        float sx = (ndc.x + 1.0f) * 0.5f * w;
        float sy = (1.0f - ndc.y) * 0.5f * h;
        ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        float padX = 6.0f, padY = 2.0f;
        ImVec2 p0(sx - ts.x * 0.5f - padX, sy - ts.y * 0.5f - padY);
        ImVec2 p1(sx + ts.x * 0.5f + padX, sy + ts.y * 0.5f + padY);
        dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 160), 4.0f);
        dl->AddText(ImVec2(sx - ts.x * 0.5f, sy - ts.y * 0.5f), col, label.c_str());
    };

    // Each camp shows what its tribe has put by — the economy, glanceable.
    for (const auto& o : sceneObjects()) {
        if (!o || o->getBuildingType() != "stockpile") continue;
        int tribe = -1;
        for (size_t t = 0; t < m_tribeCamps.size(); ++t)
            if (m_tribeCamps[t] == o->getName()) { tribe = static_cast<int>(t); break; }
        if (tribe < 0 || tribe >= static_cast<int>(m_tribeStores.size())) continue;
        const TribeStore& s = m_tribeStores[tribe];
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%c camp  water %d",
                      static_cast<char>('A' + (tribe % 26)),
                      static_cast<int>(s.water));
        auto* raw = const_cast<SceneObject*>(o.get());
        floatLabel(raw->getTransform().getPosition(), raw->getWorldBounds().max.y,
                   buf, kTribeCols[tribe & 3]);
    }

    for (const auto& o : sceneObjects()) {
        if (!o || o->getBuildingType() != "inhabitant") continue;
        glm::vec3 pos = const_cast<SceneObject*>(o.get())->getTransform().getPosition();
        float topY = const_cast<SceneObject*>(o.get())->getWorldBounds().max.y;
        glm::vec4 clip = vp * glm::vec4(pos.x, topY + 0.3f, pos.z, 1.0f);
        if (clip.w <= 0.0f) continue;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z < -1.0f || ndc.z > 1.0f) continue;
        float sx = (ndc.x + 1.0f) * 0.5f * w;
        float sy = (1.0f - ndc.y) * 0.5f * h;
        auto it = m_inhabitants.find(o->getName());
        int tribe = 0; std::string tag = "#?";
        if (it != m_inhabitants.end()) { tribe = it->second.tribe; tag = it->second.stateTag; }
        ImU32 col = kTribeCols[tribe & 3];
        std::string label = std::string(1, static_cast<char>('A' + (tribe % 26))) + ":" + tag;
        ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        float padX = 6.0f, padY = 2.0f;
        ImVec2 p0(sx - ts.x * 0.5f - padX, sy - ts.y * 0.5f - padY);
        ImVec2 p1(sx + ts.x * 0.5f + padX, sy + ts.y * 0.5f + padY);
        dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 160), 4.0f);
        if (o->getName() == m_selectedInhabitant)   // the one in the inspector
            dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 230), 4.0f, 0, 2.0f);
        dl->AddText(ImVec2(sx - ts.x * 0.5f, sy - ts.y * 0.5f), col, label.c_str());
    }
}

