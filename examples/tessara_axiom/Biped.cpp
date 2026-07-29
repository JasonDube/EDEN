#include "Biped.hpp"

#include <algorithm>
#include <cmath>

namespace tessara {

namespace {

constexpr float kPi = 3.14159265f;

float wrapDegrees(float degrees) {
    while (degrees >  180.0f) degrees -= 360.0f;
    while (degrees < -180.0f) degrees += 360.0f;
    return degrees;
}

float smoothStep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

// Ported verbatim in behaviour from LIME's solveTwoBone.
void Biped::solveTwoBone(const glm::vec3& root, const glm::vec3& goal,
                         float upperLength, float lowerLength,
                         const glm::vec3& poleHint,
                         glm::vec3& outJoint, glm::vec3& outEnd)
{
    glm::vec3 toGoal = goal - root;
    float distance = glm::length(toGoal);
    glm::vec3 dir = (distance > 1e-6f) ? (toGoal / distance) : glm::vec3(0, -1, 0);

    // Clamp the end effector into the reachable shell. Past the far limit the
    // leg would have to break to reach; inside the near limit the two bones
    // would have to pass through each other.
    float dmin = std::fabs(upperLength - lowerLength) + 1e-4f;
    float dmax = upperLength + lowerLength - 1e-4f;
    float d = glm::clamp(distance, dmin, dmax);
    outEnd = root + dir * d;

    float a = (d * d + upperLength * upperLength - lowerLength * lowerLength) / (2.0f * d);
    float h = std::sqrt(std::max(0.0f, upperLength * upperLength - a * a));

    // Only the component of the pole perpendicular to the limb steers the bend.
    glm::vec3 bend = poleHint - root;
    bend = bend - dir * glm::dot(bend, dir);
    if (glm::length(bend) < 1e-5f) {
        glm::vec3 up(0, 1, 0);
        bend = up - dir * glm::dot(up, dir);
        if (glm::length(bend) < 1e-5f) bend = glm::vec3(1, 0, 0);
    }
    bend = glm::normalize(bend);

    outJoint = root + dir * a + bend * h;
}

float Biped::randomSigned() {
    m_rngState ^= m_rngState << 13;
    m_rngState ^= m_rngState >> 17;
    m_rngState ^= m_rngState << 5;
    return (static_cast<float>(m_rngState & 0xFFFFFF) / 8388608.0f) - 1.0f;   // -1..1
}

glm::vec3 Biped::forward() const {
    float yaw = glm::radians(m_yaw);
    return glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw));
}

glm::vec3 Biped::right() const {
    float yaw = glm::radians(m_yaw);
    return glm::vec3(std::cos(yaw), 0.0f, -std::sin(yaw));
}

glm::vec3 Biped::hipJoint(int i) const {
    float side = (i == 0) ? -1.0f : 1.0f;
    return m_hipCentre + right() * (side * params.hipWidth * 0.5f);
}

void Biped::solveLeg(int i, glm::vec3& outKnee, glm::vec3& outAnkle) const {
    const glm::vec3 hip = hipJoint(i);

    // Pole ahead of the hip, so the knee bends forwards. Everything else about
    // the leg falls out of the solver.
    const glm::vec3 pole = hip + forward() * (params.thigh + params.shin);

    solveTwoBone(hip, m_foot[i], params.thigh, params.shin, pole, outKnee, outAnkle);
}

// The torso frame: the body's, twisted about the spine, then pitched forward.
// Yaw first, so the lean is applied to an already-twisted chest rather than the
// two fighting over which axis is which.
glm::vec3 Biped::torsoRight() const {
    float y = glm::radians(m_torsoYaw);
    return forward() * -std::sin(y) + right() * std::cos(y);
}

glm::vec3 Biped::torsoUp() const {
    float y = glm::radians(m_torsoYaw);
    float p = glm::radians(m_torsoPitch);
    glm::vec3 flat = forward() * std::cos(y) + right() * std::sin(y);
    return glm::vec3(0, 1, 0) * std::cos(p) + flat * std::sin(p);
}

glm::vec3 Biped::torsoForward() const {
    float y = glm::radians(m_torsoYaw);
    float p = glm::radians(m_torsoPitch);
    glm::vec3 flat = forward() * std::cos(y) + right() * std::sin(y);
    return flat * std::cos(p) - glm::vec3(0, 1, 0) * std::sin(p);
}

void Biped::updateTorso(float speed, float dt) {
    float target = std::clamp(speed * params.leanPerSpeed, 0.0f, params.maxLean);

    // A task's bend overrides the speed lean outright rather than adding to it:
    // he is not leaning into a walk he has stopped doing.
    if (m_bendTarget > 0.0f) target = m_bendTarget;

    // Eased rather than snapped, so the lean builds as he gets going and settles
    // back as he slows -- the lean IS the acceleration, visually.
    float rate = (m_bendTarget > 0.0f || m_activity == Activity::Lift)
                     ? params.bendRate : params.leanRate;
    float blend = 1.0f - std::exp(-rate * dt);
    m_torsoPitch += (target - m_torsoPitch) * blend;

    // The twist is locked to the gait rather than eased. It is a consequence of
    // the stride, not a thing that lags behind it -- easing it would put the
    // shoulders out of phase with the arms they are supposed to be driving.
    float armSwing = m_running ? params.runArmSwing : params.armSwing;
    m_torsoYaw = params.torsoTwistRatio * armSwing
               * std::sin(gaitPhase() * 2.0f * kPi);
}

glm::vec3 Biped::chestCentre() const {
    return m_hipCentre + torsoUp() * params.torsoRise;
}

glm::vec3 Biped::shoulderJoint(int i) const {
    float side = (i == 0) ? -1.0f : 1.0f;
    // torsoRight, not right: the shoulders have to ride the twist, or the whole
    // thing is a chest box rotating inside a fixed pair of arms.
    return chestCentre()
         + torsoUp() * params.shoulderRise
         + torsoRight() * (side * params.hipWidth * params.shoulderWidthFrac);
}

