/**
 * @file main.cpp
 * @brief EDEN Trading Terminal
 *
 * 3D stock trading visualization — walk through candlestick charts.
 * Fetches real market data from the Python server (localhost:8090).
 * Supports Alpaca paper/live trading, algo engine with strategy backtesting,
 * and 3D signal markers on the candlestick chart.
 */

#include "TradingConfig.hpp"
#include "MarketDataClient.hpp"

#include "Renderer/VulkanApplicationBase.hpp"
#include "Renderer/VulkanContext.hpp"
#include "Renderer/Swapchain.hpp"
#include "Renderer/ImGuiManager.hpp"
#include "Renderer/ModelRenderer.hpp"
#include "Editor/PrimitiveMeshBuilder.hpp"

#include <eden/Camera.hpp>
#include <eden/Input.hpp>
#include <eden/Window.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <map>

using namespace eden;

// Per-candle rendering data (transforms computed on CPU, shared meshes on GPU)
struct CandleModel {
    glm::mat4 bodyTransform;
    glm::mat4 wickTransform;
    int candleIndex;
    bool bullish;
    // AABB for picking (world space)
    glm::vec3 aabbMin;
    glm::vec3 aabbMax;
};

class TradingTerminalApp : public VulkanApplicationBase {
public:
    TradingTerminalApp()
        : VulkanApplicationBase(
            trading::WINDOW_WIDTH,
            trading::WINDOW_HEIGHT,
            trading::WINDOW_TITLE)
    {}

protected:
    void onInit() override {
        m_modelRenderer = std::make_unique<ModelRenderer>(
            getContext(),
            getSwapchain().getRenderPass(),
            getSwapchain().getExtent()
        );

        m_imguiManager.init(getContext(), getSwapchain(), getWindow().getHandle(),
                            trading::IMGUI_INI_FILE);

        // Create shared meshes: unit cubes with different colors
        auto bullMesh = PrimitiveMeshBuilder::createCube(1.0f, trading::BULL_COLOR);
        auto bearMesh = PrimitiveMeshBuilder::createCube(1.0f, trading::BEAR_COLOR);
        auto wickMesh = PrimitiveMeshBuilder::createCube(1.0f, trading::WICK_COLOR);
        auto selectedMesh = PrimitiveMeshBuilder::createCube(1.0f, trading::SELECTED_COLOR);
        auto buyMarkerMesh = PrimitiveMeshBuilder::createCube(1.0f, trading::BUY_SIGNAL_COLOR);
        auto sellMarkerMesh = PrimitiveMeshBuilder::createCube(1.0f, trading::SELL_SIGNAL_COLOR);

        m_bullHandle = m_modelRenderer->createModel(bullMesh.vertices, bullMesh.indices);
        m_bearHandle = m_modelRenderer->createModel(bearMesh.vertices, bearMesh.indices);
        m_wickHandle = m_modelRenderer->createModel(wickMesh.vertices, wickMesh.indices);
        m_selectedHandle = m_modelRenderer->createModel(selectedMesh.vertices, selectedMesh.indices);
        m_buyMarkerHandle = m_modelRenderer->createModel(buyMarkerMesh.vertices, buyMarkerMesh.indices);
        m_sellMarkerHandle = m_modelRenderer->createModel(sellMarkerMesh.vertices, sellMarkerMesh.indices);

        // Setup camera looking along +Z (candles extend in Z direction)
        m_camera.setPosition(glm::vec3(-10.0f, 30.0f, 50.0f));
        m_camera.setYaw(30.0f);
        m_camera.setPitch(-20.0f);
        m_camera.setNoClip(true);

        // Start market data client
        m_client = std::make_unique<trading::MarketDataClient>(
            trading::SERVER_HOST, trading::SERVER_PORT);
        m_client->start();

        // Fetch initial data
        m_currentSymbol = trading::DEFAULT_SYMBOL;
        m_currentResolution = trading::DEFAULT_RESOLUTION;
        requestData();

        // Fetch initial account and algo strategies
        m_client->fetchAccount([this](const trading::AccountResponse& resp) {
            if (resp.success) m_account = resp.account;
        });
        m_client->fetchAlgoStrategies([this](const trading::AlgoStrategiesResponse& resp) {
            if (resp.success) m_algoStrategies = resp.strategies;
        });

        std::cout << "EDEN Trading Terminal initialized." << std::endl;
        std::cout << "Connecting to data server at " << trading::SERVER_HOST
                  << ":" << trading::SERVER_PORT << std::endl;
    }

    void onCleanup() override {
        vkDeviceWaitIdle(getContext().getDevice());
        if (m_client) m_client->stop();
        m_modelRenderer.reset();
        m_imguiManager.cleanup();
    }

    void onSwapchainRecreated() override {
        m_modelRenderer = std::make_unique<ModelRenderer>(
            getContext(),
            getSwapchain().getRenderPass(),
            getSwapchain().getExtent()
        );

        // Recreate shared meshes
        auto bullMesh = PrimitiveMeshBuilder::createCube(1.0f, trading::BULL_COLOR);
        auto bearMesh = PrimitiveMeshBuilder::createCube(1.0f, trading::BEAR_COLOR);
        auto wickMesh = PrimitiveMeshBuilder::createCube(1.0f, trading::WICK_COLOR);
        auto selectedMesh = PrimitiveMeshBuilder::createCube(1.0f, trading::SELECTED_COLOR);
        auto buyMarkerMesh = PrimitiveMeshBuilder::createCube(1.0f, trading::BUY_SIGNAL_COLOR);
        auto sellMarkerMesh = PrimitiveMeshBuilder::createCube(1.0f, trading::SELL_SIGNAL_COLOR);

        m_bullHandle = m_modelRenderer->createModel(bullMesh.vertices, bullMesh.indices);
        m_bearHandle = m_modelRenderer->createModel(bearMesh.vertices, bearMesh.indices);
        m_wickHandle = m_modelRenderer->createModel(wickMesh.vertices, wickMesh.indices);
        m_selectedHandle = m_modelRenderer->createModel(selectedMesh.vertices, selectedMesh.indices);
        m_buyMarkerHandle = m_modelRenderer->createModel(buyMarkerMesh.vertices, buyMarkerMesh.indices);
        m_sellMarkerHandle = m_modelRenderer->createModel(sellMarkerMesh.vertices, sellMarkerMesh.indices);
    }

