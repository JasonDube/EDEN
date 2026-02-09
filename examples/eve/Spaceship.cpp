#include "Spaceship.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iostream>

namespace eve {

Spaceship::Spaceship() {
    initializeDefaultLayout();
}

void Spaceship::initializeDefaultLayout() {
    m_rooms.clear();
    
    // Bridge - command center
    ShipRoom bridge;
    bridge.name = "Bridge";
    bridge.description = "The ship's command center. Navigation console, pilot's seat, and main viewscreen.";
    bridge.position = {0, 0, 5};
    bridge.dimensions = {6, 3, 8};
    bridge.connectedRooms = {"Common Area", "Captain's Quarters"};
    bridge.isPublic = true;
    m_rooms.push_back(bridge);
    
    // Captain's Quarters
    ShipRoom captainQuarters;
    captainQuarters.name = "Captain's Quarters";
    captainQuarters.description = "Private quarters for the Captain. Bed, desk, personal storage.";
    captainQuarters.position = {4, 0, 3};
    captainQuarters.dimensions = {4, 3, 4};
    captainQuarters.connectedRooms = {"Bridge"};
    captainQuarters.isPublic = false; // Eve needs permission
    m_rooms.push_back(captainQuarters);
    
    // Common Area
    ShipRoom common;
    common.name = "Common Area";
    common.description = "Living space with seating, small galley, and dining table.";
    common.position = {0, 0, 0};
    common.dimensions = {8, 3, 6};
    common.connectedRooms = {"Bridge", "Cargo Hold", "Engine Room", "Eve's Alcove"};
    common.isPublic = true;
    m_rooms.push_back(common);
    
    // Cargo Hold
    ShipRoom cargo;
    cargo.name = "Cargo Hold";
    cargo.description = "Main storage area for trade goods and equipment. Magnetic clamps and cargo netting.";
    cargo.position = {0, 0, -8};
    cargo.dimensions = {10, 4, 12};
    cargo.connectedRooms = {"Common Area", "Engine Room"};
    cargo.isPublic = true;
    m_rooms.push_back(cargo);
    
    // Engine Room
    ShipRoom engine;
    engine.name = "Engine Room";
    engine.description = "Propulsion and power systems. Reactor core, fuel lines, maintenance access.";
    engine.position = {0, 0, -16};
    engine.dimensions = {8, 4, 8};
    engine.connectedRooms = {"Cargo Hold", "Common Area"};
    engine.isPublic = true;
    m_rooms.push_back(engine);
    
    // Eve's Alcove
    ShipRoom eveSpace;
    eveSpace.name = "Eve's Alcove";
    eveSpace.description = "Eve's designated space. Charging station, data terminal, small personal area.";
    eveSpace.position = {-4, 0, 0};
    eveSpace.dimensions = {3, 3, 3};
    eveSpace.connectedRooms = {"Common Area"};
    eveSpace.isPublic = true;
    m_rooms.push_back(eveSpace);
}

bool Spaceship::loadConfiguration(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    
    try {
        nlohmann::json j;
        file >> j;
        
        m_name = j.value("name", "Unnamed Vessel");
        m_type = j.value("type", "Light Freighter");
        m_hullIntegrity = j.value("hull_integrity", 1.0f);
        m_fuelLevel = j.value("fuel_level", 0.75f);
        m_powerLevel = j.value("power_level", 0.9f);
        
        // Load cargo
        if (j.contains("cargo")) {
            m_cargo.clear();
            for (const auto& item : j["cargo"]) {
                CargoItem ci;
                ci.name = item["name"];
                ci.description = item.value("description", "");
                ci.quantity = item.value("quantity", 1);
                ci.value = item.value("value", 0.0f);
                ci.location = item.value("location", "Cargo Hold");
                m_cargo.push_back(ci);
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Spaceship: Error loading config: " << e.what() << std::endl;
        return false;
    }
}

bool Spaceship::saveConfiguration(const std::string& path) const {
    nlohmann::json j;
    j["name"] = m_name;
    j["type"] = m_type;
    j["hull_integrity"] = m_hullIntegrity;
    j["fuel_level"] = m_fuelLevel;
    j["power_level"] = m_powerLevel;
    
    j["cargo"] = nlohmann::json::array();
    for (const auto& item : m_cargo) {
        nlohmann::json ci;
        ci["name"] = item.name;
        ci["description"] = item.description;
        ci["quantity"] = item.quantity;
        ci["value"] = item.value;
        ci["location"] = item.location;
        j["cargo"].push_back(ci);
    }
    
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    
    file << j.dump(2);
    return true;
}

ShipRoom* Spaceship::getRoom(const std::string& name) {
    for (auto& room : m_rooms) {
        if (room.name == name) {
            return &room;
        }
    }
    return nullptr;
}

const ShipRoom* Spaceship::getRoom(const std::string& name) const {
    for (const auto& room : m_rooms) {
        if (room.name == name) {
            return &room;
        }
    }
    return nullptr;
}

void Spaceship::addCargo(const CargoItem& item) {
    // Check if item already exists
    for (auto& existing : m_cargo) {
        if (existing.name == item.name && existing.location == item.location) {
            existing.quantity += item.quantity;
            return;
        }
    }
    m_cargo.push_back(item);
}

bool Spaceship::removeCargo(const std::string& name, int quantity) {
    for (auto it = m_cargo.begin(); it != m_cargo.end(); ++it) {
        if (it->name == name) {
            if (it->quantity >= quantity) {
                it->quantity -= quantity;
                if (it->quantity == 0) {
                    m_cargo.erase(it);
                }
                return true;
            }
            return false;
        }
    }
    return false;
}

std::string Spaceship::getStatusReport() const {
    std::stringstream ss;
    
    ss << "SHIP STATUS - " << m_name << " (" << m_type << ")\n";
    ss << "Hull Integrity: " << static_cast<int>(m_hullIntegrity * 100) << "%\n";
    ss << "Fuel Level: " << static_cast<int>(m_fuelLevel * 100) << "%\n";
    ss << "Power Level: " << static_cast<int>(m_powerLevel * 100) << "%\n";
    
    if (!m_cargo.empty()) {
        ss << "\nCargo Manifest:\n";
        for (const auto& item : m_cargo) {
            ss << "- " << item.name << " x" << item.quantity;
            if (item.value > 0) {
                ss << " (value: " << item.value << " credits)";
            }
            ss << "\n";
        }
    } else {
        ss << "\nCargo Hold: Empty\n";
    }
    
    return ss.str();
}

} // namespace eve