// Where each hand is swinging to. Computed in update() rather than in solveArm
// because it needs the terrain, and because a goal is state -- it is the thing
// an arm would be given if it were ever reaching for something real.
void Biped::updateHandGoals(const Ground& hf) {
    // In the TORSO's frame: when he leans, the arms hang off the lean rather
    // than staying vertical, which is the difference between a runner and a
    // man falling forwards.
    const glm::vec3 up  = torsoUp();
    const glm::vec3 fwd = torsoForward();
    const float armLength = params.upperArm + params.forearm;

    // Running arms differ in KIND, not degree: elbows much tighter and driving
    // fore-and-aft rather than hanging and swinging.
    const float armSwing  = m_running ? params.runArmSwing  : params.armSwing;
    const float armExtend = m_running ? params.runArmExtend : params.armExtend;

    // The hands swing on an arc through the shoulder, each opposite the leg on
    // its own side. That opposition is most of what sells a walk as a walk.
    const float swing = std::sin(gaitPhase() * 2.0f * kPi);

    // Reaching for something real. This is the whole reason the hand goal was
    // state rather than a value computed inside solveArm -- the arms do not know
    // or care whether they are swinging or fetching.
    if (m_handOverride) {
        for (int i = 0; i < 2; ++i) m_handGoal[i] = m_handTarget[i];
        return;
    }

    for (int i = 0; i < 2; ++i) {
        const glm::vec3 shoulder = shoulderJoint(i);
        float angle = glm::radians(armSwing) * (i == 0 ? -swing : swing);

        glm::vec3 direction = glm::normalize(-up * std::cos(angle) + fwd * std::sin(angle));
        glm::vec3 goal = shoulder + direction * (armLength * armExtend);

        // Keep the hand out of the ground. Arms hang beside the body, so on a
        // slope the terrain BESIDE him can be well above the terrain he is
        // standing on -- measured at 1.12 units of hand buried in the hillside
        // before this clamp. Lifting the goal and letting the solver bend the
        // elbow is what an arm does when it brushes a wall.
        float ground = hf.heightAt(goal.x, goal.z, goal.y) + 0.14f;
        goal.y = std::max(goal.y, ground);

        // But cap the lift, or a wall right beside him raises the goal all the
        // way to the shoulder and the arm folds flat into his chest -- measured
        // at a hand 0.12 from its own shoulder with the elbow dead straight.
        // Past this point he keeps his arm down and the hand clips the hillside,
        // which is much the lesser of the two.
        goal.y = std::min(goal.y, shoulder.y - armLength * armExtend * 0.45f);

        m_handGoal[i] = goal;
    }
}

void Biped::solveArm(int i, glm::vec3& outElbow, glm::vec3& outHand) const {
    const glm::vec3 shoulder = shoulderJoint(i);
    const float side = (i == 0) ? -1.0f : 1.0f;
    const float armLength = params.upperArm + params.forearm;

    // Elbows point back and slightly out -- the opposite of the knees, which is
    // the whole anatomical difference between an arm and a leg.
    glm::vec3 pole = shoulder
                   - forward() * armLength
                   + right() * (side * armLength * 0.35f);

    solveTwoBone(shoulder, m_handGoal[i], params.upperArm, params.forearm,
                 pole, outElbow, outHand);
}

// The foot's frame: the ground's up-vector, with the body's heading projected
// onto it. Built from the ankle's normal rather than the world's, which is the
// whole point of the servo.
glm::vec3 Biped::footForward(int i) const {
    glm::vec3 up = m_footUp[i];
    glm::vec3 f = forward() - up * glm::dot(forward(), up);
    if (glm::dot(f, f) < 1e-8f) return forward();
    return glm::normalize(f);
}

glm::vec3 Biped::footRight(int i) const {
    // up x forward, for the same reason as the head. This one never showed,
    // because a mirrored box is still the same box.
    return glm::normalize(glm::cross(m_footUp[i], footForward(i)));
}

void Biped::updateAnkles(const Ground& hf, float dt) {
    const glm::vec3 worldUp(0, 1, 0);
    const float limit = glm::radians(params.ankleLimitDeg);
    const float cosLimit = std::cos(limit);
    const float sinLimit = std::sin(limit);

    for (int i = 0; i < 2; ++i) {
        // Planted: match the ground he is standing on. Swinging: match the ground
        // he is about to land on, so the foot arrives already flat instead of
        // slapping down and then rotating.
        const glm::vec3 sample = m_inStance[i] ? m_foot[i] : m_swingTo[i];
        glm::vec3 target = hf.normalAt(sample.x, sample.z, sample.y);

        // A servo has a stop. Past its limit the foot stays as far over as it
        // goes and the ground wins -- which is what standing on something too
        // steep to stand on looks like.
        if (glm::dot(target, worldUp) < cosLimit) {
            glm::vec3 tangent = target - worldUp * glm::dot(target, worldUp);
            if (glm::dot(tangent, tangent) > 1e-8f) {
                target = worldUp * cosLimit + glm::normalize(tangent) * sinLimit;
            }
        }

        float blend = 1.0f - std::exp(-params.ankleRate * dt);
        m_footUp[i] = glm::normalize(m_footUp[i] + (target - m_footUp[i]) * blend);
    }
}

// Point him at where he is going, or at the door if where he is going is on the
// other side of one. Returns false if he is walking to the door rather than to
// the thing, so the caller knows not to check whether it has arrived yet.
//
// Asked EVERY FRAME rather than worked out when the job was handed to him, which
// is the whole fix. Before, the staging point was decided once, at assignment:
// whether he was aboard was true or false at that instant and then never looked
// at again. So he would be sent for a crate while standing in the hold, walk out
// correctly, pick it up -- and the fact that he was now outside, with a delivery
// to make inside, was news to nobody. He crossed the threshold and his plan did
// not. Which side of a wall you are on is not something you can be told once.
// Follow a planned route toward `to`, replanning when it goes stale. Sets the
// immediate goal to the next node along, which is never more than a couple of
// units away and is always somewhere he can actually stand.
//
// This is the difference between steering and navigating, and he needed both.
// Aiming straight at a goal and turning away from ground he cannot walk works
// perfectly in open country and is helpless the moment the way round is not
// roughly toward the thing. He sat in a bay of terrain refusing every heading he
// tried -- SIXTY refusals a second, measured -- circling the same three points
// eleven units from a crate for as long as anyone watched. No amount of better
// turning fixes that; a creature that cannot see round a corner has to be told
// what is round it.
void Biped::steerAlongRoute(const Ground& hf, const glm::vec3& to) {
    const bool stale = m_route.empty()
                    || glm::length(glm::vec2(to.x - m_routedTo.x, to.z - m_routedTo.z)) > 3.0f;

    if (stale || m_replan) {
        m_routedTo = to;
        m_replan = false;
        m_routeIndex = 0;

        const float maxRise = hf.spacing() * std::tan(glm::radians(params.maxSlopeDeg));
        std::vector<glm::ivec2> nodes;
        if (hf.findRoute(hf.nodeNear(m_hipCentre), soleHeight(), hf.nodeNear(to),
                         maxRise, params.bodyRadius, standHeight(), nodes)) {
            m_route.clear();
            for (const glm::ivec2& node : nodes) m_route.push_back(hf.terrain().worldAt(node));
        } else {
            // No way there at all. Say so rather than steering at it and hoping:
            // hoping is what the circling WAS. The scene already knows what to do
            // with a hauler that cannot reach a crate -- it marks the crate as
            // one he has refused and offers it to the other one -- and that is a
            // far better answer than a creature walking in circles for a minute
            // at a crate on the wrong side of a ridge.
            m_route.clear();
            m_routeFailed = true;
        }
    }

    // Drop nodes as he reaches them. Generously, because he is a body a metre
    // wide walking a lattice two units across and standing exactly on a node is
    // not a thing worth waiting for.
    while (m_routeIndex < m_route.size()) {
        const glm::vec3& at = m_route[m_routeIndex];
        if (glm::length(m_pos - glm::vec2(at.x, at.z)) > 2.5f) break;
        ++m_routeIndex;
    }

    // The last stretch is aimed at the thing itself rather than at the node
    // nearest it, or he stops a node short of everything he ever reaches for.
    m_goal = (m_routeIndex < m_route.size())
                 ? glm::vec2(m_route[m_routeIndex].x, m_route[m_routeIndex].z)
                 : glm::vec2(to.x, to.z);
    m_goalActive = true;
}