    void update(float deltaTime) override {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        // Poll market data responses
        if (m_client) {
            m_client->pollResponses();
        }

        // Periodic polling for account/positions (every 5 seconds)
        m_accountPollTimer += deltaTime;
        if (m_accountPollTimer >= 5.0f) {
            m_accountPollTimer = 0;
            if (m_client) {
                m_client->fetchAccount([this](const trading::AccountResponse& resp) {
                    if (resp.success) m_account = resp.account;
                });
                m_client->fetchPositions([this](const trading::PositionsResponse& resp) {
                    if (resp.success) m_positions = resp.positions;
                });
            }
        }

        // Poll algo status (every 3 seconds if any algo running)
        m_algoPollTimer += deltaTime;
        if (m_algoPollTimer >= 3.0f && !m_algoStatuses.empty()) {
            m_algoPollTimer = 0;
            if (m_client) {
                m_client->fetchAlgoStatus([this](const trading::AlgoStatusResponse& resp) {
                    if (resp.success) {
                        m_algoStatuses = resp.algos;
                        // Merge signals from all running algos for current symbol
                        m_signals.clear();
                        for (const auto& algo : m_algoStatuses) {
                            if (algo.symbol == m_currentSymbol && algo.status == "running") {
                                for (const auto& sig : algo.signals) {
                                    m_signals.push_back(sig);
                                }
                            }
                        }
                    }
                });
            }
        }

        // Handle input before Input::update clears pressed state
        handleKeyboardShortcuts();

        Input::update();

        if (!ImGui::GetIO().WantCaptureKeyboard) {
            handleCameraInput(deltaTime);
        }

        if (!ImGui::GetIO().WantCaptureMouse) {
            handleMousePicking();

            // Scroll wheel zoom in ortho mode
            if (m_orthoView) {
                float scroll = Input::getScrollDelta();
                if (scroll != 0.0f) {
                    float size = m_camera.getOrthoSize();
                    size *= (scroll > 0) ? 0.9f : 1.1f;
                    size = std::clamp(size, 2.0f, 500.0f);
                    m_camera.setOrthoSize(size);
                }
            }
        }
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) override {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);

