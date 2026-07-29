#pragma once

#include "Ground.hpp"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace tessara {

// A two-legged creature that wanders the heightfield.
//
// Deliberately NOT built the way the walker is. The walker stands on lattice
// nodes and moves by graph steps, which is what lets a colony of them be cheap.
// A biped cannot do that: two feet cannot hold a body up without the hips moving
// continuously between them, so this one lives in continuous space and only
// touches the heightfield to ask how high the ground is and how steep it gets.
//
// The legs are solved, not animated. Each foot is PLANTED at a world position
// and stays there while the hips travel over it; the knee is whatever angle
// makes the leg reach. That is why the walk holds up on a slope without a single
// slope-specific line -- the foot is on the ground because it was put there, and
// the knee bends however much it has to.
class Biped {
public:
    // What he is doing. The whole fetch is one small state machine over the
    // parts that already existed -- the hand goals were always a target the arms
    // solve toward, so reaching for something real needed no new kinematics at
    // all, only somewhere else to point them.
    enum class Activity { Wander, Approach, Reach, Lift, Carry, Place };

    struct Params {
        float walkSpeed   = 5.0f;    // world units per second
        float stepTime    = 0.42f;   // seconds for one foot's swing
        float strideScale = 1.25f;   // how far ahead of the body a foot is planted
        float hipWidth    = 0.95f;
        float thigh       = 1.55f;
        float shin        = 1.55f;
        float footLift    = 0.55f;

        // Hip height as a fraction of full leg extension. Must stay below 1 or
        // the legs lock straight and the knees stop reading.
        float standFrac   = 0.90f;

        // How much further the support leg extends through the back half of
        // stance. This is the propulsion: the hips travel up and away from a
        // foot that is not moving, so the leg has no choice but to straighten,
        // and the straightening IS the push. Nothing simulates a force.
        float pushAmount  = 0.095f;   // of full leg length

        // Running stands LOWER than walking, which is what gives the extension
        // somewhere to go. At the walk's height the leg is already near enough
        // straight that pushing off does nothing -- the reach bound cancels it
        // and the takeoff velocity comes out at zero.
        float runStandFrac = 0.76f;

        float maxSlopeDeg = 38.0f;
        float turnRate    = 130.0f;  // degrees per second he can swing his heading

        // Turning on the spot is slower, because the feet have to keep up. At
        // the walking rate the hips outrun them and the legs wind up: measured
        // at 165 degrees of twist between the hip line and the foot line for a
        // 90-degree turn.
        float holdTurnRate = 45.0f;

        // How far out of square he will let himself get before bothering to
        // pick his feet up and re-plant them under himself.
        float turnStepDeg  = 6.0f;

        // Cadence while turning on the spot. Quicker than walking, because the
        // steps are tiny and a shuffle is what this actually looks like.
        float turnStepRate = 1.4f;
        float wanderRate  = 70.0f;   // degrees per second of random heading drift
        float bobAmount   = 0.11f;
        float swayAmount  = 0.13f;

        // ---- torso and arms ----
        // The first pass had a 0.98 arm against a 3.1 leg and the hands came out
        // where the elbows should have been. Roughly 0.8 of leg length is the
        // proportion that reads, which is about double what it was.
        float torsoRise         = 0.78f;   // hips to chest centre
        float shoulderRise      = 0.34f;   // chest centre to shoulder
        float shoulderWidthFrac = 0.72f;   // of hip width
        float upperArm          = 1.16f;
        float forearm           = 1.04f;

        // Hand distance from the shoulder as a fraction of full arm reach. Below
        // 1 or the arm locks straight and the elbow stops reading, exactly as
        // with standFrac on the legs.
        float armExtend         = 0.88f;

        float armSwing    = 34.0f;   // degrees

        // ---- head and looking ----
        float headRise = 0.70f;      // chest centre to head centre

        // Half-angle of his field of view. He notices things inside this cone
        // and only this cone -- behind him you do not exist.
        float fovDegrees = 75.0f;
        float lookRange  = 70.0f;

        // How far the neck itself can turn. Deliberately SMALLER than the field
        // of view, so something at the edge of vision is watched out of the
        // corner of the eye rather than by snapping the head round to it.
        float neckYawLimit   = 62.0f;
        float neckPitchLimit = 30.0f;

        float headTurnRate = 8.0f;   // higher is snappier