bool Biped::aimAt(const Ground& hf, const glm::vec3& target) {
    glm::vec3 door;
    bool shut = false;

    // Asked about his FEET, not his chest.
    //
    // Whether you are in a room, and whether you are up in a doorway, are both
    // questions about where you are standing. Asked of the hips they are asked
    // about a point nearly three units in the air, which is above the cargo deck
    // while he is stood on the dirt beside the hull -- so a rule meant for
    // somebody partway up the ramp fired at him out on the flat, sent him back to
    // the muster point, released him, and let him turn for his goal, over and
    // over. That is the circling at the ring: not steering, not routing, just a
    // question asked about the wrong part of him.
    const glm::vec3 standing(m_pos.x, soleHeight(), m_pos.y);

    if (!hf.wayThrough(standing, target, door, shut)) {
        m_wayShut = false;
        steerAlongRoute(hf, target);
        return true;
    }

    // Shut is not a reason to stand still. Ground hands back the CONTROL as the
    // next place to go when the way is closed and he is on the outside of it, so
    // he walks to the button and works it -- which is a thing he can do and the
    // walker cannot, because he has hands and the walker has four feet.
    //
    // Nothing here presses anything. He gets himself to the panel and the scene
    // notices him standing at it, the same way it notices the player standing
    // there. A creature that could reach into the ship and open its ramp from
    // across the field would be a different and much worse kind of creature.
    m_wayShut = shut;
    steerAlongRoute(hf, door);
    return false;
}

void Biped::assignFetch(const Ground& hf, const glm::vec3& crate, const glm::vec3& storage) {
    m_crate = crate;
    m_storage = storage;
    m_wayShut = false;
    m_routeFailed = false;
    m_replan = true;         // a new job is a new route
    m_hasTask = true;
    m_carrying = false;
    m_activity = Activity::Approach;
    m_taskTimer = 0.0f;

    // Looked at before accepted. The walker has always done this -- no route, no
    // job -- and it is the difference between refusing work and pretending to do
    // it. A refused crate goes back on the pile for the other one to try.
    //
    // Through aimAt rather than straight at the crate, because a SHUT DOOR is not
    // the same as an unreachable crate and the raw search cannot tell them apart:
    // with the ramp closed there is no route to anywhere outside, so asking the
    // search directly had him decline every job in the world while stood in the
    // hold next to a button that would have opened it.
    aimAt(hf, crate);
    if (m_routeFailed) {
        m_hasTask = false;
        m_activity = Activity::Wander;
    }
}

void Biped::abandonTask() {
    m_hasTask = m_carrying = m_handOverride = m_goalActive = false;
    m_wayShut = false;
    m_routeFailed = false;
    m_route.clear();
    m_activity = Activity::Wander;
    m_bendTarget = 0.0f;
    m_squat = 0.0f;
    m_speedScale = 1.0f;
}

const char* Biped::activityName() const {
    switch (m_activity) {
        case Activity::Approach: return "walking to the crate";
        case Activity::Reach:    return "bending to pick it up";
        case Activity::Lift:     return "standing up with it";
        case Activity::Carry:    return "carrying it to storage";
        case Activity::Place:    return "setting it down";
        default:                 return "wandering";
    }
}

// Where hand `i` should take hold of an object: its side, at hand width.
glm::vec3 Biped::graspPoint(int i, const glm::vec3& object) const {
    float side = (i == 0) ? -1.0f : 1.0f;
    return object + right() * (side * 0.42f);
}

glm::vec3 Biped::cargoPosition() const {
    if (!m_carrying) return m_crate;

    // Carried between the hands, so the crate inherits whatever the arms are
    // doing rather than being pinned to the chest and sliding through them.
    glm::vec3 leftElbow, leftHand, rightElbow, rightHand;
    solveArm(0, leftElbow, leftHand);
    solveArm(1, rightElbow, rightHand);
    return (leftHand + rightHand) * 0.5f;
}

