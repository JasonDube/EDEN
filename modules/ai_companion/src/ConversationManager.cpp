#include "ConversationManager.hpp"

// Disable SSL - we only need local HTTP
#define CPPHTTPLIB_NO_EXCEPTIONS
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#undef CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <thread>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <iostream>
#include <chrono>

namespace eden {
namespace ai {

// Internal implementation
class ConversationManager::Impl {
public:
    std::string backendUrl;
    std::string host;
    int port = 8080;
    
    std::unique_ptr<httplib::Client> client;
    std::thread workerThread;
    std::atomic<bool> running{false};
    
    std::mutex requestMutex;
    std::mutex responseMutex;
    std::mutex sessionMutex;
    
    struct Request {
        std::string sessionId;
        std::string message;
        std::string npcName;
        BeingType beingType;
        std::string customPersonality;
        ResponseCallback callback;
    };
    
    struct Response {
        std::string sessionId;
        std::string response;
        bool success;
        ResponseCallback callback;
    };
    
    std::queue<Request> requestQueue;
    std::queue<Response> responseQueue;
    std::unordered_map<std::string, ConversationSession> sessions;
    
    ConnectionCallback connectionCallback;
    
    static std::vector<ChatMessage> emptyHistory;
    
    void parseUrl(const std::string& url) {
        host = url;
        port = 8080;
        
        // Remove http:// prefix
        if (host.substr(0, 7) == "http://") {
            host = host.substr(7);
        }
        
        // Extract port if specified
        auto colonPos = host.find(':');
        if (colonPos != std::string::npos) {
            port = std::stoi(host.substr(colonPos + 1));
            host = host.substr(0, colonPos);
        }
    }
    
