#include "AIPath.hpp"
#include <algorithm>

namespace eden {

AIPath::AIPath(uint32_t id, const std::string& name)
    : m_id(id)
    , m_name(name.empty() ? "Path_" + std::to_string(id) : name)
{
}

void AIPath::addWaypoint(const glm::vec3& position) {
    m_waypoints.push_back(position);
}

void AIPath::insertWaypoint(size_t index, const glm::vec3& position) {
    if (index <= m_waypoints.size()) {
        m_waypoints.insert(m_waypoints.begin() + index, position);
    }
}

void AIPath::removeWaypoint(size_t index) {
    if (index < m_waypoints.size()) {
        m_waypoints.erase(m_waypoints.begin() + index);
    }
}

void AIPath::setWaypoint(size_t index, const glm::vec3& position) {
    if (index < m_waypoints.size()) {
        m_waypoints[index] = position;
    }
}

void AIPath::clearWaypoints() {
    m_waypoints.clear();
}

glm::vec3 AIPath::getWaypoint(size_t index) const {
    if (index < m_waypoints.size()) {
        return m_waypoints[index];
    }
    return glm::vec3(0);
}

float AIPath::getTotalLength() const {
    if (m_waypoints.size() < 2) return 0.0f;

    float length = 0.0f;
    for (size_t i = 1; i < m_waypoints.size(); i++) {
        length += glm::length(m_waypoints[i] - m_waypoints[i - 1]);
    }

    // Add loop closure if looping
    if (m_loop && m_waypoints.size() > 2) {
        length += glm::length(m_waypoints.front() - m_waypoints.back());
    }

    return length;
}

glm::vec3 AIPath::getPositionAtT(float t) const {
    if (m_waypoints.empty()) return glm::vec3(0);
    if (m_waypoints.size() == 1) return m_waypoints[0];

    t = glm::clamp(t, 0.0f, 1.0f);

    float totalLength = getTotalLength();
    if (totalLength <= 0.0f) return m_waypoints[0];

    float targetDist = t * totalLength;
    float accumulatedDist = 0.0f;

    size_t numSegments = m_loop ? m_waypoints.size() : m_waypoints.size() - 1;

    for (size_t i = 0; i < numSegments; i++) {
        size_t nextIdx = (i + 1) % m_waypoints.size();
        glm::vec3 segStart = m_waypoints[i];
        glm::vec3 segEnd = m_waypoints[nextIdx];
        float segLength = glm::length(segEnd - segStart);

        if (accumulatedDist + segLength >= targetDist) {
            // Target is on this segment
            float segT = (targetDist - accumulatedDist) / segLength;
            return glm::mix(segStart, segEnd, segT);
        }

        accumulatedDist += segLength;
    }

    return m_waypoints.back();
}

} // namespace eden