void Biped::updateTask(const Ground& hf, float dt) {
    if (m_holding) {
        // Everything the task was doing is left exactly as it was, so letting go
        // of the hold picks the job back up rather than restarting it.
        m_goalActive = false;
        m_speedScale = 0.0f;
        m_bendTarget = 0.0f;
        m_squat += (0.0f - m_squat) * std::min(1.0f, dt * params.bendRate);

        // But he keeps hold of whatever he is holding. The crate is carried
        // BETWEEN his hands rather than pinned to his chest, so dropping the
        // hand override lets his arms swing back down to his sides and takes the
        // crate with them -- it ends up hanging between his knees while he
        // stands there talking to you.
        m_handOverride = m_carrying;
        if (m_carrying) {
            const glm::vec3 carrySpot = chestCentre()
                                      + torsoForward() * params.carryAhead
                                      + torsoUp() * params.carryRise;
            for (int i = 0; i < 2; ++i) m_handTarget[i] = graspPoint(i, carrySpot);
        }
        return;
    }

    // How far he has to fold to get a hand to height `y`. A crate on the dirt is
    // a full squat; one being set on top of a waist-high pile is barely a lean.
    // Scaling both the bend and the squat by this means stacking gets easier the
    // higher the pile, which is what it should look like.
    //
    // Measured against his STANDING height, not his current one. Using the live
    // hip height makes it a feedback loop -- squatting lowers the hips, which
    // shrinks the depth, which stands him back up -- and he oscillates in front
    // of the crate forever instead of picking it up. It cost 5 of 16 deliveries
    // and every run hit the timeout.
    const float nominalHip = hf.heightAt(m_pos.x, m_pos.y, m_foot[0].y)
                           + (params.thigh + params.shin) * params.standFrac;

    auto depthFor = [&](float y) {
        return std::clamp((nominalHip - y - 0.55f) / 1.55f, 0.0f, 1.0f);
    };
    float reachDepth = 0.0f;

    m_handOverride = false;
    m_goalActive = false;
    m_speedScale = 1.0f;
    m_bendTarget = 0.0f;

    // Lost the way mid-job. Same answer as never having had one.
    if (m_routeFailed && m_hasTask) {
        abandonTask();
        m_squat += (0.0f - m_squat) * std::min(1.0f, dt * params.bendRate);
        return;
    }

    if (!m_hasTask) {
        m_activity = Activity::Wander;

        // Idle, and too far out: walk back. Routed rather than aimed, so a hill
        // between him and home is something he goes round rather than into.
        if (m_homeRadius > 0.0f) {
            const float out = glm::length(glm::vec2(m_pos.x - m_home.x, m_pos.y - m_home.z));
            if (out > m_homeRadius) {
                aimAt(hf, m_home);
                m_squat += (0.0f - m_squat) * std::min(1.0f, dt * params.bendRate);
                return;
            }
        }
        m_squat += (0.0f - m_squat) * std::min(1.0f, dt * params.bendRate);
        return;
    }

    // Where he is trying to get to, on the ground.
    auto distanceTo = [&](const glm::vec3& p) {
        return glm::length(glm::vec2(p.x, p.z) - m_pos);
    };

    // If he cannot reach from where he stopped, close in.
    //
    // Stopping at a fixed distance assumes the target is at his feet. On a slope
    // it is not: standing uphill of the pile puts the slot 2.8 units below his
    // hips, and his arm is 2.2, so no amount of folding gets him there -- he
    // just stoops at it until the timer runs out. Creeping forward also walks
    // him DOWN the slope, which is what actually closes the gap.
    auto closeIn = [&](glm::vec2 target) {
        if (m_taskTimer < 0.5f) return 0.0f;    // give the fold a moment first
        m_goal = target;
        m_goalActive = true;
        return 0.30f;
    };

    // Are both hands where they were told to go?
    auto handsArrived = [&]() {
        for (int i = 0; i < 2; ++i) {
            glm::vec3 elbow, hand;
            solveArm(i, elbow, hand);
            if (glm::length(hand - m_handTarget[i]) > params.graspDistance) return false;
        }
        return true;
    };

    const glm::vec3 chest = chestCentre();
    const glm::vec3 carrySpot = chest
                              + torsoForward() * params.carryAhead
                              + torsoUp() * params.carryRise;

    switch (m_activity) {
        case Activity::Approach: {
            if (!aimAt(hf, m_crate)) break;   // going out through the door first

            if (distanceTo(m_crate) < params.reachDistance) {
                m_activity = Activity::Reach;
                m_taskTimer = 0.0f;
            }
            break;
        }

        case Activity::Reach: {
            // Stopped, bent at the hip, hips dropped, both hands out at the
            // crate. Nothing here poses an arm -- the hands are given a place to
            // be and the same solver that handles the swing does the rest.
            m_handOverride = true;
            for (int i = 0; i < 2; ++i) m_handTarget[i] = graspPoint(i, m_crate);
            reachDepth = depthFor(m_crate.y);

            m_taskTimer += dt;
            m_speedScale = closeIn(glm::vec2(m_crate.x, m_crate.z));

            if (m_taskTimer > 0.25f && handsArrived()) {
                m_carrying = true;
                m_activity = Activity::Lift;
                m_taskTimer = 0.0f;
            }
            // If he cannot reach it after a while, give up rather than stoop
            // there forever.
            if (m_taskTimer > 9.0f) abandonTask();
            break;
        }

        case Activity::Lift: {
            m_speedScale = 0.0f;
            m_bendTarget = 0.0f;
            m_handOverride = true;
            for (int i = 0; i < 2; ++i) m_handTarget[i] = graspPoint(i, carrySpot);

            if (m_torsoPitch < 8.0f && m_squat < 0.12f) m_activity = Activity::Carry;
            break;
        }

        case Activity::Carry: {
            m_handOverride = true;
            for (int i = 0; i < 2; ++i) m_handTarget[i] = graspPoint(i, carrySpot);

            if (!aimAt(hf, m_storage)) break;   // in through the door first

            if (distanceTo(m_storage) < params.placeDistance) {
                m_activity = Activity::Place;
                m_taskTimer = 0.0f;
            }
            break;
        }

        case Activity::Place: {
            m_handOverride = true;

            // m_storage is the exact place the crate should end up -- the top
            // of the pile, not the dirt. He reaches for that.
            for (int i = 0; i < 2; ++i) m_handTarget[i] = graspPoint(i, m_storage);
            reachDepth = depthFor(m_storage.y);

            m_taskTimer += dt;
            m_speedScale = closeIn(glm::vec2(m_storage.x, m_storage.z));

            if (m_taskTimer > 0.25f && handsArrived()) {
                m_crate = cargoPosition();      // it stays where his hands left it
                m_carrying = false;
                m_hasTask = false;
                m_activity = Activity::Wander;
            }
            if (m_taskTimer > 9.0f) { m_crate = cargoPosition(); abandonTask(); }
            break;
        }

        default: break;
    }

    // Ease the fold toward whatever depth the state asked for.
    m_bendTarget = params.bendAngle * reachDepth;
    m_squat += (reachDepth - m_squat) * std::min(1.0f, dt * params.bendRate);
}

glm::vec3 Biped::headCentre() const {
    return chestCentre() + torsoUp() * params.headRise;
}

// The head's frame: the body's, yawed then pitched. Kept horizontal in yaw so
// he cannot roll his head, which nothing with a neck does casually.
glm::vec3 Biped::headRight() const {
    float y = glm::radians(m_headYaw);
    return forward() * -std::sin(y) + right() * std::cos(y);
}

glm::vec3 Biped::headForward() const {
    float y = glm::radians(m_headYaw);
    float p = glm::radians(m_headPitch);
    glm::vec3 flat = forward() * std::cos(y) + right() * std::sin(y);
    return glm::normalize(flat * std::cos(p) + glm::vec3(0, 1, 0) * std::sin(p));
}

glm::vec3 Biped::headUp() const {
    // forward x right, NOT right x forward. Swapped, this returns straight DOWN
    // for a level head -- which inverted the whole head frame, drew any loaded
    // model upside down, and made the "rise" offset push it further into the
    // torso the more you raised it.
    return glm::normalize(glm::cross(headForward(), headRight()));
}

void Biped::updateHead(const glm::vec3* observer, float dt) {
    float targetYaw = 0.0f;
    float targetPitch = 0.0f;
    bool see = false;

    if (observer) {
        const glm::vec3 head = headCentre();
        const glm::vec3 toObserver = *observer - head;

        float distance = glm::length(toObserver);
        glm::vec2 flat(toObserver.x, toObserver.z);
        float flatDistance = glm::length(flat);

        if (distance > 0.5f && distance < params.lookRange && flatDistance > 1e-4f) {
            glm::vec2 dir = flat / flatDistance;
            glm::vec2 fwd(forward().x, forward().z);
            glm::vec2 rgt(right().x, right().z);

            // Signed bearing off his nose: negative is to his left.
            float bearing = glm::degrees(std::atan2(glm::dot(dir, rgt), glm::dot(dir, fwd)));

            // Hysteresis. Without it, walking along his edge of vision makes the
            // head flick on and off every few frames as the bearing crosses the
            // boundary -- he looks broken rather than uncertain.
            float limit = params.fovDegrees + (m_watching ? 12.0f : 0.0f);

            if (std::fabs(bearing) <= limit) {
                see = true;
                targetYaw = std::clamp(bearing, -params.neckYawLimit, params.neckYawLimit);

                float pitch = glm::degrees(std::atan2(toObserver.y, flatDistance));
                targetPitch = std::clamp(pitch, -params.neckPitchLimit, params.neckPitchLimit);
            }
        }
    }

    m_watching = see;

    // Exponential ease, so the head leads and settles rather than snapping. It
    // also means that once he is locked on, his OWN turning is what moves the
    // head -- the target is measured against his body, so as he walks past you
    // the neck counter-rotates to hold you without anything asking it to.
    float blend = 1.0f - std::exp(-params.headTurnRate * dt);
    m_headYaw   += (targetYaw   - m_headYaw)   * blend;
    m_headPitch += (targetPitch - m_headPitch) * blend;
}

