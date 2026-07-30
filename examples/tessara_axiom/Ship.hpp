#pragma once

#include "Ground.hpp"
#include "TerrainSource.hpp"
#include "SceneVertex.hpp"

#include <cstdint>
#include <vector>

namespace tessara {

// A landed cargo hauler: a bay to put crates in, a bridge to fly it from, and a
// ramp at the back that opens.
//
// Everything is in the ship's own frame -- origin on the ground under its
// centre, +Z out the nose -- and placed once. The only thing that moves is the
// ramp, so that is the only thing that needs animating.
class Ship {
public:
    struct Params {
        // Sized from the creature, not by eye. He measures 4.90 from sole to
        // crown and about 1.8 across the shoulders; the first pass gave him a
        // 4.40 bay, so he had HALF A UNIT OF HEAD too much and simply could not
        // get in -- he walked to the door and timed out. A hold wants headroom
        // enough to carry something through, and width enough for two.
        float length     = 38.0f;
        float width      = 17.0f;
        float hullHeight = 8.6f;

        float deckHeight = 2.2f;    // cargo floor above the ground
        float bayWidth   = 11.0f;   // the doorway
        float bayHeight  = 6.6f;

        // The ramp is exactly as long as the doorway is tall, so shut it seals
        // the opening instead of standing up out of the hull like a fin. That
        // fixes its length, which in turn fixes how shallow it can lie: the open
        // angle is whatever reaches the ground, not a number chosen separately.
        float rampSeconds  = 2.4f;  // full open or close

        // A second section that slides out of the first.
        //
        // This is the answer to a ramp that stops short, and it is the only answer
        // that does not make it unwalkable. Reach comes from length x sin(angle),
        // and the ANGLE cannot grow -- past 24 degrees the biped's router will not
        // cross it and he simply stops delivering. So the length grows instead: at
        // the nominal 21 degrees, eight more units of ramp is nearly three more
        // units of drop, at exactly the same grade underfoot.
        float rampExtend   = 8.0f;
        float rampExtendSeconds = 1.8f;

        // The steepest the ramp will lay itself, whatever the drop behind the ship.
        //
        // Bounded by the BIPED'S ROUTER, not by taste: he plans over the lattice
        // allowing params.stepUp of rise per two-unit node, so a ramp steeper than
        // atan(stepUp / 2.0) is one no route will ever cross and he simply stops
        // delivering. That limit was 24 degrees while his step-up was 0.90, and the
        // ramp's nominal angle is 21 -- which is why this could not exist before.
        // At 1.5 it is 37, so 33 leaves the margin and buys twelve degrees of reach.
        float rampMaxDegrees = 33.0f;

        float controlRise  = 2.3f;  // the button, at about chest height on a unit

        // How far anything meant to touch the ground actually sinks into it.
        //
        // Resting a face exactly ON the terrain is the one thing guaranteed to
        // z-fight, and the landing pad being perfectly level makes it certain
        // rather than likely -- two coplanar surfaces, no depth between them to
        // resolve. Burying the contact slightly means the faces intersect rather
        // than coincide, and there is always a winner.
        float groundBite   = 0.14f;

        // How far a landing leg will reach down past the hull's own resting
        // plane. The hull sits on the HIGHEST ground under it -- it has to, or the
        // uphill end is buried -- so on any slope the downhill legs have to make
        // up the difference or hang in the air, which is what they were doing.
        //
        // Bounded because a leg is a strut, not a rope: past this the site is too
        // steep to stand on and the ship rests on whichever legs reach.
        float legTravel    = 3.2f;


    };

    Params params;

    // Finds the flattest ground near `near`, LEVELS it, and sets down.
    void place(TerrainSource& hf, glm::vec2 near, float yawDegrees);

    float openAngleDegrees() const { return m_openAngle; }

    // ---- the ramp extension ------------------------------------------------
    // How far the second section is out, 0..1, and how long the ramp therefore is.
    //
    // rampSpan() is the one number everything else already asked for: the patch you
    // walk on, the kerbs down its sides, the mesh and the spot creatures wait at are
    // all derived from rampFootPosition(), which is derived from this. So extending
    // the ramp extends all of them without any of them being told.
    float rampSpan() const { return m_rampLength + params.rampExtend * m_rampExt; }
    float rampExtension() const { return m_rampExt; }

    // Roll it out, or put it away. -1 means "as far as the ground needs", which is
    // what solveRampExtension worked out on touchdown.
    void setRampExtension(float want) { m_rampExtWant = want; }
    void autoRampExtension() { m_rampExtWant = m_rampExtAuto; }
    float neededRampExtension() const { return m_rampExtAuto; }

