#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace eve {

/**
 * @brief Represents a room/area in the spaceship
 */
struct ShipRoom {
    std::string name;
    std::string description;
    glm::vec3 position;         // World position of room center
    glm::vec3 dimensions;       // Room size
    std::vector<std::string> connectedRooms;
    bool isPublic = true;       // Can Eve enter freely?
};

/**
 * @brief Ship equipment/cargo item
 */
struct CargoItem {
    std::string name;
    std::string description;
    int quantity;
    float value;
    std::string location;       // Which room it's stored in
};

/**
 * @brief Spaceship environment manager
 */
class Spaceship {
public:
    Spaceship();
    ~Spaceship() = default;
    
    /**
     * @brief Initialize default ship layout
     */
    void initializeDefaultLayout();
    
    /**
     * @brief Load ship configuration from file
     */
    bool loadConfiguration(const std::string& path);
    
    /**
     * @brief Save ship configuration to file
     */
    bool saveConfiguration(const std::string& path) const;
    
    // Ship info
    const std::string& getName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }
    
    const std::string& getType() const { return m_type; }
    
    // Room management
    const std::vector<ShipRoom>& getRooms() const { return m_rooms; }
    ShipRoom* getRoom(const std::string& name);
    const ShipRoom* getRoom(const std::string& name) const;
    
    // Cargo/equipment
    const std::vector<CargoItem>& getCargo() const { return m_cargo; }
    void addCargo(const CargoItem& item);
    bool removeCargo(const std::string& name, int quantity);
    
    // Ship status
    float getHullIntegrity() const { return m_hullIntegrity; }
    float getFuelLevel() const { return m_fuelLevel; }
    float getPowerLevel() const { return m_powerLevel; }
    
    void setHullIntegrity(float v) { m_hullIntegrity = glm::clamp(v, 0.0f, 1.0f); }
    void setFuelLevel(float v) { m_fuelLevel = glm::clamp(v, 0.0f, 1.0f); }
    void setPowerLevel(float v) { m_powerLevel = glm::clamp(v, 0.0f, 1.0f); }
    
    /**
     * @brief Get a text description of ship status for Eve's context
     */
    std::string getStatusReport() const;
    
private:
    std::string m_name = "Unnamed Vessel";
    std::string m_type = "Light Freighter";
    
    std::vector<ShipRoom> m_rooms;
    std::vector<CargoItem> m_cargo;
    
    // Ship systems status (0.0 - 1.0)
    float m_hullIntegrity = 1.0f;
    float m_fuelLevel = 0.75f;
    float m_powerLevel = 0.9f;
};

} // namespace eve