        // ---- ankles ----
        // The foot pitches and rolls to sit flat on whatever is under it. This
        // is the one piece of ground-adaptation in the whole creature that is
        // NOT free -- the legs solve for slopes on their own, but the foot at
        // the end of the leg has no reason to know which way the hill runs.
        float ankleLimitDeg = 38.0f;   // how far the servo can tilt from level
        float ankleRate     = 14.0f;   // how quickly it gets there

        // ---- torso lean ----
        // Everything above the hips pitches forward as he picks up speed. Needed
        // for the run, but the walk gets a little of it too -- a body travelling
        // at all is a body leaning slightly into it.
        float leanPerSpeed = 1.0f;   // degrees of lean per unit of speed
        float maxLean      = 20.0f;
        float leanRate     = 4.0f;   // how quickly the lean catches up

        // Counter-rotation. The legs throw angular momentum about the vertical
        // axis every stride and something has to absorb it; in anything that
        // walks, the shoulder girdle twists against the hips and the arms ride
        // that twist. Real walking robots do it for the same reason -- it is
        // momentum cancellation, not decoration, which is why it belongs on a
        // machine and does not read as organic.
        //
        // As a ratio of the arm swing, so it grows into the run automatically:
        // 0.22 gives about 7 degrees walking and 14 running.
        //
        // POSITIVE puts each shoulder forward with its own arm -- the shoulders
        // counter-rotating against the HIPS, which is what a body actually does.
        // Negative gives the other reading: shoulders opposing the arms. It is
        // one drag of the slider to see both.
        float torsoTwistRatio = 0.22f;

        // ---- fetching ----
        // Measured, not guessed. At 1.75 his hands could not quite make it and
        // most placements ended in the six-second give-up instead of a grasp:
        // worst error 0.51 against a 0.30 grasp threshold, and runs took 162s.
        // At 1.45 the hands actually arrive (worst 0.296) and the same haul
        // takes 65s.
        float reachDistance  = 1.45f;   // how close he stops to the crate
        float graspDistance  = 0.30f;   // hand this near its target counts as contact
        float placeDistance  = 1.45f;
        float bendAngle      = 54.0f;   // hip bend while reaching down
        float reachStandFrac = 0.56f;   // and how far he drops his hips to do it
        float bendRate       = 3.2f;
        float carryAhead     = 0.62f;   // crate held this far in front of the chest
        float carryRise      = 0.10f;

        // ---- running ----
        // Duty factor is THE thing that separates a walk from a run: the
        // fraction of each foot's cycle spent on the ground. Above 0.5 the two
        // stances overlap and there is always a foot down. Below it they leave a
        // gap where neither foot is down, and that gap is the flight phase. The
        // run is not a mode bolted on -- it is what this number crossing 0.5
        // does.
        float walkDuty = 0.62f;
        float runDuty  = 0.38f;

        // Break into a run above this, drop back below it (with hysteresis, or
        // he flickers between gaits at the threshold).
        float runSpeed = 7.0f;

        float gravity  = 26.0f;   // pulls the hips down through the flight phase

        // Running changes the arms in kind, not degree: elbows much tighter and
        // driving fore-and-aft rather than hanging.
        float runArmSwing  = 62.0f;
        float runArmExtend = 0.62f;
        float runFootLift  = 1.15f;

        // How high a LEDGE he will step onto -- a kerb, the lip at the foot of
        // the ramp. Deliberately a separate number from maxSlopeDeg, which is
        // how steep a SLOPE he will walk up, because they are different
        // questions and answering both with one number is what let him climb the
        // side of the ramp.
        //
        // A slope rises gradually over the distance he covers, so the limit that
        // governs it has to scale with distance. A ledge is the whole height at
        // once however far away he started, so its limit must not. Hold them
        // apart and the foot of the ramp becomes the only point along its length
        // where the surface is within stepping height of the ground -- so he
        // walks round to it, without anything telling him to.
        float stepUp = 0.90f;

        // ---- how much room he needs -------------------------------------
        // The column he occupies, for the sake of anything solid. Wider than his
        // hips and narrower than his arm span: shoulders 1.8 across would have
        // him refusing doorways he plainly fits through, and hips 0.95 lets a
        // wall pass between his shoulder and his ear.
        float bodyRadius = 0.80f;
    };

    Params params;

    // Sole to crown when he is stood up straight, worked out from the same
    // numbers the skeleton is. Not measured off the live pose on purpose: a
    // squatting creature would shrink his own collision, and the doorway he
    // could not walk through standing would open the moment he bent to pick
    // something up.
    float standHeight() const;

