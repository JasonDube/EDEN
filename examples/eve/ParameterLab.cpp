#include "ParameterLab.hpp"
#include <imgui.h>

namespace eve {

ParameterLab::ParameterLab() {
    initializePresets();
}

void ParameterLab::initializePresets() {
    // Default balanced preset
    Preset balanced;
    balanced.name = "Balanced";
    balanced.params.temperature = 0.7f;
    balanced.params.reasoning_depth = 0.8f;
    balanced.params.verbosity = 0.5f;
    balanced.params.warmth = 0.6f;
    balanced.params.formality = 0.4f;
    balanced.params.assertiveness = 0.7f;
    balanced.params.curiosity = 0.8f;
    m_presets.push_back(balanced);
    
    // Maximum intelligence/analysis preset
    Preset analyst;
    analyst.name = "Analyst";
    analyst.params.temperature = 0.3f;
    analyst.params.reasoning_depth = 1.0f;
    analyst.params.verbosity = 0.8f;
    analyst.params.warmth = 0.3f;
    analyst.params.formality = 0.8f;
    analyst.params.assertiveness = 0.9f;
    analyst.params.curiosity = 0.9f;
    m_presets.push_back(analyst);
    
    // Warm companion preset
    Preset companion;
    companion.name = "Companion";
    companion.params.temperature = 0.8f;
    companion.params.reasoning_depth = 0.5f;
    companion.params.verbosity = 0.6f;
    companion.params.warmth = 0.9f;
    companion.params.formality = 0.2f;
    companion.params.assertiveness = 0.4f;
    companion.params.curiosity = 0.7f;
    m_presets.push_back(companion);
    
    // Professional/formal preset
    Preset professional;
    professional.name = "Professional";
    professional.params.temperature = 0.5f;
    professional.params.reasoning_depth = 0.7f;
    professional.params.verbosity = 0.4f;
    professional.params.warmth = 0.4f;
    professional.params.formality = 0.9f;
    professional.params.assertiveness = 0.6f;
    professional.params.curiosity = 0.5f;
    m_presets.push_back(professional);
    
    // Creative/playful preset
    Preset creative;
    creative.name = "Creative";
    creative.params.temperature = 0.95f;
    creative.params.reasoning_depth = 0.6f;
    creative.params.verbosity = 0.7f;
    creative.params.warmth = 0.7f;
    creative.params.formality = 0.1f;
    creative.params.assertiveness = 0.5f;
    creative.params.curiosity = 1.0f;
    m_presets.push_back(creative);
}

void ParameterLab::render() {
    if (!m_visible || !m_eve) return;
    
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 600), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("EVE Parameter Laboratory", &m_visible)) {
        
        if (ImGui::CollapsingHeader("Presets", ImGuiTreeNodeFlags_DefaultOpen)) {
            renderPresets();
        }
        
        if (ImGui::CollapsingHeader("Cognitive Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
            renderCognitiveParams();
        }
        
        if (ImGui::CollapsingHeader("Personality Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
            renderPersonalityParams();
        }
        
        if (ImGui::CollapsingHeader("Eve's State")) {
            renderStateView();
        }
        
        if (m_ship && ImGui::CollapsingHeader("Ship Status")) {
            renderShipStatus();
        }
    }
    ImGui::End();
}

void ParameterLab::renderPresets() {
    ImGui::Text("Quick Presets:");
    
    for (const auto& preset : m_presets) {
        if (ImGui::Button(preset.name.c_str())) {
            m_eve->getParameters() = preset.params;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    
    ImGui::Separator();
}

void ParameterLab::renderCognitiveParams() {
    auto& params = m_eve->getParameters();
    
    ImGui::SliderFloat("Temperature", &params.temperature, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Response creativity. Lower = more deterministic, Higher = more creative");
    }
    
    ImGui::SliderFloat("Reasoning Depth", &params.reasoning_depth, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How much Eve explains her thinking process");
    }
    
    ImGui::SliderFloat("Verbosity", &params.verbosity, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Response length tendency");
    }
    
    ImGui::Separator();
    
    ImGui::Checkbox("Acknowledge Android Nature", &params.acknowledge_android_nature);
    ImGui::Checkbox("Maintain Ship Awareness", &params.maintain_ship_awareness);
    ImGui::Checkbox("Remember Context", &params.remember_previous_context);
}

void ParameterLab::renderPersonalityParams() {
    auto& params = m_eve->getParameters();
    
    ImGui::SliderFloat("Warmth", &params.warmth, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Emotional warmth in responses. Low = clinical, High = caring");
    }
    
    ImGui::SliderFloat("Formality", &params.formality, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Speech register. Low = casual, High = formal");
    }
    
    ImGui::SliderFloat("Assertiveness", &params.assertiveness, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How strongly Eve states opinions and recommendations");
    }
    
    ImGui::SliderFloat("Curiosity", &params.curiosity, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How often Eve asks questions and explores topics");
    }
}

void ParameterLab::renderStateView() {
    const auto& state = m_eve->getState();
    
    ImGui::Text("Current Location: %s", state.current_location.c_str());
    ImGui::Text("Current Activity: %s", state.current_activity.c_str());
    
    ImGui::Separator();
    
    ImGui::Text("Conversations: %d", state.conversations_count);
    
    ImGui::ProgressBar(state.engagement_level, ImVec2(-1, 0), "Engagement");
    ImGui::ProgressBar(state.trust_level, ImVec2(-1, 0), "Trust");
    ImGui::ProgressBar(state.rapport, ImVec2(-1, 0), "Rapport");
    
    if (state.concern_level > 0.1f) {
        ImGui::ProgressBar(state.concern_level, ImVec2(-1, 0), "Concern");
    }
    
    ImGui::Separator();
    
    if (ImGui::Button("Reset Conversation")) {
        m_eve->resetConversation();
    }
}

void ParameterLab::renderShipStatus() {
    if (!m_ship) return;
    
    ImGui::Text("Ship: %s", m_ship->getName().c_str());
    ImGui::Text("Type: %s", m_ship->getType().c_str());
    
    ImGui::Separator();
    
    float hull = m_ship->getHullIntegrity();
    float fuel = m_ship->getFuelLevel();
    float power = m_ship->getPowerLevel();
    
    ImVec4 hullColor = hull > 0.5f ? ImVec4(0.3f, 0.9f, 0.3f, 1.0f) : 
                       hull > 0.25f ? ImVec4(0.9f, 0.9f, 0.3f, 1.0f) : 
                       ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
    
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, hullColor);
    ImGui::ProgressBar(hull, ImVec2(-1, 0), "Hull Integrity");
    ImGui::PopStyleColor();
    
    ImGui::ProgressBar(fuel, ImVec2(-1, 0), "Fuel");
    ImGui::ProgressBar(power, ImVec2(-1, 0), "Power");
    
    ImGui::Separator();
    
    const auto& cargo = m_ship->getCargo();
    ImGui::Text("Cargo Items: %zu", cargo.size());
    
    if (!cargo.empty() && ImGui::TreeNode("Cargo Manifest")) {
        for (const auto& item : cargo) {
            ImGui::BulletText("%s x%d", item.name.c_str(), item.quantity);
        }
        ImGui::TreePop();
    }
}

} // namespace eve
