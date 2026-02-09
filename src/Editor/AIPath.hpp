#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace eden {

/**
 * AIPath represents a named sequence of waypoints that NPCs can follow.
 * Paths are independent of AINodes - they store their own waypoint positions.
 */
class AIPath {
public:
    AIPath(uint32_t id, const std::string& name = "");

    // Identity
    uint32_t getId() const { return m_id; }
    const std::string& getName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

    // Waypoints (ordered positions)
    void addWaypoint(const glm::vec3& position);
    void insertWaypoint(size_t index, const glm::vec3& position);
    void removeWaypoint(size_t index);
    void setWaypoint(size_t index, const glm::vec3& position);
    void clearWaypoints();

    const std::vector<glm::vec3>& getWaypoints() const { return m_waypoints; }
    size_t getWaypointCount() const { return m_waypoints.size(); }
    glm::vec3 getWaypoint(size_t index) const;

    // Loop behavior
    void setLooping(bool loop) { m_loop = loop; }
    bool isLooping() const { return m_loop; }

    // Selection (for editor)
    void setSelected(bool selected) { m_selected = selected; }
    bool isSelected() const { return m_selected; }

    // Visibility (for rendering)
    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

    // Color (for rendering different paths)
    void setColor(const glm::vec3& color) { m_color = color; }
    const glm::vec3& getColor() const { return m_color; }

    // Calculate total path length
    float getTotalLength() const;

    // Get position along path at normalized t (0-1)
    glm::vec3 getPositionAtT(float t) const;

private:
    uint32_t m_id;
    std::string m_name;
    std::vector<glm::vec3> m_waypoints;
    bool m_loop = true;
    bool m_selected = false;
    bool m_visible = true;
    glm::vec3 m_color{1.0f, 0.5f, 0.0f};  // Default orange
};

} // namespace eden