    // Where his soles are. Not derivable from the hips without getting it wrong
    // by a third of a unit -- see the note on the definition.
    float soleHeight() const;

    void reset(const Ground& hf, glm::vec2 position, float headingDeg, uint32_t seed);

    // `observer` is somebody worth looking at, or null for nobody. He never
    // changes course for it -- he keeps walking wherever he was going and just
    // turns his head, which is what makes it read as noticing you rather than
    // as reacting to you.
    void update(const Ground& hf, float dt, const glm::vec3* observer = nullptr);

    // ---- frame ------------------------------------------------------------
    float     yawDegrees() const { return m_yaw; }
    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::vec3 hipCentre() const { return m_hipCentre; }

    // The torso's own frame: the body's, pitched forward about the right axis.
    // Everything above the hips hangs off this rather than off world up, which
    // is what makes the lean move the whole upper body as one piece.
    float     torsoPitch() const { return m_torsoPitch; }
    glm::vec3 torsoUp() const;
    glm::vec3 torsoForward() const;
    glm::vec3 torsoRight() const;
    float     torsoYaw() const { return m_torsoYaw; }

    // ---- joints -----------------------------------------------------------
    // i = 0 is the left leg, 1 the right.
    glm::vec3 footWorld(int i) const { return m_foot[i]; }
    glm::vec3 hipJoint(int i) const;
    void      solveLeg(int i, glm::vec3& outKnee, glm::vec3& outAnkle) const;

    // Arms are solved by the same two-bone chain as the legs. They have no goal
    // on the ground, so the hand is swung on an arc from the shoulder and the
    // elbow is whatever that implies -- which is still worth solving rather than
    // posing, because it keeps the two bone lengths honest and it is the same
    // code path, so an arm can be handed a real goal later (a grab, a rail, a
    // carried object) without any of this changing.
    glm::vec3 chestCentre() const;
    glm::vec3 shoulderJoint(int i) const;
    void      solveArm(int i, glm::vec3& outElbow, glm::vec3& outHand) const;
    glm::vec3 handGoal(int i) const { return m_handGoal[i]; }

    // ---- head -------------------------------------------------------------
    // Its own frame, so the head can be turned without the body knowing.
    glm::vec3 headCentre() const;
    glm::vec3 headForward() const;
    glm::vec3 headRight() const;
    glm::vec3 headUp() const;

    // The foot's own frame, tilted to match the ground beneath it.
    glm::vec3 footUp(int i) const { return m_footUp[i]; }
    glm::vec3 footForward(int i) const;
    glm::vec3 footRight(int i) const;

    bool  isWatching() const { return m_watching; }
    float headYaw() const { return m_headYaw; }
    float headPitch() const { return m_headPitch; }

    // Where in the two-step cycle he is, 0..1. Drives the arm swing.
    float gaitPhase() const;
    bool  isSwinging(int i) const { return !m_inStance[i]; }

    // ---- what he is doing -------------------------------------------------
    // ---- fetching ---------------------------------------------------------
    // Just the two ends of the job. Where the door is, which side of it he is on,
    // and whether it is open are asked of the Ground as he goes -- see aimAt().
    void assignFetch(const Ground& hf, const glm::vec3& crate, const glm::vec3& storage);

    // He is stood at a way through that is closed, holding a job on the far side
    // of it. Not an error and not something he resolves: somebody has to open it.
    bool wayShut() const { return m_wayShut; }
    void abandonTask();

    // Stand still and face whoever is watching, so the thing can be looked at.
    // Not a pause: the head still tracks, the ankles still settle, the arms
    // still hang. Freezing everything shows you a statue rather than the
    // creature standing there.
    void setHold(bool on) { m_holding = on; }
    bool holding() const { return m_holding; }

    Activity  activity() const { return m_activity; }
    bool      hasCargo() const { return m_carrying; }
    bool      hasTask()  const { return m_hasTask; }
    glm::vec3 cargoPosition() const;
    const char* activityName() const;

    bool  isRunning() const { return m_running; }
    bool  isAirborne() const { return m_airborne; }
    float dutyFactor() const { return m_running ? params.runDuty : params.walkDuty; }
    float airTimeFraction() const { return m_airFraction; }

    bool  refusedLastProbe() const { return m_refused; }
    int   refusals() const { return m_refusalCount; }
    int   steps() const { return m_steps; }

