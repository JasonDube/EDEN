#pragma once

#include <glm/glm.hpp>
#include <string>

namespace trading {

// Server
constexpr const char* SERVER_HOST = "localhost";
constexpr int SERVER_PORT = 8090;

// Window
constexpr const char* WINDOW_TITLE = "EDEN Trading Terminal";
constexpr int WINDOW_WIDTH = 1600;
constexpr int WINDOW_HEIGHT = 900;
constexpr const char* IMGUI_INI_FILE = "trading_terminal.ini";

// Candle geometry
constexpr float CANDLE_SPACING = 2.0f;        // Z distance between candles
constexpr float CANDLE_BODY_WIDTH = 1.2f;      // X/Z width of candle body
constexpr float CANDLE_WICK_WIDTH = 0.15f;     // X/Z width of wick
constexpr float PRICE_SCALE = 0.5f;            // World units per dollar

// Colors (RGBA)
const glm::vec4 BULL_COLOR = {0.15f, 0.75f, 0.35f, 1.0f};  // Green
const glm::vec4 BEAR_COLOR = {0.85f, 0.20f, 0.20f, 1.0f};  // Red
const glm::vec4 WICK_COLOR = {0.6f, 0.6f, 0.6f, 1.0f};     // Gray
const glm::vec4 SELECTED_COLOR = {1.0f, 0.84f, 0.0f, 1.0f}; // Gold
const glm::vec4 GRID_COLOR = {0.3f, 0.3f, 0.4f, 1.0f};     // Dim blue-gray

// Background
constexpr float BG_R = 0.05f;
constexpr float BG_G = 0.05f;
constexpr float BG_B = 0.08f;

// Default symbol
constexpr const char* DEFAULT_SYMBOL = "AAPL";
constexpr const char* DEFAULT_RESOLUTION = "D";

// Camera
constexpr float CAMERA_SPEED = 30.0f;
constexpr float LOOK_SPEED = 0.5f;

// Grid
constexpr float GRID_LINE_LENGTH = 500.0f;     // X extent of grid lines

// Signal markers
const glm::vec4 BUY_SIGNAL_COLOR  = {0.1f, 0.6f, 1.0f, 1.0f};   // Blue
const glm::vec4 SELL_SIGNAL_COLOR = {1.0f, 0.5f, 0.0f, 1.0f};    // Orange
constexpr float SIGNAL_MARKER_SIZE = 0.8f;

} // namespace trading
