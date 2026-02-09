#pragma once

#include "Eve.hpp"
#include "Spaceship.hpp"

namespace eve {

/**
 * @brief ImGui interface for tuning Eve's parameters and viewing state
 */
class ParameterLab {
public:
    ParameterLab();
    ~ParameterLab() = default;
    
    /**
     * @brief Set references to Eve and Ship
     */
    void setEve(Eve* eve) { m_eve = eve; }
    void setShip(Spaceship* ship) { m_ship = ship; }
    
    /**
     * @brief Render the parameter lab interface
     */
    void render();
    
    /**
     * @brief Check if lab window is visible
     */
    bool isVisible() const { return m_visible; }
    void setVisible(bool v) { m_visible = v; }
    void toggleVisible() { m_visible = !m_visible; }
    
private:
    void renderCognitiveParams();
    void renderPersonalityParams();
    void renderStateView();
    void renderShipStatus();
    void renderPresets();
    
    Eve* m_eve = nullptr;
    Spaceship* m_ship = nullptr;
    bool m_visible = true;
    
    // Preset configurations
    struct Preset {
        std::string name;
        PersonalityParameters params;
    };
    std::vector<Preset> m_presets;
    
    void initializePresets();
};

} // namespace eve