    // Two-bone IK, ported from LIME's solveTwoBone (ModelingMode_Rigging.cpp).
    // Handles the two cases that bite: a goal further away than the leg is long
    // (the ankle is clamped to the reachable point rather than the leg snapping)
    // and a pole vector parallel to the limb (falls back to up, then to X).
    static void solveTwoBone(const glm::vec3& root, const glm::vec3& goal,
                             float upperLength, float lowerLength,
                             const glm::vec3& poleHint,
                             glm::vec3& outJoint, glm::vec3& outEnd);

private:
    void  steer(const Ground& hf, float dt);
    glm::vec3 footTarget(const Ground& hf, int i, float swingTime, float stanceTime) const;
    void  updateHandGoals(const Ground& hf);
    void  updateHead(const glm::vec3* observer, float dt);
    void  updateAnkles(const Ground& hf, float dt);
    void  updateTask(const Ground& hf, float dt);
    glm::vec3 graspPoint(int i, const glm::vec3& object) const;
    void  updateTorso(float speed, float dt);
    float supportHipHeight(const Ground& hf, float& outDesired) const;
    bool  passable(const Ground& hf, glm::vec2 from, float headingDeg, float distance) const;
    bool  aimAt(const Ground& hf, const glm::vec3& target);
    void  steerAlongRoute(const Ground& hf, const glm::vec3& to);
    float randomSigned();

    glm::vec2 m_pos{0.0f};        // world x, z
    float m_yaw        = 0.0f;    // degrees
    float m_desiredYaw = 0.0f;

    // The gait is one cycle of TWO steps, and each foot's stance is an interval
    // within it. Foot i lands at cycle phase i*0.5 and leaves at i*0.5 + duty.
    // Whether the two intervals overlap or leave a gap is the whole difference
    // between walking and running.
    glm::vec3 m_foot[2]{};        // where each foot is right now
    glm::vec3 m_plant[2]{};       // where it is planted, while it is planted
    glm::vec3 m_swingFrom[2]{};
    glm::vec3 m_swingTo[2]{};
    bool  m_inStance[2]{true, true};
    float m_localPhase[2]{0.0f, 0.5f};   // 0 at this foot's touchdown
    float m_cyclePhase = 0.0f;

    glm::vec3 m_hipCentre{0.0f};
    glm::vec3 m_handGoal[2]{};   // where each hand is swinging to, computed in update()

    glm::vec3 m_footUp[2]{{0,1,0},{0,1,0}};

    float m_torsoPitch = 0.0f;   // degrees of forward lean
    float m_torsoYaw   = 0.0f;   // degrees of twist about the spine

    float m_headYaw   = 0.0f;    // degrees, relative to the body's facing
    float m_headPitch = 0.0f;
    bool  m_watching  = false;

    Activity  m_activity = Activity::Wander;
    bool      m_hasTask  = false;
    bool      m_carrying = false;
    glm::vec3 m_crate{0.0f};
    glm::vec3 m_storage{0.0f};
    glm::vec3 m_handTarget[2]{};
    bool      m_handOverride = false;
    float     m_bendTarget = 0.0f;   // degrees of hip bend the task is asking for
    float     m_squat = 0.0f;        // 0..1 toward reachStandFrac
    float     m_speedScale = 1.0f;
    float     m_taskTimer = 0.0f;

    bool      m_wayShut = false;

    // The way to wherever he is going, as world points a couple of units apart.
    // Steering handles the gap between two of them; it was never going to handle
    // the gap between him and the far side of a hill.
    std::vector<glm::vec3> m_route;
    size_t    m_routeIndex = 0;
    glm::vec3 m_routedTo{0.0f};
    bool      m_replan = true;
    bool      m_routeFailed = false;

    glm::vec2 m_goal{0.0f};
    bool      m_goalActive = false;
    bool      m_holding = false;
    glm::vec3 m_observer{0.0f};
    bool      m_hasObserver = false;

    bool  m_running   = false;
    bool  m_airborne  = false;
    bool  m_wasAirborne = false;
    float m_hipY      = 0.0f;    // integrated through flight, derived on the ground
    float m_hipVelY   = 0.0f;
    float m_airFraction = 0.0f;  // rolling measure of how much of the time he is off the ground

    bool m_refused = false;
    int  m_refusalCount = 0;
    int  m_steps = 0;

    uint32_t m_rngState = 0x2545F491u;
};

} // namespace tessara