    void workerLoop() {
        while (running) {
            Request request;
            bool hasRequest = false;
            
            {
                std::lock_guard<std::mutex> lock(requestMutex);
                if (!requestQueue.empty()) {
                    request = std::move(requestQueue.front());
                    requestQueue.pop();
                    hasRequest = true;
                }
            }
            
            if (hasRequest) {
                Response response;
                response.sessionId = request.sessionId;
                response.callback = request.callback;
                
                try {
                    nlohmann::json reqJson;
                    reqJson["session_id"] = request.sessionId.empty() ? nullptr : nlohmann::json(request.sessionId);
                    reqJson["message"] = request.message;
                    reqJson["npc_name"] = request.npcName;
                    reqJson["being_type"] = static_cast<int>(request.beingType);
                    if (!request.customPersonality.empty()) {
                        reqJson["npc_personality"] = request.customPersonality;
                    }
                    
                    auto result = client->Post("/chat", reqJson.dump(), "application/json");
                    
                    if (result && result->status >= 200 && result->status < 300) {
                        auto respJson = nlohmann::json::parse(result->body);
                        response.response = respJson.value("response", "...");
                        response.sessionId = respJson.value("session_id", request.sessionId);
                        response.success = true;
                    } else {
                        response.response = "(Connection error)";
                        response.success = false;
                    }
                } catch (const std::exception& e) {
                    response.response = "(Error: " + std::string(e.what()) + ")";
                    response.success = false;
                }
                
                {
                    std::lock_guard<std::mutex> lock(responseMutex);
                    responseQueue.push(std::move(response));
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
};

std::vector<ChatMessage> ConversationManager::Impl::emptyHistory;

ConversationManager::ConversationManager()
    : m_impl(std::make_unique<Impl>()) {
}

ConversationManager::~ConversationManager() {
    shutdown();
}

void ConversationManager::initialize(const std::string& backendUrl) {
    m_impl->backendUrl = backendUrl;
    m_impl->parseUrl(backendUrl);
    
    m_impl->client = std::make_unique<httplib::Client>(m_impl->host, m_impl->port);
    m_impl->client->set_connection_timeout(5);
    m_impl->client->set_read_timeout(60);
    
    // Test connection
    auto result = m_impl->client->Get("/health");
    m_connected = result && result->status == 200;
    
    if (m_connected) {
        std::cout << "[AICompanion] Connected to backend at " << backendUrl << std::endl;
    } else {
        std::cout << "[AICompanion] Backend not available at " << backendUrl << std::endl;
    }
    
    // Start worker thread
    m_impl->running = true;
    m_impl->workerThread = std::thread(&Impl::workerLoop, m_impl.get());
}

void ConversationManager::shutdown() {
    m_impl->running = false;
    if (m_impl->workerThread.joinable()) {
        m_impl->workerThread.join();
    }
    m_impl->client.reset();
    m_connected = false;
}

void ConversationManager::update(float /*deltaTime*/) {
    // Process responses
    std::lock_guard<std::mutex> lock(m_impl->responseMutex);
    while (!m_impl->responseQueue.empty()) {
        auto response = std::move(m_impl->responseQueue.front());
        m_impl->responseQueue.pop();
        
        // Update session
        {
            std::lock_guard<std::mutex> sessionLock(m_impl->sessionMutex);
            auto it = m_impl->sessions.find(response.sessionId);
            if (it != m_impl->sessions.end()) {
                it->second.waitingForResponse = false;
                it->second.history.push_back({
                    it->second.npcName,
                    response.response,
                    false,
                    0.0f // TODO: track time
                });
            }
        }
        
        // Invoke callback
        if (response.callback) {
            response.callback(response.response, response.success);
        }
    }
}

std::string ConversationManager::startConversation(const std::string& npcName,
                                                    BeingType beingType,
                                                    const std::string& customPersonality) {
    // Generate session ID
    std::string sessionId = npcName + "_" + std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count());
    
    ConversationSession session;
    session.sessionId = sessionId;
    session.npcName = npcName;
    session.beingType = beingType;
    session.isActive = true;
    
    {
        std::lock_guard<std::mutex> lock(m_impl->sessionMutex);
        m_impl->sessions[sessionId] = std::move(session);
    }
    
    std::cout << "[AICompanion] Started conversation with " << npcName 
              << " (type: " << getBeingTypeName(beingType) << ")" << std::endl;
    
    return sessionId;
}

void ConversationManager::endConversation(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(m_impl->sessionMutex);
    auto it = m_impl->sessions.find(sessionId);
    if (it != m_impl->sessions.end()) {
        it->second.isActive = false;
        std::cout << "[AICompanion] Ended conversation: " << sessionId << std::endl;
    }
}

void ConversationManager::sendMessage(const std::string& sessionId,
                                       const std::string& message,
                                       ResponseCallback callback) {
    std::lock_guard<std::mutex> sessionLock(m_impl->sessionMutex);
    auto it = m_impl->sessions.find(sessionId);
    if (it == m_impl->sessions.end()) {
        if (callback) callback("(Invalid session)", false);
        return;
    }
    
    // Add player message to history
    it->second.history.push_back({"You", message, true, 0.0f});
    it->second.waitingForResponse = true;
    
    // Queue request
    Impl::Request request;
    request.sessionId = sessionId;
    request.message = message;
    request.npcName = it->second.npcName;
    request.beingType = it->second.beingType;
    request.callback = callback;
    
    {
        std::lock_guard<std::mutex> lock(m_impl->requestMutex);
        m_impl->requestQueue.push(std::move(request));
    }
}

const std::vector<ChatMessage>& ConversationManager::getHistory(const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(m_impl->sessionMutex);
    auto it = m_impl->sessions.find(sessionId);
    if (it != m_impl->sessions.end()) {
        return it->second.history;
    }
    return Impl::emptyHistory;
}

bool ConversationManager::isWaitingForResponse(const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(m_impl->sessionMutex);
    auto it = m_impl->sessions.find(sessionId);
    return it != m_impl->sessions.end() && it->second.waitingForResponse;
}

ConversationSession* ConversationManager::getActiveSession() {
    std::lock_guard<std::mutex> lock(m_impl->sessionMutex);
    for (auto& [id, session] : m_impl->sessions) {
        if (session.isActive) return &session;
    }
    return nullptr;
}

const ConversationSession* ConversationManager::getActiveSession() const {
    return const_cast<ConversationManager*>(this)->getActiveSession();
}

void ConversationManager::setConnectionCallback(ConnectionCallback callback) {
    m_impl->connectionCallback = callback;
}

} // namespace ai
} // namespace eden
