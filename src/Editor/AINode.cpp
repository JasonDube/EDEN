#include "AINode.hpp"
#include <algorithm>

namespace eden {

AINode::AINode(uint32_t id, const std::string& name)
    : m_id(id)
    , m_name(name.empty() ? "AINode_" + std::to_string(id) : name)
{
}

void AINode::addBehavior(const Behavior& behavior) {
    m_behaviors.push_back(behavior);
}

void AINode::removeBehavior(const std::string& name) {
    m_behaviors.erase(
        std::remove_if(m_behaviors.begin(), m_behaviors.end(),
            [&name](const Behavior& b) { return b.name == name; }),
        m_behaviors.end());
}

void AINode::addConnection(uint32_t targetNodeId) {
    if (!hasConnection(targetNodeId) && targetNodeId != m_id) {
        m_connections.push_back(targetNodeId);
    }
}

void AINode::removeConnection(uint32_t targetNodeId) {
    m_connections.erase(
        std::remove(m_connections.begin(), m_connections.end(), targetNodeId),
        m_connections.end());
}

bool AINode::hasConnection(uint32_t targetNodeId) const {
    return std::find(m_connections.begin(), m_connections.end(), targetNodeId) != m_connections.end();
}

void AINode::clearConnections() {
    m_connections.clear();
}

void AINode::setProperty(const std::string& key, float value) {
    m_properties[key] = value;
}

float AINode::getProperty(const std::string& key, float defaultVal) const {
    auto it = m_properties.find(key);
    return it != m_properties.end() ? it->second : defaultVal;
}

bool AINode::hasProperty(const std::string& key) const {
    return m_properties.find(key) != m_properties.end();
}

void AINode::addTag(const std::string& tag) {
    if (!hasTag(tag)) {
        m_tags.push_back(tag);
    }
}

void AINode::removeTag(const std::string& tag) {
    m_tags.erase(
        std::remove(m_tags.begin(), m_tags.end(), tag),
        m_tags.end());
}

bool AINode::hasTag(const std::string& tag) const {
    return std::find(m_tags.begin(), m_tags.end(), tag) != m_tags.end();
}

const char* AINode::getTypeName(AINodeType type) {
    switch (type) {
        case AINodeType::WAYPOINT:  return "Waypoint";
        case AINodeType::PATROL:    return "Patrol";
        case AINodeType::SPAWN:     return "Spawn";
        case AINodeType::TRIGGER:   return "Trigger";
        case AINodeType::OBJECTIVE: return "Objective";
        case AINodeType::COVER:     return "Cover";
        case AINodeType::INTEREST:  return "Interest";
        case AINodeType::GRAPH:     return "Graph";
        case AINodeType::CUSTOM:    return "Custom";
        default:                    return "Unknown";
    }
}

const char* AINode::getTypeShortName(AINodeType type) {
    switch (type) {
        case AINodeType::WAYPOINT:  return "WP";
        case AINodeType::PATROL:    return "PT";
        case AINodeType::SPAWN:     return "SP";
        case AINodeType::TRIGGER:   return "TR";
        case AINodeType::OBJECTIVE: return "OB";
        case AINodeType::COVER:     return "CV";
        case AINodeType::INTEREST:  return "IN";
        case AINodeType::GRAPH:     return "GR";
        case AINodeType::CUSTOM:    return "CU";
        default:                    return "??";
    }
}

const char* AINode::getCategoryName(GraphCategory cat) {
    switch (cat) {
        case GraphCategory::NONE:       return "None";
        case GraphCategory::REFUEL:     return "Refuel";
        case GraphCategory::MARKET:     return "Market";
        case GraphCategory::WAREHOUSE:  return "Warehouse";
        case GraphCategory::DOCK:       return "Dock";
        case GraphCategory::FACTORY:    return "Factory";
        case GraphCategory::RESIDENCE:  return "Residence";
        case GraphCategory::OFFICE:     return "Office";
        case GraphCategory::RESTAURANT: return "Restaurant";
        case GraphCategory::HOSPITAL:   return "Hospital";
        case GraphCategory::CUSTOM:     return "Custom";
        default:                        return "Unknown";
    }
}

const char* AINode::getLayerName(GraphLayer layer) {
    switch (layer) {
        case GraphLayer::NONE:       return "None";
        case GraphLayer::PEDESTRIAN: return "Pedestrian";
        case GraphLayer::VEHICLE:    return "Vehicle";
        case GraphLayer::FLYING:     return "Flying";
        case GraphLayer::WATER:      return "Water";
        case GraphLayer::RAIL:       return "Rail";
        case GraphLayer::ALL:        return "All";
        default:                     return "Mixed";
    }
}

} // namespace eden
