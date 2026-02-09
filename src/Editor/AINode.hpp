#pragma once

#include <eden/Action.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace eden {

enum class AINodeType {
    WAYPOINT,    // Navigation point
    PATROL,      // Patrol route marker
    SPAWN,       // Spawn location
    TRIGGER,     // Trigger zone
    OBJECTIVE,   // Mission objective
    COVER,       // Cover position
    INTEREST,    // Point of interest
    GRAPH,       // Graph node for A* pathfinding (diamond shape)
    CUSTOM       // User-defined
};

// Categories for GRAPH nodes (what kind of location is this?)
enum class GraphCategory {
    NONE = 0,       // Generic graph node
    REFUEL,         // Refueling station
    MARKET,         // Buy/sell goods
    WAREHOUSE,      // Storage
    DOCK,           // Ship/vehicle dock
    FACTORY,        // Production facility
    RESIDENCE,      // Housing
    OFFICE,         // Work location
    RESTAURANT,     // Food service
    HOSPITAL,       // Medical facility
    CUSTOM          // User-defined category
};

// Layer flags for movement types (can be combined)
enum class GraphLayer : uint8_t {
    NONE       = 0,
    PEDESTRIAN = 1 << 0,  // Walking paths
    VEHICLE    = 1 << 1,  // Road vehicles
    FLYING     = 1 << 2,  // Aircraft
    WATER      = 1 << 3,  // Boats/ships
    RAIL       = 1 << 4,  // Trains/trams
    ALL        = 0xFF
};

// Bitwise operators for GraphLayer
inline GraphLayer operator|(GraphLayer a, GraphLayer b) {
    return static_cast<GraphLayer>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline GraphLayer operator&(GraphLayer a, GraphLayer b) {
    return static_cast<GraphLayer>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline bool hasLayer(GraphLayer flags, GraphLayer layer) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(layer)) != 0;
}

/**
 * AINode represents a generic AI marker in the game world.
 * Nodes can be assigned behaviors using the Action system and
 * optionally connected to other nodes for pathfinding/patrol routes.
 */
class AINode {
public:
    AINode(uint32_t id, const std::string& name = "");

    // Identity
    uint32_t getId() const { return m_id; }
    const std::string& getName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

    // Position (world coordinates)
    void setPosition(const glm::vec3& pos) { m_position = pos; }
    const glm::vec3& getPosition() const { return m_position; }

    // Type
    void setType(AINodeType type) { m_type = type; }
    AINodeType getType() const { return m_type; }

    // Graph node properties (only used when type == GRAPH)
    void setCategory(GraphCategory cat) { m_category = cat; }
    GraphCategory getCategory() const { return m_category; }

    void setLayers(GraphLayer layers) { m_layers = layers; }
    GraphLayer getLayers() const { return m_layers; }
    void addLayer(GraphLayer layer) { m_layers = m_layers | layer; }
    void removeLayer(GraphLayer layer) { m_layers = static_cast<GraphLayer>(static_cast<uint8_t>(m_layers) & ~static_cast<uint8_t>(layer)); }
    bool hasLayer(GraphLayer layer) const { return eden::hasLayer(m_layers, layer); }

    // Edge cost multiplier (for weighted pathfinding)
    void setEdgeCost(float cost) { m_edgeCost = cost; }
    float getEdgeCost() const { return m_edgeCost; }

    // Radius (for triggers, detection areas, etc.)
    void setRadius(float radius) { m_radius = radius; }
    float getRadius() const { return m_radius; }

    // Behaviors (from Action system)
    void addBehavior(const Behavior& behavior);
    void removeBehavior(const std::string& name);
    std::vector<Behavior>& getBehaviors() { return m_behaviors; }
    const std::vector<Behavior>& getBehaviors() const { return m_behaviors; }
    bool hasBehaviors() const { return !m_behaviors.empty(); }

    // Connections to other nodes (by ID)
    void addConnection(uint32_t targetNodeId);
    void removeConnection(uint32_t targetNodeId);
    bool hasConnection(uint32_t targetNodeId) const;
    const std::vector<uint32_t>& getConnections() const { return m_connections; }
    void clearConnections();

    // Selection state (for editor)
    void setSelected(bool selected) { m_selected = selected; }
    bool isSelected() const { return m_selected; }

    // Visibility
    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

    // Custom properties (key-value for extensibility)
    void setProperty(const std::string& key, float value);
    float getProperty(const std::string& key, float defaultVal = 0.0f) const;
    bool hasProperty(const std::string& key) const;
    const std::unordered_map<std::string, float>& getProperties() const { return m_properties; }

    // Tags for filtering/grouping
    void addTag(const std::string& tag);
    void removeTag(const std::string& tag);
    bool hasTag(const std::string& tag) const;
    const std::vector<std::string>& getTags() const { return m_tags; }

    // Helper to get type as string
    static const char* getTypeName(AINodeType type);
    static const char* getTypeShortName(AINodeType type);
    static const char* getCategoryName(GraphCategory cat);
    static const char* getLayerName(GraphLayer layer);

private:
    uint32_t m_id;
    std::string m_name;
    glm::vec3 m_position{0.0f};
    AINodeType m_type = AINodeType::WAYPOINT;
    float m_radius = 5.0f;

    // Graph node specific
    GraphCategory m_category = GraphCategory::NONE;
    GraphLayer m_layers = GraphLayer::ALL;
    float m_edgeCost = 1.0f;

    std::vector<Behavior> m_behaviors;
    std::vector<uint32_t> m_connections;

    bool m_selected = false;
    bool m_visible = true;

    std::unordered_map<std::string, float> m_properties;
    std::vector<std::string> m_tags;
};

} // namespace eden
