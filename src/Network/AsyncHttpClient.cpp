#include "AsyncHttpClient.hpp"

// Disable SSL support - we only need local HTTP
#define CPPHTTPLIB_NO_EXCEPTIONS
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#undef CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib.h>

#include <iostream>
#include <fstream>

namespace eden {

AsyncHttpClient::AsyncHttpClient(const std::string& baseUrl)
    : m_baseUrl(baseUrl) {
}

AsyncHttpClient::~AsyncHttpClient() {
    stop();
}

void AsyncHttpClient::start() {
    if (m_running) return;

    m_running = true;
    m_workerThread = std::thread(&AsyncHttpClient::workerThread, this);
}

void AsyncHttpClient::stop() {
    m_running = false;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void AsyncHttpClient::workerThread() {
    while (m_running) {
        Request request;
        bool hasRequest = false;

        {
            std::lock_guard<std::mutex> lock(m_requestMutex);
            if (!m_requestQueue.empty()) {
                request = std::move(m_requestQueue.front());
                m_requestQueue.pop();
                hasRequest = true;
            }
        }

        if (hasRequest) {
            Response response = executeRequest(request);

            {
                std::lock_guard<std::mutex> lock(m_responseMutex);
                m_responseQueue.push({response, request.callback});
            }
        } else {
            // Sleep a bit to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

AsyncHttpClient::Response AsyncHttpClient::executeRequest(const Request& request) {
    Response response;

    // Parse host and port from base URL
    std::string host = m_baseUrl;
    int port = 8080;

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

    httplib::Client client(host, port);
    client.set_connection_timeout(5);  // 5 seconds to connect
    client.set_read_timeout(60);       // 60 seconds for LLM response

    httplib::Result result;

    if (!request.uploadFilePath.empty()) {
        // Multipart file upload
        std::ifstream file(request.uploadFilePath, std::ios::binary);
        if (file) {
            std::string fileData((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
            file.close();

            // Extract filename from path
            std::string filename = request.uploadFilePath;
            auto slashPos = filename.find_last_of('/');
            if (slashPos != std::string::npos) filename = filename.substr(slashPos + 1);

            httplib::MultipartFormDataItems items = {
                {"audio", fileData, filename, "audio/wav"}
            };
            result = client.Post(request.path, items);
        }
    } else if (request.method == "GET") {
        result = client.Get(request.path);
    } else if (request.method == "POST") {
        result = client.Post(request.path, request.body, "application/json");
    }

    if (result) {
        response.success = (result->status >= 200 && result->status < 300);
        response.statusCode = result->status;
        response.body = result->body;
        m_connected = true;
    } else {
        response.success = false;
        response.error = "Connection failed";
        m_connected = false;
    }

    return response;
}

void AsyncHttpClient::sendChatMessage(const std::string& sessionId, const std::string& message,
                                       const std::string& npcName, const std::string& npcPersonality,
                                       int beingType, ResponseCallback callback) {
    nlohmann::json body;
    if (!sessionId.empty()) {
        body["session_id"] = sessionId;
    }
    body["message"] = message;
    body["npc_name"] = npcName;
    body["npc_personality"] = npcPersonality;
    body["being_type"] = beingType;

    Request request;
    request.method = "POST";
    request.path = "/chat";
    request.body = body.dump();
    request.callback = callback;

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_requestQueue.push(std::move(request));
    }
}

void AsyncHttpClient::sendChatMessageWithPerception(const std::string& sessionId, const std::string& message,
                                                     const std::string& npcName, const std::string& npcPersonality,
                                                     int beingType, const PerceptionData& perception,
                                                     ResponseCallback callback) {
    nlohmann::json body;
    if (!sessionId.empty()) {
        body["session_id"] = sessionId;
    }
    body["message"] = message;
    body["npc_name"] = npcName;
    body["npc_personality"] = npcPersonality;
    body["being_type"] = beingType;
    body["perception"] = perception.toJson();

    Request request;
    request.method = "POST";
    request.path = "/chat";
    request.body = body.dump();
    request.callback = callback;

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_requestQueue.push(std::move(request));
    }
}

void AsyncHttpClient::createSession(const std::string& npcName, const std::string& npcPersonality,
                                     int beingType, ResponseCallback callback) {
    nlohmann::json body;
    body["npc_name"] = npcName;
    body["npc_personality"] = npcPersonality;
    body["being_type"] = beingType;

    Request request;
    request.method = "POST";
    request.path = "/session/new";
    request.body = body.dump();
    request.callback = callback;

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_requestQueue.push(std::move(request));
    }
}

void AsyncHttpClient::endSession(const std::string& sessionId, ResponseCallback callback) {
    Request request;
    request.method = "POST";
    request.path = "/session/" + sessionId + "/end";
    request.body = "{}";
    request.callback = callback;

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_requestQueue.push(std::move(request));
    }
}

void AsyncHttpClient::sendHeartbeat(const std::string& sessionId, const std::string& npcName,
                                     int beingType, const PerceptionData& perception,
                                     ResponseCallback callback) {
    nlohmann::json body;
    if (!sessionId.empty()) {
        body["session_id"] = sessionId;
    }
    body["npc_name"] = npcName;
    body["being_type"] = beingType;
    body["perception"] = perception.toJson();

    Request request;
    request.method = "POST";
    request.path = "/heartbeat";
    request.body = body.dump();
    request.callback = callback;

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_requestQueue.push(std::move(request));
    }
}

void AsyncHttpClient::requestTTS(const std::string& text, const std::string& voice,
                                  ResponseCallback callback, const std::string& rate,
                                  bool robot) {
    nlohmann::json body;
    body["text"] = text;
    body["voice"] = voice;
    if (!rate.empty()) {
        body["rate"] = rate;
    }
    if (robot) {
        body["robot"] = true;
    }

    Request request;
    request.method = "POST";
    request.path = "/tts";
    request.body = body.dump();
    request.callback = callback;

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_requestQueue.push(std::move(request));
    }
}

void AsyncHttpClient::requestSTT(const std::string& wavFilePath, ResponseCallback callback) {
    Request request;
    request.method = "POST";
    request.path = "/stt";
    request.uploadFilePath = wavFilePath;
    request.callback = callback;

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_requestQueue.push(std::move(request));
    }
}

void AsyncHttpClient::checkHealth(ResponseCallback callback) {
    Request request;
    request.method = "GET";
    request.path = "/health";
    request.callback = callback;

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_requestQueue.push(std::move(request));
    }
}

void AsyncHttpClient::pollResponses() {
    std::queue<CompletedRequest> toProcess;

    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        std::swap(toProcess, m_responseQueue);
    }

    while (!toProcess.empty()) {
        auto& completed = toProcess.front();
        if (completed.callback) {
            completed.callback(completed.response);
        }
        toProcess.pop();
    }
}

} // namespace eden