        auto& swapchain = getSwapchain();
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = swapchain.getRenderPass();
        renderPassInfo.framebuffer = swapchain.getFramebuffers()[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapchain.getExtent();

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{trading::BG_R, trading::BG_G, trading::BG_B, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        auto extent = swapchain.getExtent();
        float aspect = static_cast<float>(extent.width) / extent.height;
        glm::mat4 view = m_camera.getViewMatrix();
        glm::mat4 proj = m_camera.getProjectionMatrix(aspect, 0.1f, 10000.0f);
        glm::mat4 viewProj = proj * view;

        // Render candle bodies and wicks
        for (size_t i = 0; i < m_candleModels.size(); i++) {
            const auto& cm = m_candleModels[i];
            bool isSelected = (m_selectedCandle == cm.candleIndex);

            // Wick
            if (isSelected) {
                m_modelRenderer->render(cmd, viewProj, m_selectedHandle, cm.wickTransform);
            } else {
                m_modelRenderer->render(cmd, viewProj, m_wickHandle, cm.wickTransform);
            }

            // Body
            uint32_t bodyHandle;
            if (isSelected) {
                bodyHandle = m_selectedHandle;
            } else {
                bodyHandle = cm.bullish ? m_bullHandle : m_bearHandle;
            }
            m_modelRenderer->render(cmd, viewProj, bodyHandle, cm.bodyTransform);
        }

        // Render signal markers (buy=blue below candle, sell=orange above candle)
        for (const auto& sig : m_signals) {
            if (sig.candleIndex < 0 || sig.candleIndex >= static_cast<int>(m_candles.size()))
                continue;

            float z = static_cast<float>(sig.candleIndex) * trading::CANDLE_SPACING;
            float markerSize = trading::SIGNAL_MARKER_SIZE;
            float y;

            if (sig.side == "buy") {
                // Place below the candle low
                float low = (m_candles[sig.candleIndex].low - m_chartMinPrice) * trading::PRICE_SCALE;
                y = low - markerSize * 1.5f;
            } else {
                // Place above the candle high
                float high = (m_candles[sig.candleIndex].high - m_chartMinPrice) * trading::PRICE_SCALE;
                y = high + markerSize * 1.5f;
            }

            glm::mat4 transform = glm::mat4(1.0f);
            // Rotate 45 degrees for diamond shape
            transform = glm::translate(transform, glm::vec3(0.0f, y, z));
            transform = glm::rotate(transform, glm::radians(45.0f), glm::vec3(0, 0, 1));
            transform = glm::scale(transform, glm::vec3(markerSize));

            uint32_t handle = (sig.side == "buy") ? m_buyMarkerHandle : m_sellMarkerHandle;
            m_modelRenderer->render(cmd, viewProj, handle, transform);
        }

        // Render price grid lines
        if (!m_gridLines.empty()) {
            m_modelRenderer->renderLines(cmd, viewProj, m_gridLines,
                                          glm::vec3(trading::GRID_COLOR));
        }

        // Render ImGui
        renderUI();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
    }

private:
    void requestData() {
        m_loading = true;
        m_client->fetchCandles(m_currentSymbol, m_currentResolution,
            [this](const trading::CandleResponse& resp) {
                m_loading = false;
                if (resp.success) {
                    m_candles = resp.candles;
                    m_selectedCandle = -1;
                    buildCandleChart();
                    std::cout << "Loaded " << m_candles.size() << " candles for "
                              << resp.symbol << std::endl;
                } else {
                    std::cerr << "Failed to fetch candles: " << resp.error << std::endl;
                    m_statusMessage = "Error: " + resp.error;
                }
            });

        m_client->fetchQuote(m_currentSymbol,
            [this](const trading::QuoteResponse& resp) {
                if (resp.success) {
                    m_quote = resp.quote;
                }
            });
    }

    void buildCandleChart() {
        m_candleModels.clear();
        m_gridLines.clear();

        if (m_candles.empty()) return;

        // Find price range for normalization
        float minLow = m_candles[0].low;
        float maxHigh = m_candles[0].high;
        for (const auto& c : m_candles) {
            minLow = std::min(minLow, c.low);
            maxHigh = std::max(maxHigh, c.high);
        }
        m_chartMinPrice = minLow;
        m_chartMaxPrice = maxHigh;

        float priceRange = maxHigh - minLow;
        if (priceRange < 0.01f) priceRange = 1.0f;

        for (size_t i = 0; i < m_candles.size(); i++) {
            const auto& candle = m_candles[i];
            CandleModel cm;
            cm.candleIndex = static_cast<int>(i);
            cm.bullish = candle.bullish();

            float z = static_cast<float>(i) * trading::CANDLE_SPACING;

            // Body: position between open and close
            float bodyBottom = (std::min(candle.open, candle.close) - minLow) * trading::PRICE_SCALE;
            float bodyTop = (std::max(candle.open, candle.close) - minLow) * trading::PRICE_SCALE;
            float bodyHeight = std::max(bodyTop - bodyBottom, 0.05f); // Min height for doji

            cm.bodyTransform = glm::mat4(1.0f);
            cm.bodyTransform = glm::translate(cm.bodyTransform,
                glm::vec3(0.0f, bodyBottom, z));
            cm.bodyTransform = glm::scale(cm.bodyTransform,
                glm::vec3(trading::CANDLE_BODY_WIDTH, bodyHeight, trading::CANDLE_BODY_WIDTH));

            // Wick: position between low and high
            float wickBottom = (candle.low - minLow) * trading::PRICE_SCALE;
            float wickTop = (candle.high - minLow) * trading::PRICE_SCALE;
            float wickHeight = std::max(wickTop - wickBottom, 0.05f);

            cm.wickTransform = glm::mat4(1.0f);
            cm.wickTransform = glm::translate(cm.wickTransform,
                glm::vec3(0.0f, wickBottom, z));
            cm.wickTransform = glm::scale(cm.wickTransform,
                glm::vec3(trading::CANDLE_WICK_WIDTH, wickHeight, trading::CANDLE_WICK_WIDTH));

            // AABB for picking (use body bounds, slightly expanded)
            float hw = trading::CANDLE_BODY_WIDTH / 2.0f;
            cm.aabbMin = glm::vec3(-hw, wickBottom, z - hw);
            cm.aabbMax = glm::vec3(hw, wickTop, z + hw);

            m_candleModels.push_back(cm);
        }

        // Build price grid lines at round price levels
        buildPriceGrid(minLow, maxHigh);
    }

    void buildPriceGrid(float minPrice, float maxPrice) {
        m_gridLines.clear();

        // Choose grid interval based on price range
        float range = maxPrice - minPrice;
        float interval = 1.0f;
        if (range > 500) interval = 50.0f;
        else if (range > 200) interval = 25.0f;
        else if (range > 100) interval = 10.0f;
        else if (range > 50) interval = 5.0f;
        else if (range > 20) interval = 2.0f;
        else if (range > 5) interval = 1.0f;
        else interval = 0.5f;

        m_gridPrices.clear();

        float startPrice = std::floor(minPrice / interval) * interval;
        float chartLength = static_cast<float>(m_candles.size()) * trading::CANDLE_SPACING + 5.0f;

        for (float price = startPrice; price <= maxPrice + interval; price += interval) {
            float y = (price - minPrice) * trading::PRICE_SCALE;

            // Line extending along Z axis (the candle direction)
            m_gridLines.push_back(glm::vec3(-trading::GRID_LINE_LENGTH * 0.1f, y, -2.0f));
            m_gridLines.push_back(glm::vec3(-trading::GRID_LINE_LENGTH * 0.1f, y, chartLength));

            m_gridPrices.push_back(price);
        }
    }

    void handleCameraInput(float deltaTime) {
        if (m_orthoView) {
            // Ortho mode: WASD pans the view (Y = up/down, Z = along chart)
            float panSpeed = m_camera.getOrthoSize() * 2.0f * deltaTime;
            glm::vec3 pos = m_camera.getPosition();
            if (Input::isKeyDown(Input::KEY_W)) pos.y += panSpeed;
            if (Input::isKeyDown(Input::KEY_S)) pos.y -= panSpeed;
            if (Input::isKeyDown(Input::KEY_A)) pos.z -= panSpeed;
            if (Input::isKeyDown(Input::KEY_D)) pos.z += panSpeed;
            m_camera.setPosition(pos);

            // Right-mouse drag to pan
            if (Input::isMouseButtonDown(Input::MOUSE_RIGHT)) {
                auto delta = Input::getMouseDelta();
                float scale = m_camera.getOrthoSize() * 0.005f;
                pos.z -= delta.x * scale;
                pos.y += delta.y * scale;
                m_camera.setPosition(pos);
                Input::setMouseCaptured(true);
            } else {
                Input::setMouseCaptured(false);
            }
        } else {
            // Free-fly mode
            bool forward = Input::isKeyDown(Input::KEY_W);
            bool backward = Input::isKeyDown(Input::KEY_S);
            bool left = Input::isKeyDown(Input::KEY_A);
            bool right = Input::isKeyDown(Input::KEY_D);
            bool up = Input::isKeyDown(Input::KEY_SPACE);
            bool down = Input::isKeyDown(Input::KEY_LEFT_CONTROL);

            m_camera.setSpeed(trading::CAMERA_SPEED);
            m_camera.processKeyboard(deltaTime, forward, backward, left, right, up, down);

            if (Input::isMouseButtonDown(Input::MOUSE_RIGHT)) {
                auto delta = Input::getMouseDelta();
                m_camera.processMouse(delta.x * trading::LOOK_SPEED, delta.y * trading::LOOK_SPEED);
                Input::setMouseCaptured(true);
            } else {
                Input::setMouseCaptured(false);
            }
        }
    }

    void handleKeyboardShortcuts() {
        if (Input::isKeyPressed(Input::KEY_ESCAPE)) {
            m_selectedCandle = -1;
        }

        // V - Toggle ortho side view (traditional chart view)
        if (Input::isKeyPressed(Input::KEY_V)) {
            if (m_orthoView) {
                // Return to perspective free-fly
                m_camera.setProjectionMode(ProjectionMode::Perspective);
                m_camera.setPosition(m_savedCameraPos);
                m_camera.setYaw(m_savedCameraYaw);
                m_camera.setPitch(m_savedCameraPitch);
                m_orthoView = false;
            } else {
                // Save current camera state
                m_savedCameraPos = m_camera.getPosition();
                m_savedCameraYaw = m_camera.getYaw();
                m_savedCameraPitch = m_camera.getPitch();

                // Snap to side view: look along -X axis (Right preset)
                float chartMidZ = m_candles.empty() ? 0.0f :
                    (m_candles.size() / 2.0f) * trading::CANDLE_SPACING;
                float chartMidY = (m_chartMaxPrice - m_chartMinPrice) * 0.5f * trading::PRICE_SCALE;

                float chartHeight = (m_chartMaxPrice - m_chartMinPrice) * trading::PRICE_SCALE;
                m_camera.setOrthoSize(std::max(chartHeight * 0.6f, 10.0f));

                m_camera.setPosition(glm::vec3(50.0f, chartMidY, chartMidZ));
                m_camera.setYaw(180.0f);
                m_camera.setPitch(0.0f);
                m_camera.setProjectionMode(ProjectionMode::Orthographic);
                m_orthoView = true;
            }
        }

        // T - Toggle trade panel
        if (Input::isKeyPressed(Input::KEY_T)) {
            if (!ImGui::GetIO().WantCaptureKeyboard)
                m_showTradePanel = !m_showTradePanel;
        }

        // P - Toggle positions panel
        if (Input::isKeyPressed(Input::KEY_P)) {
            if (!ImGui::GetIO().WantCaptureKeyboard)
                m_showPositionsPanel = !m_showPositionsPanel;
        }

        // A - Toggle algo panel
        if (Input::isKeyPressed(Input::KEY_A)) {
            if (!ImGui::GetIO().WantCaptureKeyboard && !Input::isKeyDown(Input::KEY_LEFT_CONTROL))
                m_showAlgoPanel = !m_showAlgoPanel;
        }
    }

    void handleMousePicking() {
        if (!Input::isMouseButtonPressed(Input::MOUSE_LEFT)) return;
        if (m_candleModels.empty()) return;

        // Get mouse position and compute ray
        auto mousePos = Input::getMousePosition();
        auto extent = getSwapchain().getExtent();
        float ndcX = (2.0f * mousePos.x / extent.width) - 1.0f;
        float ndcY = (2.0f * mousePos.y / extent.height) - 1.0f;

        float aspect = static_cast<float>(extent.width) / extent.height;
        glm::mat4 view = m_camera.getViewMatrix();
        glm::mat4 proj = m_camera.getProjectionMatrix(aspect, 0.1f, 10000.0f);
        glm::mat4 invVP = glm::inverse(proj * view);

        glm::vec4 nearPoint = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
        glm::vec4 farPoint = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
        nearPoint /= nearPoint.w;
        farPoint /= farPoint.w;

        glm::vec3 rayOrigin = glm::vec3(nearPoint);
        glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint - nearPoint));

