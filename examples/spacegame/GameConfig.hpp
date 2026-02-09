#pragma once

#include <string>

namespace spacegame {

// ============================================
// GAME IDENTITY - Change these to rename the game
// ============================================
constexpr const char* GAME_NAME = "Eve Project";
constexpr const char* GAME_WINDOW_TITLE = "Eve Project";
constexpr const char* GAME_VERSION = "0.1.0";
constexpr const char* GAME_CONFIG_FILE = "eve_project.ini";
constexpr const char* GAME_SAVE_EXTENSION = ".evesave";

// Default AI backend settings
constexpr const char* DEFAULT_AI_BACKEND_URL = "http://localhost:8080";
constexpr const char* DEFAULT_PERSONALITY_FILE = "personality.json";

// Window defaults
constexpr int DEFAULT_WINDOW_WIDTH = 1600;
constexpr int DEFAULT_WINDOW_HEIGHT = 900;

} // namespace spacegame