float Biped::gaitPhase() const {
    // Already one full cycle of two steps, so the arms swing once per pair.
    return m_cyclePhase;
}

void Biped::reset(const Ground& hf, glm::vec2 position, float headingDeg, uint32_t seed) {
    m_rngState = seed ? seed : 1u;

    float half = hf.n() * 0.5f * hf.spacing() - 6.0f;
    m_pos = glm::clamp(position, glm::vec2(-half), glm::vec2(half));
    m_yaw = m_desiredYaw = headingDeg;

    m_cyclePhase = 0.0f;

    // Both feet start planted either side of him, half a cycle apart, so the
    // first stride is a normal one rather than a lurch.
    //
    // Restoring this: the gait rewrite deleted it, and the feet defaulted to the
    // world origin. He then spent his first second with legs stretched up to 56
    // units across the map to a pair of feet at (0,0,0) -- rare enough to hide
    // in an average (0.9% of frames) and impossible to miss on screen.
    for (int i = 0; i < 2; ++i) {
        float side = (i == 0) ? -1.0f : 1.0f;
        glm::vec3 offset = right() * (side * params.hipWidth * 0.5f);
        glm::vec2 footXZ(m_pos.x + offset.x, m_pos.y + offset.z);
        m_foot[i] = glm::vec3(footXZ.x, hf.terrainHeight(footXZ.x, footXZ.y), footXZ.y);

        m_plant[i] = m_swingFrom[i] = m_swingTo[i] = m_foot[i];
        m_localPhase[i] = (i == 0) ? 0.0f : 0.5f;
        m_inStance[i] = true;
    }
    m_footUp[0] = hf.normalAt(m_foot[0].x, m_foot[0].z, m_foot[0].y);
    m_footUp[1] = hf.normalAt(m_foot[1].x, m_foot[1].z, m_foot[1].y);
    m_running = m_airborne = m_wasAirborne = false;
    m_airFraction = 0.0f;
    m_torsoPitch = 0.0f;
    m_headYaw = m_headPitch = 0.0f;
    m_watching = false;

    m_hipY = hf.terrainHeight(m_pos.x, m_pos.y)
           + (params.thigh + params.shin) * params.standFrac;
    m_hipVelY = 0.0f;
    m_hipCentre = glm::vec3(m_pos.x, m_hipY, m_pos.y);

    updateHandGoals(hf);
}

// Where his soles actually are.
//
// The obvious estimate -- hips minus a full leg -- is WRONG, and wrong in the
// one direction that matters. He stands at 0.90 of full extension, so it puts
// his feet a third of a unit UNDER the floor he is standing on. Against a
// blocker that is harmless, because a blocker has margin above and below. Against
// a surface it is fatal: the test there is whether the floor passes through him,
// and a body whose soles are below the floor it is standing on always fails it.
//
// The symptom was a creature who stepped onto the ramp and was immediately shoved
// back off it, every frame, forever -- because he was judged to be inside the very
// thing he was standing on.
//
// The foot bearing WEIGHT -- which is a different thing from the higher one, and
// the difference is half a unit of nonsense.
//
// Taking the maximum was meant to pick the foot that is on the deck while the
// other dangles. On flat ground it picks the foot in the AIR: the swing lifts it
// better than half a unit, so he reports himself standing that far above ground
// he is walking on, for most of every stride.
//
// Against a blocker that is harmless -- they have margin. Against a rule that
// asks whether he is up on a RAMP it is fatal, because standing on a ramp is
// exactly "higher than the dirt". So the doorway rule fired at him out on open
// country every time a foot came up, sent him back to the muster mark, released
// him when the foot came down, and let him turn for his goal again. That is the
// circling at the ring, and no amount of routing or steering was ever going to
// fix it: he was being asked where he stood and answering with a foot in mid-air.
float Biped::soleHeight() const {
    if (m_inStance[0] && m_inStance[1]) return std::max(m_foot[0].y, m_foot[1].y);
    if (m_inStance[0]) return m_foot[0].y;
    if (m_inStance[1]) return m_foot[1].y;

    // Both off the ground, which is a run's flight phase. The lower is the truer
    // account of the floor he left and is about to land on.
    return std::min(m_foot[0].y, m_foot[1].y);
}

float Biped::standHeight() const {
    // Hips at rest, then everything stacked above them, plus a little for the
    // crown of the head sitting above its own centre.
    return (params.thigh + params.shin) * params.standFrac
         + params.torsoRise + params.shoulderRise + params.headRise + 0.45f;
}

// Can he get from `from` to a point `distance` along `headingDeg` without the
// ground rising or falling faster than the limit, and without walking into
// something solid? Sampled in a few places because a cliff edge between two
// samples is exactly the thing that catches a creature out.
bool Biped::passable(const Ground& hf, glm::vec2 from, float headingDeg,
                     float distance) const
{
    float yaw = glm::radians(headingDeg);
    glm::vec2 dir(std::sin(yaw), std::cos(yaw));

    float half = hf.n() * 0.5f * hf.spacing() - 4.0f;
    glm::vec2 destination = from + dir * distance;
    if (destination.x < -half || destination.x > half ||
        destination.y < -half || destination.y > half) {
        return false;
    }

    const int kSamples = 4;
    const float maxRise = std::tan(glm::radians(params.maxSlopeDeg)) * (distance / kSamples);

    const float body = standHeight();

    // Each sample's surface found by stepping up from the LAST sample's, rather
    // than all of them measured from where he stands.
    //
    // Chained like this a ramp is a run of small rises, each one inside what he
    // will step onto, and he follows it all the way up. Measured independently
    // from his own feet it is one big rise at the far end, refused -- which is
    // why he used to walk straight through the ramp on level ground with every
    // check agreeing he was fine.
    float previous = hf.heightAt(from.x, from.y, soleHeight(), params.stepUp);
    for (int s = 1; s <= kSamples; ++s) {
        glm::vec2 p = from + dir * (distance * s / kSamples);

        float height = hf.heightAt(p.x, p.y, previous, params.stepUp);
        if (std::fabs(height - previous) > maxRise) return false;

        // Standing THERE, not here: the surface he would be on has already been
        // worked out, so the solid test asks whether a body stood on it would be
        // inside something. That is what makes the hull wall a refusal at the
        // same moment as a cliff, through the same code, without the steering
        // above knowing there is a difference between the two.
        if (hf.blocked(p.x, p.y, height, body, params.bodyRadius)) return false;

        previous = height;
    }
    return true;
}