        // Test against candle AABBs
        float closestT = 1e30f;
        int closestCandle = -1;

        for (const auto& cm : m_candleModels) {
            float t = rayAABBIntersect(rayOrigin, rayDir, cm.aabbMin, cm.aabbMax);
            if (t > 0 && t < closestT) {
                closestT = t;
                closestCandle = cm.candleIndex;
            }
        }

        m_selectedCandle = closestCandle;
    }

    float rayAABBIntersect(const glm::vec3& origin, const glm::vec3& dir,
                           const glm::vec3& aabbMin, const glm::vec3& aabbMax) {
        glm::vec3 invDir = 1.0f / dir;

        float t1 = (aabbMin.x - origin.x) * invDir.x;
        float t2 = (aabbMax.x - origin.x) * invDir.x;
        float t3 = (aabbMin.y - origin.y) * invDir.y;
        float t4 = (aabbMax.y - origin.y) * invDir.y;
        float t5 = (aabbMin.z - origin.z) * invDir.z;
        float t6 = (aabbMax.z - origin.z) * invDir.z;

        float tmin = std::max(std::max(std::min(t1, t2), std::min(t3, t4)), std::min(t5, t6));
        float tmax = std::min(std::min(std::max(t1, t2), std::max(t3, t4)), std::max(t5, t6));

        if (tmax < 0 || tmin > tmax) return -1.0f;
        return tmin;
    }

    std::string formatTimestamp(int64_t ts) {
        time_t t = static_cast<time_t>(ts);
        struct tm* tm = localtime(&t);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
        return std::string(buf);
    }

    std::string formatPrice(float price) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << price;
        return ss.str();
    }

    void renderUI() {
        ImGui::NewFrame();

        auto extent = getSwapchain().getExtent();
        float width = static_cast<float>(extent.width);
        float height = static_cast<float>(extent.height);

        renderTopBar(width);
        renderLeftPanel(height);
        renderRightPanel(width, height);

        if (m_showTradePanel) renderTradePanel(width, height);
        if (m_showPositionsPanel) renderPositionsPanel(width, height);
        if (m_showAlgoPanel) renderAlgoPanel(width, height);

        ImGui::Render();
    }

    void renderTopBar(float width) {
        ImGuiWindowFlags barFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(width, 40));
        ImGui::SetNextWindowBgAlpha(0.85f);

        if (ImGui::Begin("##TopBar", nullptr, barFlags)) {
            ImGui::SetCursorPosY(8);

            // PAPER badge
            if (m_account.paper) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "[PAPER]");
                ImGui::SameLine();
            }

            // Symbol name
            ImGui::Text("%s", m_currentSymbol.c_str());

            ImGui::SameLine(150);

            // Current price + change
            if (m_quote.current > 0) {
                ImGui::Text("$%s", formatPrice(m_quote.current).c_str());

                ImGui::SameLine();
                if (m_quote.change >= 0) {
                    ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f),
                        "+%s (+%.2f%%)", formatPrice(m_quote.change).c_str(), m_quote.changePercent);
                } else {
                    ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f),
                        "%s (%.2f%%)", formatPrice(m_quote.change).c_str(), m_quote.changePercent);
                }
            }

            // Account equity
            ImGui::SameLine(width - 400);
            ImGui::Text("Equity: $%s", formatPrice(m_account.equity).c_str());

            ImGui::SameLine(width - 200);
            if (m_loading) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Loading...");
            } else if (m_client && m_client->isConnected()) {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Connected");
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Disconnected");
            }

            ImGui::SameLine(width - 80);
            ImGui::Text("%zu candles", m_candles.size());
        }
        ImGui::End();
    }

    void renderLeftPanel(float height) {
        ImGui::SetNextWindowPos(ImVec2(0, 45));
        ImGui::SetNextWindowSize(ImVec2(220, height - 45));
        ImGui::SetNextWindowBgAlpha(0.8f);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("##LeftPanel", nullptr, flags)) {
            // Ticker search
            ImGui::Text("Symbol");
            static char searchBuf[32] = "";
            if (ImGui::InputText("##search", searchBuf, sizeof(searchBuf),
                                  ImGuiInputTextFlags_EnterReturnsTrue)) {
                std::string sym(searchBuf);
                if (!sym.empty()) {
                    for (auto& c : sym) c = toupper(c);
                    m_currentSymbol = sym;
                    requestData();
                    searchBuf[0] = '\0';
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Go")) {
                std::string sym(searchBuf);
                if (!sym.empty()) {
                    for (auto& c : sym) c = toupper(c);
                    m_currentSymbol = sym;
                    requestData();
                    searchBuf[0] = '\0';
                }
            }

            ImGui::Separator();

            // Search results
            if (m_showSearch && !m_searchResults.empty()) {
                ImGui::Text("Results:");
                for (const auto& sr : m_searchResults) {
                    std::string label = sr.symbol + " - " + sr.description;
                    if (label.length() > 30) label = label.substr(0, 27) + "...";
                    if (ImGui::Selectable(label.c_str())) {
                        m_currentSymbol = sr.symbol;
                        m_showSearch = false;
                        requestData();
                    }
                }
                ImGui::Separator();
            }

            // Timeframe buttons
            ImGui::Text("Timeframe");
            const char* resolutions[] = {"1", "5", "15", "60", "D", "W", "M"};
            const char* labels[] = {"1m", "5m", "15m", "1h", "D", "W", "M"};

            for (int i = 0; i < 7; i++) {
                if (i > 0 && i != 4) ImGui::SameLine();
                bool selected = (m_currentResolution == resolutions[i]);
                if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
                if (ImGui::Button(labels[i], ImVec2(26, 0))) {
                    m_currentResolution = resolutions[i];
                    requestData();
                }
                if (selected) ImGui::PopStyleColor();
            }

            ImGui::Separator();

            // Quick tickers (stocks + crypto)
            ImGui::Text("Watchlist");
            const char* tickers[] = {"AAPL", "TSLA", "MSFT", "GOOG", "AMZN", "NVDA", "META", "AMD"};
            for (const auto& ticker : tickers) {
                bool isCurrent = (m_currentSymbol == ticker);
                if (isCurrent) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.9f, 0.4f, 1.0f));
                if (ImGui::Selectable(ticker, isCurrent)) {
                    m_currentSymbol = ticker;
                    requestData();
                }
                if (isCurrent) ImGui::PopStyleColor();
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.2f, 1.0f), "Crypto");
            const char* cryptos[] = {"BTC/USD", "ETH/USD", "SOL/USD"};
            for (const auto& crypto : cryptos) {
                bool isCurrent = (m_currentSymbol == crypto);
                if (isCurrent) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.9f, 0.4f, 1.0f));
                // Use BTC-USD format for yfinance
                std::string displayName = crypto;
                std::string yfSymbol = displayName;
                // Replace / with - for yfinance compatibility
                for (auto& ch : yfSymbol) { if (ch == '/') ch = '-'; }
                if (ImGui::Selectable(displayName.c_str(), isCurrent)) {
                    m_currentSymbol = yfSymbol;
                    requestData();
                }
                if (isCurrent) ImGui::PopStyleColor();
            }

            ImGui::Separator();

            // Controls help
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Controls");
            ImGui::BulletText("WASD - Move/Pan");
            ImGui::BulletText("RMB + Mouse - Look/Pan");
            ImGui::BulletText("Space/Ctrl - Up/Down");
            ImGui::BulletText("LMB - Select candle");
            ImGui::BulletText("V - Ortho chart view");
            ImGui::BulletText("T - Trade panel");
            ImGui::BulletText("P - Positions panel");
            ImGui::BulletText("A - Algo panel");
            ImGui::BulletText("Scroll - Zoom (ortho)");
            ImGui::BulletText("Esc - Deselect");
        }
        ImGui::End();
    }

    void renderRightPanel(float width, float height) {
        if (m_selectedCandle < 0 || m_selectedCandle >= static_cast<int>(m_candles.size()))
            return;

        const auto& candle = m_candles[m_selectedCandle];

        float panelWidth = 200;
        ImGui::SetNextWindowPos(ImVec2(width - panelWidth, 45));
        ImGui::SetNextWindowSize(ImVec2(panelWidth, 300));
        ImGui::SetNextWindowBgAlpha(0.85f);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("##RightPanel", nullptr, flags)) {
            ImGui::Text("Candle #%d", m_selectedCandle + 1);
            ImGui::Text("%s", formatTimestamp(candle.timestamp).c_str());
            ImGui::Separator();

            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Open");
            ImGui::SameLine(80); ImGui::Text("$%s", formatPrice(candle.open).c_str());

            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "High");
            ImGui::SameLine(80); ImGui::Text("$%s", formatPrice(candle.high).c_str());

            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Low");
            ImGui::SameLine(80); ImGui::Text("$%s", formatPrice(candle.low).c_str());

            ImGui::TextColored(
                candle.bullish() ? ImVec4(0.3f, 0.9f, 0.3f, 1.0f) : ImVec4(0.9f, 0.3f, 0.3f, 1.0f),
                "Close");
            ImGui::SameLine(80); ImGui::Text("$%s", formatPrice(candle.close).c_str());

            ImGui::Separator();

            float change = candle.close - candle.open;
            float changePct = (candle.open != 0) ? (change / candle.open) * 100.0f : 0;
            if (change >= 0) {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f),
                    "Change: +$%s (+%.2f%%)", formatPrice(change).c_str(), changePct);
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f),
                    "Change: $%s (%.2f%%)", formatPrice(change).c_str(), changePct);
            }

            ImGui::Text("Volume: %lld", (long long)candle.volume);
        }
        ImGui::End();
    }

    // ============================================================
    // Trade Panel (T key)
    // ============================================================
    void renderTradePanel(float width, float height) {
        float panelW = 320;
        float panelH = 380;
        ImGui::SetNextWindowPos(ImVec2(width / 2 - panelW / 2, height / 2 - panelH / 2),
                                ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.92f);

        if (ImGui::Begin("Trade", &m_showTradePanel)) {
            // Account summary
            if (m_account.paper) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "PAPER TRADING");
            }
            ImGui::Text("Buying Power: $%s", formatPrice(m_account.buyingPower).c_str());
            ImGui::Text("Cash: $%s", formatPrice(m_account.cash).c_str());
            ImGui::Text("Equity: $%s", formatPrice(m_account.equity).c_str());
            ImGui::Separator();

            // Order form
            ImGui::Text("Symbol");
            static char orderSymBuf[32] = "";
            if (orderSymBuf[0] == '\0') {
                snprintf(orderSymBuf, sizeof(orderSymBuf), "%s", m_currentSymbol.c_str());
            }
            ImGui::InputText("##orderSym", orderSymBuf, sizeof(orderSymBuf));

            ImGui::Text("Quantity");
            static float orderQty = 1.0f;
            ImGui::InputFloat("##orderQty", &orderQty, 0.1f, 1.0f, "%.3f");

            // Order type
            static int orderTypeIdx = 0;
            ImGui::Text("Order Type");
            ImGui::RadioButton("Market", &orderTypeIdx, 0); ImGui::SameLine();
            ImGui::RadioButton("Limit", &orderTypeIdx, 1);

            static float limitPrice = 0;
            if (orderTypeIdx == 1) {
                ImGui::Text("Limit Price");
                ImGui::InputFloat("##limitPrice", &limitPrice, 0.01f, 1.0f, "%.2f");
            }

            ImGui::Separator();

            // Buy / Sell buttons
            float buttonW = (panelW - 30) / 2;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.6f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.8f, 0.3f, 1.0f));
            if (ImGui::Button("BUY", ImVec2(buttonW, 35))) {
                trading::OrderRequest ord;
                ord.symbol = std::string(orderSymBuf);
                ord.qty = orderQty;
                ord.side = "buy";
                ord.type = (orderTypeIdx == 0) ? "market" : "limit";
                if (orderTypeIdx == 1) ord.limitPrice = limitPrice;
                m_client->submitOrder(ord, [this](const trading::OrderSubmitResponse& resp) {
                    if (resp.success && resp.order.success) {
                        m_orderStatus = "Order placed: " + resp.order.status;
                    } else {
                        m_orderStatus = "Error: " + (resp.error.empty() ? resp.order.error : resp.error);
                    }
                });
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("SELL", ImVec2(buttonW, 35))) {
                trading::OrderRequest ord;
                ord.symbol = std::string(orderSymBuf);
                ord.qty = orderQty;
                ord.side = "sell";
                ord.type = (orderTypeIdx == 0) ? "market" : "limit";
                if (orderTypeIdx == 1) ord.limitPrice = limitPrice;
                m_client->submitOrder(ord, [this](const trading::OrderSubmitResponse& resp) {
                    if (resp.success && resp.order.success) {
                        m_orderStatus = "Order placed: " + resp.order.status;
                    } else {
                        m_orderStatus = "Error: " + (resp.error.empty() ? resp.order.error : resp.error);
                    }
                });
            }
            ImGui::PopStyleColor(2);

            // Status message
            if (!m_orderStatus.empty()) {
                ImGui::Separator();
                bool isError = m_orderStatus.find("Error") != std::string::npos;
                ImGui::TextColored(
                    isError ? ImVec4(0.9f, 0.3f, 0.3f, 1.0f) : ImVec4(0.3f, 0.9f, 0.3f, 1.0f),
                    "%s", m_orderStatus.c_str());
            }
        }
        ImGui::End();
    }

    // ============================================================
    // Positions Panel (P key)
    // ============================================================
    void renderPositionsPanel(float width, float height) {
        float panelW = 500;
        float panelH = 300;
        ImGui::SetNextWindowPos(ImVec2(width / 2 - panelW / 2, height - panelH - 20),
                                ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.92f);

        if (ImGui::Begin("Positions", &m_showPositionsPanel)) {
            if (m_positions.empty()) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No open positions");
            } else {
                // Table header
                ImGui::Columns(5, "posColumns");
                ImGui::SetColumnWidth(0, 80);
                ImGui::SetColumnWidth(1, 60);
                ImGui::SetColumnWidth(2, 100);
                ImGui::SetColumnWidth(3, 100);
                ImGui::SetColumnWidth(4, 120);

                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Symbol");  ImGui::NextColumn();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Qty");     ImGui::NextColumn();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Avg Entry"); ImGui::NextColumn();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Current");   ImGui::NextColumn();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "P&L");       ImGui::NextColumn();
                ImGui::Separator();

                for (const auto& pos : m_positions) {
                    ImGui::Text("%s", pos.symbol.c_str());     ImGui::NextColumn();
                    ImGui::Text("%.3f", pos.qty);              ImGui::NextColumn();
                    ImGui::Text("$%s", formatPrice(pos.avgEntryPrice).c_str()); ImGui::NextColumn();
                    ImGui::Text("$%s", formatPrice(pos.currentPrice).c_str());  ImGui::NextColumn();

                    ImVec4 plColor = pos.unrealizedPl >= 0
                        ? ImVec4(0.3f, 0.9f, 0.3f, 1.0f) : ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
                    ImGui::TextColored(plColor, "$%s (%.1f%%)",
                        formatPrice(pos.unrealizedPl).c_str(), pos.unrealizedPlPct);
                    ImGui::NextColumn();
                }
                ImGui::Columns(1);
            }
        }
        ImGui::End();
    }

    // ============================================================
    // Algo Panel (A key)
    // ============================================================
    void renderAlgoPanel(float width, float height) {
        float panelW = 400;
        float panelH = 450;
        ImGui::SetNextWindowPos(ImVec2(width - panelW - 20, 50),
                                ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.92f);

        if (ImGui::Begin("Algo Engine", &m_showAlgoPanel)) {
            // Strategy selector
            ImGui::Text("Strategy");
            if (!m_algoStrategies.empty()) {
                if (ImGui::BeginCombo("##stratCombo",
                    m_selectedStrategyIdx < static_cast<int>(m_algoStrategies.size())
                        ? m_algoStrategies[m_selectedStrategyIdx].name.c_str() : "Select...")) {
                    for (int i = 0; i < static_cast<int>(m_algoStrategies.size()); i++) {
                        bool sel = (i == m_selectedStrategyIdx);
                        if (ImGui::Selectable(m_algoStrategies[i].name.c_str(), sel)) {
                            m_selectedStrategyIdx = i;
                            // Reset params to defaults
                            m_algoParamValues.clear();
                            for (const auto& [name, val] : m_algoStrategies[i].defaultParams) {
                                m_algoParamValues[name] = std::stof(val);
                            }
                        }
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "No strategies loaded");
                if (ImGui::Button("Refresh Strategies")) {
                    m_client->fetchAlgoStrategies([this](const trading::AlgoStrategiesResponse& resp) {
                        if (resp.success) m_algoStrategies = resp.strategies;
                    });
                }
            }

            // Parameter inputs
            if (m_selectedStrategyIdx >= 0 && m_selectedStrategyIdx < static_cast<int>(m_algoStrategies.size())) {
                ImGui::Separator();
                ImGui::Text("Parameters");
                const auto& strat = m_algoStrategies[m_selectedStrategyIdx];
                for (const auto& [name, defaultVal] : strat.defaultParams) {
                    float val = m_algoParamValues.count(name) ? m_algoParamValues[name] : std::stof(defaultVal);
                    if (ImGui::InputFloat(name.c_str(), &val, 1.0f, 10.0f, "%.1f")) {
                        m_algoParamValues[name] = val;
                    }
                }
            }

            ImGui::Separator();

            // Symbol + Resolution for algo
            ImGui::Text("Symbol: %s  Resolution: %s", m_currentSymbol.c_str(), m_currentResolution.c_str());

            // Backtest button
            if (m_selectedStrategyIdx >= 0 && m_selectedStrategyIdx < static_cast<int>(m_algoStrategies.size())) {
                if (ImGui::Button("Run Backtest", ImVec2(180, 30))) {
                    std::string paramsJson = buildParamsJson();
                    m_backtestRunning = true;
                    m_client->runBacktest(
                        m_algoStrategies[m_selectedStrategyIdx].name,
                        m_currentSymbol,
                        m_currentResolution,
                        paramsJson,
                        [this](const trading::BacktestResponse& resp) {
                            m_backtestRunning = false;
                            m_lastBacktest = resp;
                            if (resp.success) {
                                // Load signals onto chart
                                m_signals = resp.signals;
                            }
                        });
                }
                ImGui::SameLine();
                if (ImGui::Button("Start Live", ImVec2(100, 30))) {
                    std::string paramsJson = buildParamsJson();
                    m_client->startAlgo(
                        m_algoStrategies[m_selectedStrategyIdx].name,
                        m_currentSymbol,
                        m_currentResolution,
                        paramsJson,
                        [this](const trading::AlgoStartResponse& resp) {
                            if (resp.success) {
                                // Trigger initial status fetch
                                m_client->fetchAlgoStatus([this](const trading::AlgoStatusResponse& r) {
                                    if (r.success) m_algoStatuses = r.algos;
                                });
                            }
                        });
                }
            }

            if (m_backtestRunning) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Running backtest...");
            }

            // Backtest results
            if (m_lastBacktest.success) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Backtest Results");

                ImVec4 pnlColor = m_lastBacktest.totalPnl >= 0
                    ? ImVec4(0.3f, 0.9f, 0.3f, 1.0f) : ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
                ImGui::TextColored(pnlColor, "P&L: $%s", formatPrice(m_lastBacktest.totalPnl).c_str());
                ImGui::Text("Trades: %d (W:%d L:%d)", m_lastBacktest.totalTrades,
                            m_lastBacktest.winningTrades, m_lastBacktest.losingTrades);
                ImGui::Text("Win Rate: %.1f%%", m_lastBacktest.winRate);
                ImGui::Text("Sharpe: %.2f", m_lastBacktest.sharpeRatio);
                ImGui::Text("Max Drawdown: %.1f%%", m_lastBacktest.maxDrawdown);
                ImGui::Text("Signals: %zu", m_lastBacktest.signals.size());
            }

            // Running algos
            if (!m_algoStatuses.empty()) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Running Algos");
                for (const auto& algo : m_algoStatuses) {
                    ImGui::PushID(algo.id.c_str());
                    ImVec4 statusColor = (algo.status == "running")
                        ? ImVec4(0.3f, 0.9f, 0.3f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
                    ImGui::TextColored(statusColor, "[%s] %s %s",
                        algo.status.c_str(), algo.strategy.c_str(), algo.symbol.c_str());
                    ImGui::SameLine();
                    ImVec4 pnlColor = algo.pnl >= 0
                        ? ImVec4(0.3f, 0.9f, 0.3f, 1.0f) : ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
                    ImGui::TextColored(pnlColor, "$%s", formatPrice(algo.pnl).c_str());
                    if (algo.status == "running") {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Stop")) {
                            m_client->stopAlgo(algo.id, [](const trading::AlgoStopResponse&) {});
                        }
                    }
                    ImGui::PopID();
                }
            }
        }
        ImGui::End();
    }

    std::string buildParamsJson() {
        std::string json = "{";
        bool first = true;
        for (const auto& [key, val] : m_algoParamValues) {
            if (!first) json += ",";
            json += "\"" + key + "\":" + std::to_string(val);
            first = false;
        }
        json += "}";
        return json;
    }

    // ============================================================
    // Member variables
    // ============================================================

    // Rendering
    std::unique_ptr<ModelRenderer> m_modelRenderer;
    ImGuiManager m_imguiManager;
    Camera m_camera;

    // Shared GPU mesh handles
    uint32_t m_bullHandle = 0;
    uint32_t m_bearHandle = 0;
    uint32_t m_wickHandle = 0;
    uint32_t m_selectedHandle = 0;
    uint32_t m_buyMarkerHandle = 0;
    uint32_t m_sellMarkerHandle = 0;

    // Market data
    std::unique_ptr<trading::MarketDataClient> m_client;
    std::string m_currentSymbol;
    std::string m_currentResolution;
    std::vector<trading::Candle> m_candles;
    trading::Quote m_quote;

    // Chart
    std::vector<CandleModel> m_candleModels;
    std::vector<glm::vec3> m_gridLines;
    std::vector<float> m_gridPrices;
    float m_chartMinPrice = 0;
    float m_chartMaxPrice = 0;

    // UI state
    int m_selectedCandle = -1;
    bool m_loading = false;
    std::string m_statusMessage;
    bool m_showSearch = false;
    std::vector<trading::SearchResult> m_searchResults;

    // Ortho view state
    bool m_orthoView = false;
    glm::vec3 m_savedCameraPos{0};
    float m_savedCameraYaw = 0;
    float m_savedCameraPitch = 0;

    // Trading state
    trading::AccountInfo m_account;
    std::vector<trading::Position> m_positions;
    std::string m_orderStatus;
    bool m_showTradePanel = false;
    bool m_showPositionsPanel = false;
    float m_accountPollTimer = 4.0f;  // Start near 5 to trigger initial fetch quickly

    // Algo state
    std::vector<trading::AlgoStrategy> m_algoStrategies;
    std::vector<trading::AlgoStatus> m_algoStatuses;
    std::vector<trading::TradeSignal> m_signals;
    trading::BacktestResponse m_lastBacktest;
    bool m_showAlgoPanel = false;
    bool m_backtestRunning = false;
    int m_selectedStrategyIdx = 0;
    std::map<std::string, float> m_algoParamValues;
    float m_algoPollTimer = 0;
};


int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "  EDEN Trading Terminal" << std::endl;
    std::cout << "  EDEN Engine" << std::endl;
    std::cout << "========================================" << std::endl;

    try {
        TradingTerminalApp app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