    // How much of it the ground actually calls for, swept rather than solved --
    // see the note in solveRampAngle about what iterating against real terrain did.
    void solveRampExtension(const TerrainSource& ground);

    // `obstructed` is whether anything is standing on the ramp. A ramp that shuts
    // regardless scoops whoever is on it: measured at two and a half units of lift
    // and seventeen units of travel in a second and a half, none of it walked.
    // Held rather than reversed, because a ramp that undoes its own closing the
    // moment somebody steps on it can never be shut at all while anyone is about.
    void update(float dt, bool obstructed = false);

    // Worked from where the ramp IS, not from where it was last told to go.
    //
    // This used to flip a bool, and the bool is not only yours: the interlock
    // sets it to opening whenever something stands on a closing ramp, and the
    // biped sets it when he wants in. So a press could arrive with the intent
    // already inverted and do the exact opposite of what the ramp plainly needed
    // -- press to open a shut ramp and shut it again, while the walker went on
    // waiting for it, correctly and apparently forever. Deciding from m_ramp
    // means the button always does the thing you can see it needs.
    void toggleRamp() { m_opening = (m_ramp < 0.5f); }

    // Said outright, for anything that wants one of the two rather than the other.
    void openRamp()  { m_opening = true; }
    void closeRamp() { m_opening = false; }

    // True while it wants to close and cannot, so the fact can be shown rather
    // than merely happening.
    bool rampHeld() const { return m_rampHeld; }

    // Is this standing ON the ramp? Not merely over its footprint -- somebody on
    // the deck above its top end is not on the ramp, and somebody on the ground
    // beside its foot is not either.
    bool isOnRamp(const glm::vec3& p) const;

    bool  isOpening() const { return m_opening; }
    bool  rampSettled() const { return m_ramp <= 0.0f || m_ramp >= 1.0f; }
    float rampProgress() const { return m_ramp; }   // 0 shut, 1 down

    // ---- frame -------------------------------------------------------------
    glm::vec3 origin() const { return m_origin; }       // on the ground, ship centre

    // Degrees, counted so that forward() is (sin, ., cos). Worth saying out loud
    // because eden::Camera counts the other way and the two have to be reconciled
    // wherever somebody stands at the helm.
    float yawDegrees() const { return m_yaw; }
    glm::vec3 forward() const;
    glm::vec3 right() const;
    static glm::vec3 up() { return glm::vec3(0, 1, 0); }

    // Where the ramp control sits. There are two: one on the hull outside, and
    // one on the bay wall.
    //
    // A hold you can be shut into wants a way to shut yourself into it, and a
    // way back out. With only the outer panel the politics of the door were
    // lopsided -- anything shut OUT could let itself in, and anything shut IN
    // could do nothing but wait for someone else.
    glm::vec3 controlPosition() const;
    glm::vec3 innerControlPosition() const;

    // The far end of the ramp, so the scene can tell how far it has swung.
    glm::vec3 rampFootPosition() const;

    // ---- the boarding ladder -----------------------------------------------
    // On the hull by the BRIDGE, deliberately nowhere near the ramp.
    //
    // It exists because of a landing site that passes every test and then strands
    // you: flat enough on top for the ship, but a cliff at the back, so the ramp
    // opens onto air. You get down somehow, and then you cannot get back to the
    // ship you need in order to fly it somewhere better.
    //
    // Put at the far end from the ramp on purpose. It is a way back aboard, not a
    // second door -- walk the length of the hull to use it.
    glm::vec3 ladderFoot() const;    // at the ground, outside the hull
    float ladderTopY() const { return m_origin.y + params.deckHeight; }

    // ---- landing gear ------------------------------------------------------
    // Four legs, each as long as the ground under IT requires.
    //
    // The hull does not tilt, and that is a decision about collision rather than
    // taste -- see the note on flight. Every Blocker in this hull is an upright
    // box with a floor and a ceiling, the deck is a level SurfacePatch, and the
    // walker stands on a world-aligned lattice; pitching the whole ship would
    // invalidate all three at once and the first sign of getting it wrong is
    // somebody falling out through the side.
    //
    // So the gear compensates instead, which is what gear is for. Level hull,
    // four feet on the dirt, and a ramp that reaches -- and a deck that stays
    // walkable and crates that stay where they were put.
    static constexpr int kLegs = 4;
    glm::vec3 legBase(int i) const;                  // foot, at the hull's plane
    float legDrop(int i) const { return m_legDrop[i]; }   // how far it reached down
    glm::vec3 legFoot(int i) const { return legBase(i) - up() * m_legDrop[i]; }

