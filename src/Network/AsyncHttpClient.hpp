#pragma once

#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>

namespace eden {

/**
 * Visible object data from scan cone perception
 */
struct VisibleObject {
    std::string name;
    std::string type;           // "cube", "cylinder", "model", etc.
    float distance;             // Distance in world units
    float angle;                // Angle from forward direction (degrees)
    std::string bearing;        // "ahead", "left", "right", "behind"
    float posX = 0, posY = 0, posZ = 0;  // World position of object
    std::string beingType;      // "human", "robot", etc. if sentient
    bool isSentient = false;
    std::string description;    // Optional description (e.g. "timber board: 6x6x2m")
};

/**
 * Perception data from scan cone
 */
struct PerceptionData {
    float posX = 0, posY = 0, posZ = 0;       // NPC position
    float facingX = 0, facingY = 0, facingZ = 1; // NPC facing direction
    float fov = 120.0f;                        // Field of view (degrees)
    float range = 50.0f;                       // Scan range (world units)
    std::vector<VisibleObject> visibleObjects;
    
    // Convert to JSON for transmission
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["position"] = {posX, posY, posZ};
        j["facing"] = {facingX, facingY, facingZ};
        j["fov"] = fov;
        j["range"] = range;
        
        nlohmann::json objects = nlohmann::json::array();
        for (const auto& obj : visibleObjects) {
            nlohmann::json objJson;
            objJson["name"] = obj.name;
            objJson["type"] = obj.type;
            objJson["distance"] = obj.distance;
            objJson["angle"] = obj.angle;
            objJson["bearing"] = obj.bearing;
            objJson["posX"] = obj.posX;
            objJson["posY"] = obj.posY;
            objJson["posZ"] = obj.posZ;
            objJson["being_type"] = obj.beingType;
            objJson["is_sentient"] = obj.isSentient;
            if (!obj.description.empty()) {
                objJson["description"] = obj.description;
            }
            objects.push_back(objJson);
        }
        j["visible_objects"] = objects;
        return j;
    }
};

/**
 * Async HTTP client for communicating with the AI backend.
 * Runs requests on a background thread to avoid blocking game rendering.
 */
class AsyncHttpClient {
public:
    struct Response {
        bool success = false;
        int statusCode = 0;
        std::string body;
        std::string error;
    };

    using ResponseCallback = std::function<void(const Response&)>;

    AsyncHttpClient(const std::string& baseUrl = "http://localhost:8080");
    ~AsyncHttpClient();

    // Start the background worker thread
    void start();

    // Stop the background worker thread
    void stop();

    // Check if connected to backend
    bool isConnected() const { return m_connected; }

    // Chat API (basic)
    void sendChatMessage(const std::string& sessionId, const std::string& message,
                         const std::string& npcName, const std::string& npcPersonality,
                         int beingType, ResponseCallback callback);
    
    // Chat API with perception data
    void sendChatMessageWithPerception(const std::string& sessionId, const std::string& message,
                                        const std::string& npcName, const std::string& npcPersonality,
                                        int beingType, const PerceptionData& perception,
                                        ResponseCallback callback);

    // Create a new session
    void createSession(const std::string& npcName, const std::string& npcPersonality,
                       int beingType, ResponseCallback callback);

    // End a session
    void endSession(const std::string& sessionId, ResponseCallback callback);

    // Heartbeat (passive perception for EDEN companions)
    void sendHeartbeat(const std::string& sessionId, const std::string& npcName,
                       int beingType, const PerceptionData& perception,
                       ResponseCallback callback);

    // Text-to-speech: POST text, get audio bytes back in response.body
    void requestTTS(const std::string& text, const std::string& voice,
                    ResponseCallback callback, const std::string& rate = "",
                    bool robot = false);

    // Speech-to-text: upload WAV file, get transcription in response.body (JSON)
    void requestSTT(const std::string& wavFilePath, ResponseCallback callback);

    // Health check
    void checkHealth(ResponseCallback callback);

    // Process completed requests (call from main thread)
    void pollResponses();

private:
    struct Request {
        std::string method;
        std::string path;
        std::string body;
        std::string uploadFilePath;  // if non-empty, do multipart file upload instead
        ResponseCallback callback;
    };

    struct CompletedRequest {
        Response response;
        ResponseCallback callback;
    };

    void workerThread();
    Response executeRequest(const Request& request);

    std::string m_baseUrl;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};
    std::thread m_workerThread;

    std::mutex m_requestMutex;
    std::queue<Request> m_requestQueue;

    std::mutex m_responseMutex;
    std::queue<CompletedRequest> m_responseQueue;
};

} // namespace eden