void Biped::steer(const Ground& hf, float dt) {
    // How far ahead he checks before committing to a heading.
    //
    // Seven and a half units when he is finding his own way, which is what makes
    // him turn away from a bank before he is standing on it. But a body following
    // a ROUTE has already been told the way, and the next mark on it is two units
    // off and known good -- so probing four times that distance means refusing a
    // heading because of ground well beyond where he is being sent. He sidesteps,
    // the route pulls him back, and the two argue at about thirty refusals a
    // second while he walks in a small circle.
    //
    // Knowing the way is exactly the licence to look less far ahead.
    const bool routed = m_routeIndex < m_route.size();
    const float probe = routed ? 2.5f : (params.walkSpeed * 0.9f + 3.0f);

    m_refused = false;

    // Held: turn to face whoever is looking, and nothing else.
    if (m_holding) {
        if (m_hasObserver) {
            glm::vec2 to(m_observer.x - m_pos.x, m_observer.z - m_pos.y);
            if (glm::dot(to, to) > 1e-4f) {
                m_desiredYaw = glm::degrees(std::atan2(to.x, to.y));
            }
        }
        float delta = wrapDegrees(m_desiredYaw - m_yaw);
        float maxTurn = params.holdTurnRate * dt;
        m_yaw = wrapDegrees(m_yaw + std::clamp(delta, -maxTurn, maxTurn));
        return;
    }

    // With a goal he aims at it; the terrain check below still gets the last
    // word, so he walks around a bank rather than into it.
    if (m_goalActive) {
        glm::vec2 to = m_goal - m_pos;
        if (glm::dot(to, to) > 1e-6f) {
            m_desiredYaw = glm::degrees(std::atan2(to.x, to.y));
        }
    }

    if (!passable(hf, m_pos, m_desiredYaw, probe)) {
        // Turn to the nearest heading that is not a wall. Sweeping outward in
        // pairs means he prefers a small correction to a large one, which is
        // what makes it look like he is following the ground rather than
        // panicking.
        ++m_refusalCount;
        m_refused = true;

        bool found = false;
        for (int step = 1; step <= 6 && !found; ++step) {
            for (int sign = -1; sign <= 1 && !found; sign += 2) {
                float candidate = m_desiredYaw + sign * step * 30.0f;
                if (passable(hf, m_pos, candidate, probe)) {
                    m_desiredYaw = candidate;
                    found = true;
                }
            }
        }
        if (!found) m_desiredYaw += 180.0f;   // boxed in: turn around
    } else if (!m_goalActive) {
        // Idle drift, so a walk across open ground is not a straight line.
        m_desiredYaw += randomSigned() * params.wanderRate * dt;
    }

    float delta = wrapDegrees(m_desiredYaw - m_yaw);
    float maxTurn = params.turnRate * dt;
    m_yaw = wrapDegrees(m_yaw + std::clamp(delta, -maxTurn, maxTurn));
    m_desiredYaw = wrapDegrees(m_desiredYaw);
}

// Where foot `i` should be put down at the end of its swing.
glm::vec3 Biped::footTarget(const Ground& hf, int i,
                            float swingTime, float stanceTime) const {
    const float side = (i == 0) ? -1.0f : 1.0f;
    const float fullLeg = params.thigh + params.shin;

    // ACTUAL speed, not the parameter. Standing still, this collapses to zero
    // and the foot is planted directly under its own hip -- which is what makes
    // stepping on the spot possible at all: the same code that takes a stride
    // forward at speed puts a foot squarely beneath him at rest.
    const float speed = params.walkSpeed * m_speedScale;

    // Plant ahead of where the body will BE when the step lands, not where it is
    // now. Aiming at the current position makes him walk on his heels and shrink
    // his stride the faster he goes.
    float reach = speed * swingTime * params.strideScale;

    // Cap the stride against the leg, using how far the body ACTUALLY travels
    // while this foot is down. The earlier version assumed a foot was planted
    // for exactly one step, which stopped being true the moment duty factor
    // arrived -- at a duty of 0.62 the body covers half again as much ground
    // during a stance, so the foot ended up further behind than the leg is long
    // and the solver clamped on up to 38% of frames.
    // A running foot must leave the ground while it is still fairly under him.
    // Let it trail as far as the walk does and the reach bound is pulling the
    // hips down at the very moment the push is trying to lift them.
    const float maxTrail = fullLeg * (m_running ? 0.34f : 0.50f);
    float travel = speed * stanceTime;
    if (travel - reach > maxTrail) {
        reach = std::max(fullLeg * 0.15f, travel - maxTrail);
    }

    glm::vec3 target = glm::vec3(m_pos.x, 0.0f, m_pos.y)
                     + forward() * reach
                     + right() * (side * params.hipWidth * 0.5f);

    // Referenced to the ground under HIM, not to the foot that is lifting off.
    //
    // The trailing foot is a whole stride behind, and a stride behind on a slope
    // is a long way down. Measuring from there forces a choice between two wrong
    // things: allow a reach big enough to cover the stride, and he can step up
    // onto the flank of the ramp and onto the cargo deck out of the dirt beside
    // the hull; allow a small one, and he cannot find the slope he is already
    // standing on.
    //
    // Measuring from under his own hips separates them. The distance from him to
    // where the foot lands is short, so a slope stays a small rise -- while a
    // ledge is its full height however he came at it.
    const float underHim = hf.heightAt(m_pos.x, m_pos.y, soleHeight(), params.stepUp);
    target.y = hf.heightAt(target.x, target.z, underHim, params.stepUp);
    return target;
}