    // Longest and shortest leg, so a site too steep to stand on can be said out
    // loud rather than merely looking wrong.
    float legSpread() const;

    // How far the ramp's tip finished from the ground under it: 0 is resting,
    // positive is hanging in the air. Asked so a site the hatch cannot reach can
    // be said out loud instead of merely looking wrong.
    float rampGap(const TerrainSource& ground) const;

    // How much the ground varies under the four feet, and whether the gear can
    // absorb it.
    //
    // A hull thirty-eight units long on a fifteen degree slope has ten units of
    // drop from nose to tail, and no landing leg is ten units long. So there is a
    // real limit here, and it is worth being able to ASK about rather than
    // discovering it by looking at a ship with daylight under one end. Told to the
    // pilot before committing, this is the difference between a bad landing SITE
    // and a bad landing.
    float siteDrop(const TerrainSource& ground) const;
    bool  standsLevel(const TerrainSource& ground) const {
        return siteDrop(ground) <= params.legTravel;
    }

    // What can be stood on. Published rather than acted on: the ship does not
    // know who walks into it, and nothing that walks knows it is a ship.
    SurfacePatch deckPatch() const;
    SurfacePatch rampPatch() const;

    // What cannot be walked through. Note what is NOT in here: there is no
    // "doorway" blocker with a hole in it, and nothing names the bay. The way in
    // is simply the strip of ground between the two hull walls that no box
    // covers, which is the same trick as the patches -- the ramp is the way up
    // because it is the only surface that reaches, not because it is labelled.
    void appendBlockers(std::vector<Blocker>& out) const;

    // The hold, as a room with one door -- so anything walking between the bay
    // and the field routes through the ramp instead of at the nearest wall.
    // Reports itself shut whenever the ramp is not down far enough to walk on.
    Enclosure enclosure() const;

    // ---- flight ------------------------------------------------------------
    // Hover only: heading and height, and no bank.
    //
    // That is a decision about COLLISION, not about taste. A SurfacePatch can
    // tilt -- the ramp is one -- so a rolling deck would be expressible. A
    // Blocker cannot: it is an upright box with a floor and a ceiling, and the
    // note on it says why. Banking would turn all sixteen of this hull's walls
    // into oriented boxes needing a real narrow-phase, and the first sign of
    // getting it wrong is somebody falling out through the side.
    struct Flight {
        float speed     = 26.0f;   // units per second, full ahead
        float turnRate  = 42.0f;   // degrees per second
        float climbRate = 14.0f;
        float clearance = 7.0f;    // never closer than this to the ground
        float ceiling   = 260.0f;  // nor further from it

        // ---- coming down -------------------------------------------------
        // The clearance above is what makes this a hover: it is a floor the ship
        // is simply not allowed through. Landing is that floor being let go of,
        // and once it is gone the only thing holding a hundred tons up is the
        // lift the pilot is asking for. So the descent is not a scripted
        // animation -- it is a fall being flown, and it can be flown badly.
        float gravity   = 11.0f;   // units per second per second, unlifted
        float thrust    = 30.0f;   // what full lift answers it with
        float maxDrop   = 26.0f;   // terminal descent; airframes have one
        float hardAt    = 7.0f;    // touchdown faster than this is a bad one
    };
    Flight flight;

    // Let go of the hover floor and start falling. Flown down from here: lift
    // still works, and is the only thing that stops it arriving hard.
    void beginLanding() { m_landing = true; }
    void abortLanding() { m_landing = false; }
    bool landing() const { return m_landing; }

    // How fast it was going down when the gear met the ground, and whether that
    // counts as a landing or an arrival. Both stay readable after touchdown so
    // whoever was flying gets told.
    float touchdownSpeed() const { return m_touchdownSpeed; }
    bool  landedHard() const { return m_touchdownSpeed > flight.hardAt; }
    float verticalSpeed() const { return m_vy; }

    // Controls are -1..1. `ground` is asked how high the world is here, because a
    // surface aircraft that will not fly into a hill is most of what makes one
    // pleasant, and it costs one height query.
    void fly(float dt, float forward, float turn, float lift, const TerrainSource& ground);

    // Put it on the ground where it stands, at once, and report the displacement
    // so everything aboard can be moved by the same amount. The blunt instrument
    // beside beginLanding(): no descent, no way to do it badly, and nothing to fly.
    glm::vec3 settle(const TerrainSource& ground);

    // How high the world is under the hull -- the highest of several samples along
    // it, not the one under the middle. Public because a landing pad, a shadow and
    // a HUD all want the same number.
    float groundUnderHull(const TerrainSource& ground) const;

    // What the last fly() actually did. Everything standing in the hold has to be
    // moved by exactly this, which is the whole of carrying passengers.
    glm::vec3 lastMove() const { return m_lastMove; }
    float     lastTurn() const { return m_lastTurn; }

    bool  airborne() const { return m_airborne; }
    void  setAirborne(bool on) {
        m_airborne = on;
        m_landing = false;
        m_vy = 0.0f;
        if (on) m_touchdownSpeed = 0.0f;   // last arrival stays readable after it
    }
    float heightAboveGround(const TerrainSource& ground) const {
        return m_origin.y - ground.heightAtWorld(m_origin.x, m_origin.z);
    }

    // ---- the bridge door ---------------------------------------------------
    // Two panels sliding apart in the bulkhead. Opens for whoever is standing at
    // it and shuts behind them, which is what an interior door does -- there is
    // no button because nobody wants one on a doorway they walk through twenty
    // times an hour.
    void updateBridgeDoor(float dt, bool somebodyNear);
    float bridgeDoorProgress() const { return m_bridgeDoor; }   // 0 shut, 1 open
    glm::vec3 bridgeDoorCentre() const;

    // ---- the helm ----------------------------------------------------------
    // Where the ship is flown from, and where the captain stands to fly it.
    //
    // At DECK level in the nose rather than in the raised cabin on the roof --
    // that cabin reads as a bridge from outside and is six units above anyone's
    // head, with nothing to climb. A helm you cannot reach is scenery.
    glm::vec3 helmPosition() const;   // the console itself
    glm::vec3 helmStation() const;    // where a body stands to work it

    // ---- muster stations ---------------------------------------------------
    // Numbered places in the hold where a unit stands for launch.
    //
    // Numbered rather than "somewhere in the bay" because a crew of five has to
    // line up the same way every time, and because a station a unit can be SENT
    // to is a thing the scene can check has been reached. "Near the ship" is not
    // a state anything can be sure of.
    //
    // Laid out in pairs down the bay, port and starboard, working forward from
    // behind the pile so nobody stands on the cargo.
    int       stationCount() const { return 6; }
    glm::vec3 stationPosition(int index) const;

    // Where cargo goes, and the spot on the ground you walk to before you can
    // walk UP. Published for the same reason as the patches.
    glm::vec3 bayStoragePoint() const;
    glm::vec3 rampApproachPoint() const;
    bool      isAboard(const glm::vec3& p) const;

private:
    glm::vec3 m_origin{0.0f};
    float m_yaw = 0.0f;
    float m_ramp = 0.0f;       // 0 shut, 1 down
    bool  m_opening = false;
    bool  m_rampHeld = false;
    float m_bridgeDoor = 0.0f;   // 0 shut, 1 open
    void solveRampAngle(const TerrainSource& ground);
    void solveLegs(const TerrainSource& ground);

    bool  m_airborne = false;
    bool  m_landing  = false;
    float m_legDrop[kLegs] = {0.0f, 0.0f, 0.0f, 0.0f};
    float m_vy = 0.0f;               // vertical speed, only while coming down
    float m_touchdownSpeed = 0.0f;   // how fast it was falling when it arrived
    glm::vec3 m_lastMove{0.0f};
    float m_lastTurn = 0.0f;
    float m_rampLength = 4.4f; // the doorway height
    float m_rampExt     = 0.0f;   // 0 stowed, 1 fully out
    float m_rampExtWant = 0.0f;
    float m_rampExtAuto = 0.0f;   // what the ground under the tip asks for
    float m_openAngle  = 27.0f; // worked out at placement, from the two of them
};

void appendShipMesh(const Ship& ship,
                    std::vector<SceneVertex>& outVertices,
                    std::vector<uint32_t>& outIndices);

// The windows, kept OUT of the mesh above and drawn separately.
//
// Everything in this example goes through one pipeline, and alpha lives in the
// per-draw tint rather than per-vertex -- SceneVertex has no alpha channel. So a
// pane mixed in with the hull is drawn at the hull's opacity, which is to say
// none, and a "window" you cannot see through is a wall painted blue. Split out,
// the glass can be drawn last, translucent, through the pipeline variant that
// already exists for the creature's see-through panels.
void appendShipGlass(const Ship& ship,
                     std::vector<SceneVertex>& outVertices,
                     std::vector<uint32_t>& outIndices);

} // namespace tessara