void Biped::update(const Ground& hf, float dt, const glm::vec3* observer) {
    if (dt <= 0.0f) return;

    m_hasObserver = observer != nullptr;
    if (observer) m_observer = *observer;
    dt = std::min(dt, 0.1f);   // a hitch should not teleport him

    updateTask(hf, dt);
    steer(hf, dt);

    // Slow down through a hard turn, the way anything with legs does.
    float turnPenalty = 1.0f - 0.55f * std::min(1.0f, std::fabs(
        wrapDegrees(m_desiredYaw - m_yaw)) / 90.0f);
    m_pos += glm::vec2(forward().x, forward().z) * (params.walkSpeed * turnPenalty * m_speedScale * dt);

    float half = hf.n() * 0.5f * hf.spacing() - 4.0f;
    m_pos = glm::clamp(m_pos, glm::vec2(-half), glm::vec2(half));

    // Then put him back outside anything he has ended up inside.
    //
    // Belt as well as braces, and the braces are the real mechanism -- steering
    // refuses to walk into a wall in the first place. But refusing only looks
    // AHEAD, and there are ways to be somewhere without having walked there: the
    // ship is set down on top of him when the terrain is regenerated, the ramp
    // rises through him if he is standing under it, and a shove sideways out of
    // a turn covers ground no probe was pointed at. Without this he sinks into
    // the hull in exactly those cases and never comes out, because from inside a
    // wall every direction is refused and he stands there turning.
    // Against the floor UNDER HIM, reached for from his soles -- not the soles
    // themselves.
    //
    // His hips cross onto a ramp a stride before his feet do, so for a moment his
    // soles are on the flat behind it while his body is over it. Judged by his
    // soles he is under the ramp in that moment and gets shoved back off, and the
    // moment repeats every stride: he walks to the foot of the ramp and can never
    // get on. Judged by the floor beneath him he is standing on the ramp, which is
    // what a creature halfway onto a ramp is doing.
    //
    // And when he genuinely IS under it -- soles on the dirt, slab two units over
    // his head -- the ramp is out of stepping range, so the floor under him comes
    // back as the dirt and the test still catches him.
    const float floor = hf.heightAt(m_pos.x, m_pos.y, soleHeight(), params.stepUp);
    const glm::vec2 shoved = hf.resolve(m_pos, floor, standHeight(), params.bodyRadius);

    // Being pushed moves ALL of him, feet included.
    //
    // His feet are planted at world positions and stay there while the hips
    // travel over them -- which is the whole basis of the walk, and is exactly
    // wrong for a shove. Moving the hips alone strands them: the legs stretch to
    // reach ground he is no longer standing over, the solver clamps at full
    // extension, and the hips get dragged down to whatever the legs can still
    // reach. Standing still under a ramp swinging open, he was pushed four units
    // clear and his hips ended at ground level with his feet somewhere behind
    // him. He did not fall over; he was pulled apart and then down.
    //
    // A shove is a translation of a creature, not a correction to a coordinate.
    const glm::vec2 push = shoved - m_pos;
    if (glm::dot(push, push) > 1e-8f) {
        const glm::vec3 push3(push.x, 0.0f, push.y);
        for (int i = 0; i < 2; ++i) {
            m_foot[i]      += push3;
            m_plant[i]     += push3;
            m_swingFrom[i] += push3;
            m_swingTo[i]   += push3;
        }
        m_hipCentre += push3;
    }
    m_pos = shoved;

    // ---- gait ---------------------------------------------------------
    // Break into a run above a speed, drop back below it. The hysteresis band
    // matters: without it he flickers between gaits while cruising at the
    // threshold, changing duty factor several times a second.
    float speed = params.walkSpeed * turnPenalty * m_speedScale;
    if (!m_running && speed > params.runSpeed)        m_running = true;
    else if (m_running && speed < params.runSpeed * 0.85f) m_running = false;

    const float duty      = m_running ? params.runDuty : params.walkDuty;
    const float cycleTime = 2.0f * std::max(0.05f, params.stepTime);
    const float swingTime = (1.0f - duty) * cycleTime;
    const float footLift  = m_running ? params.runFootLift : params.footLift;
    const float fullLeg   = params.thigh + params.shin;

    // Standing still stops the cycle -- but only once both feet are down, or
    // he freezes with a foot in the air.
    float phaseRate = m_speedScale;

    // ...and not while he is turning on the spot. A body that rotates over
    // planted feet does not turn, it wrings its own legs off; what actually
    // happens is a series of small steps that put the feet back underneath.
    // Keeping the cycle running while he is out of square gives exactly that,
    // out of the walking machinery, with nothing new to animate.
    float yawError = std::fabs(wrapDegrees(m_desiredYaw - m_yaw));
    if (yawError > params.turnStepDeg) phaseRate = std::max(phaseRate, params.turnStepRate);

    if (!m_inStance[0] || !m_inStance[1]) phaseRate = std::max(phaseRate, 1.0f);
    m_cyclePhase = std::fmod(m_cyclePhase + dt * phaseRate / cycleTime, 1.0f);
    bool touchedDownThisFrame = false;

    for (int i = 0; i < 2; ++i) {
        // 0 at this foot's touchdown, `duty` at its takeoff, 1 at the next.
        float local = std::fmod(m_cyclePhase - i * 0.5f + 1.0f, 1.0f);
        bool  stance = local < duty;

        bool touchedDown = local < m_localPhase[i];          // wrapped past 1
        bool tookOff     = stance == false && m_inStance[i];

        if (touchedDown) {
            m_plant[i] = m_swingTo[i];
            touchedDownThisFrame = true;
        }
        if (tookOff) {
            m_swingFrom[i] = m_plant[i];
            m_swingTo[i]   = footTarget(hf, i, swingTime, duty * cycleTime);
        }

        m_localPhase[i] = local;
        m_inStance[i]   = stance;

        if (stance) {
            // A planted foot does not move. That is the whole point, and it is
            // what makes a slope work without a line of slope-specific code.
            m_foot[i] = m_plant[i];
        } else {
            // The swing arc depends only on where the foot left and where it is
            // going, so it can be known BEFORE the hips -- and it has to be, or
            // the hips get bounded against last frame's foot and the clamp below
            // fires on the final frame of the swing. The foot then lands on the
            // unclamped target and drops 0.2 units in one frame.
            float t = (local - duty) / std::max(1e-4f, 1.0f - duty);
            glm::vec3 swinging = m_swingFrom[i] + (m_swingTo[i] - m_swingFrom[i]) * smoothStep(t);
            swinging.y += std::sin(t * kPi) * footLift;
            m_foot[i] = swinging;
        }
    }

    const bool airborne = !m_inStance[0] && !m_inStance[1];

    // ---- hips ---------------------------------------------------------
    //
    // How much of his weight each planted foot is carrying: nothing as it lands,
    // everything through mid-stance, nothing again as it leaves.
    //
    // The first version just picked ONE support foot -- whichever landed most
    // recently -- and read the push, the bob, the sway and the ground height off
    // that one. Every part of that flips in a single frame at the handover: the
    // outgoing foot is at 0.81 of its stance with the push-off near its peak,
    // the incoming one is at 0.00 with no push at all, so the target height fell
    // roughly 0.17 units between one frame and the next. Measured, the worst
    // single frame moved the hips 310 times as far as a typical one.
    //
    // Riding a weighted blend of both instead means the load transfers across
    // double support the way it does in anything that walks, and there is no
    // instant at which the answer changes.
    float weight[2]  = {0.0f, 0.0f};
    float stanceT[2] = {0.0f, 0.0f};
    float totalWeight = 0.0f;

    // The transfer happens over the double-support window -- the part of the
    // cycle where both feet are down -- expressed as a fraction of one stance.
    const float transfer = std::clamp((2.0f * duty - 1.0f) / std::max(1e-4f, duty),
                                      0.10f, 0.5f);

    for (int i = 0; i < 2; ++i) {
        if (!m_inStance[i]) continue;
        stanceT[i] = m_localPhase[i] / std::max(1e-4f, duty);

        float takingUp   = smoothStep(stanceT[i] / transfer);
        float givingBack = smoothStep((1.0f - stanceT[i]) / transfer);

        // Never exactly zero, or a lone stance foot would carry nothing and the
        // normalisation below would have nothing to divide by.
        weight[i] = std::max(1e-3f, takingUp * givingBack);
        totalWeight += weight[i];
    }

    // Sway toward whichever foot is carrying him, by the same weights. Done
    // before the height, because the lean moves the hip joints and the hip
    // joints decide how far the legs have to reach.
    float swayAmount = 0.0f;
    if (totalWeight > 0.0f) {
        for (int i = 0; i < 2; ++i) {
            if (!m_inStance[i]) continue;
            float side = (i == 1) ? 1.0f : -1.0f;
            // Scaled by speed like the push and the bob: the sway is a walk's
            // weight shift, and a creature standing still holding a crate should
            // not be leaning permanently onto one leg -- it moves his shoulders
            // sideways, and his hands with them.
            swayAmount += (weight[i] / totalWeight) * side
                        * params.swayAmount * std::sin(stanceT[i] * kPi)
                        * m_speedScale;
        }
    }
    glm::vec3 sway = right() * swayAmount;

    const glm::vec2 hipXZ = m_pos + glm::vec2(sway.x, sway.z);
    const glm::vec2 rightXZ(right().x, right().z);

    // The reach bound: a leg spanning horizontal distance h can only drop
    // sqrt(L^2 - h^2) before it runs out. Standing at a fixed fraction of leg
    // length ignores that and the solver ends up clamping the ankle, which does
    // not look like a stretch -- it looks like the foot coming off the ground.
    // BOTH feet, not just the planted ones. The swing foot is only ever the
    // binding one at the very start and end of its swing, when it is on the
    // ground and a long way from the body -- which is exactly the moment it
    // changes role. Leave it out and the hips ignore it right up to touchdown,
    // the separate swing-foot clamp yanks the foot inward instead, and the yank
    // reverses in a single frame as the roles swap: measured at 0.34 units of
    // foot travel in one frame, and the hip snap that came with it.
    //
    // Mid-swing it never binds anyway: the foot is lifted and tucked under him,
    // so its bound sits far above whatever the planted leg is asking for.
    // (In flight this is not called at all -- nothing is holding him up.)
    auto reachBound = [&](float height) {
        for (int i = 0; i < 2; ++i) {
            float legSide = (i == 0) ? -1.0f : 1.0f;
            glm::vec2 jointXZ = hipXZ + rightXZ * (legSide * params.hipWidth * 0.5f);
            glm::vec2 footXZ(m_foot[i].x, m_foot[i].z);

            float horizontal = glm::length(footXZ - jointXZ);
            float reach = fullLeg * 0.985f;
            float drop = std::sqrt(std::max(0.0f, reach * reach - horizontal * horizontal));
            height = std::min(height, m_foot[i].y + drop);
        }
        return height;
    };

    if (!airborne && totalWeight > 0.0f) {
        float stand = m_running ? params.runStandFrac : params.standFrac;

        // Bending at the hip alone cannot get a hand near the ground: with the
        // shoulder 1.1 above the hips, a 2.2 arm is a good half unit short. He
        // has to drop his hips as well, which is what anyone picking something
        // up actually does.
        stand = stand + (params.reachStandFrac - stand) * m_squat;

        // Each planted leg proposes a hip height; the hips ride the weighted
        // average. On a slope the two proposals genuinely differ, and blending
        // them is also what carries him smoothly from one step level to the next.
        float desired = 0.0f;
        for (int i = 0; i < 2; ++i) {
            if (!m_inStance[i]) continue;

            // Extension through the back half of stance. The hips travel up away
            // from a foot that is not moving, so the leg straightens because it
            // has to -- the propulsion is a consequence of the trajectory, not a
            // force. Quadratic, not smoothstep: smoothstep's slope is ZERO at
            // its end, so the hips would stop accelerating exactly as he leaves
            // the ground and the launch velocity would come out at nothing.
            // Scaled by how fast he is actually going. Push-off is propulsion,
            // and a creature standing still is not propelling -- left in, it
            // holds the hips high while he is stopped and trying to reach the
            // ground, and placing a crate went from 0.30 units of error to 1.38.
            float pushT = std::clamp((stanceT[i] - 0.5f) / 0.5f, 0.0f, 1.0f);
            float push = params.pushAmount * pushT * pushT * m_speedScale;

            // No bob while running. It is a walking device -- a rise and fall
            // over the support leg -- and at takeoff its sine is on the way
            // DOWN, which subtracted about 1.1 units/s from the launch.
            // Same for the bob: it is the rise and fall of a walk, so it fades
            // out with the walk rather than leaving a standing creature parked
            // at some arbitrary point on its own sine.
            float bob = m_running ? 0.0f
                                  : std::sin(stanceT[i] * kPi)
                                        * params.bobAmount * m_speedScale;

            desired += (weight[i] / totalWeight)
                     * (m_foot[i].y + fullLeg * (stand + push) + bob);
        }

        float grounded = reachBound(desired);

        m_hipVelY = (grounded - m_hipY) / std::max(1e-4f, dt);
        m_hipY = grounded;

        if (m_wasAirborne) m_hipVelY = 0.0f;   // landing absorbs, it does not bounce
        if (touchedDownThisFrame) ++m_steps;
    } else {
        if (!m_wasAirborne) {
            // Takeoff. He leaves with whatever upward velocity the push-off
            // already gave him -- m_hipVelY is the measured rate of climb of the
            // hips through the extension, so the launch IS the extension and
            // nothing has to invent a force.
            //
            // The first version solved for a velocity that would ARRIVE at the
            // next foot's height, which sounds tidier and was badly wrong: at
            // takeoff the hips are often crouched by the reach bound, so the
            // gap to an idealised landing height divided by a 0.1s flight came
            // out enormous and he launched 7 units into the sky.
            m_hipVelY = std::clamp(m_hipVelY, 0.0f, 6.0f);
        }
        m_hipVelY -= params.gravity * dt;
        m_hipY += m_hipVelY * dt;
    }

    m_wasAirborne = airborne;
    m_airborne = airborne;

    // A rolling measure, so the panel can report how much of the time he is
    // actually off the ground rather than how much the duty factor claims.
    m_airFraction += ((airborne ? 1.0f : 0.0f) - m_airFraction) * std::min(1.0f, dt * 2.0f);

    m_hipCentre = glm::vec3(hipXZ.x, m_hipY, hipXZ.y);

    // A last safety on the swing legs: nothing may span further than the leg is
    // long. With the hips now bounded against both feet this should almost never
    // fire -- it is here for the cases the bound cannot satisfy, like a foot
    // stranded out to one side by a hard turn.
    const float maxSpan = fullLeg * 0.985f;
    for (int i = 0; i < 2; ++i) {
        if (m_inStance[i]) continue;

        glm::vec3 joint = hipJoint(i);
        glm::vec3 offset = m_foot[i] - joint;
        float span = glm::length(offset);
        if (span > maxSpan) m_foot[i] = joint + offset * (maxSpan / span);
    }

    // Last, because everything above the hips hangs off them -- and the lean
    // has to be settled before the shoulders and head are placed.
    updateTorso(speed, dt);
    updateAnkles(hf, dt);
    updateHandGoals(hf);
    updateHead(observer, dt);
}

} // namespace tessara
