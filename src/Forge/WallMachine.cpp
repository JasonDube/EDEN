#include "Forge/WallMachine.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <chrono>

namespace eden {

const std::vector<std::string> WallMachine::s_emptyLog;

WallMachine::~WallMachine() {
    cancelAll();
}

// Minimal JSON parsing for the schema file
bool WallMachine::loadSchema(const std::string& jsonPath) {
    std::ifstream f(jsonPath);
    if (!f.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    // Extract simple string fields
    auto extractString = [&](const std::string& key) -> std::string {
        std::string needle = "\"" + key + "\"";
        auto pos = content.find(needle);
        if (pos == std::string::npos) return "";
        auto colon = content.find(':', pos);
        if (colon == std::string::npos) return "";
        auto q1 = content.find('"', colon + 1);
        auto q2 = content.find('"', q1 + 1);
        if (q1 == std::string::npos || q2 == std::string::npos) return "";
        return content.substr(q1 + 1, q2 - q1 - 1);
    };

    m_schema.name = extractString("name");
    m_schema.displayName = extractString("display_name");

    // Parse inputs array
    auto inputsPos = content.find("\"inputs\"");
    if (inputsPos != std::string::npos) {
        auto arrStart = content.find('[', inputsPos);
        auto arrEnd = content.find(']', arrStart);
        if (arrStart != std::string::npos && arrEnd != std::string::npos) {
            std::string arr = content.substr(arrStart, arrEnd - arrStart + 1);
            size_t pos = 0;
            while (pos < arr.size()) {
                auto objStart = arr.find('{', pos);
                if (objStart == std::string::npos) break;
                auto objEnd = arr.find('}', objStart);
                if (objEnd == std::string::npos) break;
                std::string obj = arr.substr(objStart, objEnd - objStart + 1);
                pos = objEnd + 1;

                MachineSchema::ParamDef pd;
                // Extract name
                auto nPos = obj.find("\"name\"");
                if (nPos != std::string::npos) {
                    auto c = obj.find(':', nPos);
                    auto q1 = obj.find('"', c + 1);
                    auto q2 = obj.find('"', q1 + 1);
                    if (q1 != std::string::npos && q2 != std::string::npos)
                        pd.name = obj.substr(q1 + 1, q2 - q1 - 1);
                }
                // Extract type
                auto tPos = obj.find("\"type\"");
                if (tPos != std::string::npos) {
                    auto c = obj.find(':', tPos);
                    auto q1 = obj.find('"', c + 1);
                    auto q2 = obj.find('"', q1 + 1);
                    if (q1 != std::string::npos && q2 != std::string::npos)
                        pd.type = obj.substr(q1 + 1, q2 - q1 - 1);
                }
                pd.required = (obj.find("\"required\":true") != std::string::npos ||
                               obj.find("\"required\": true") != std::string::npos);
                m_schema.inputs.push_back(pd);
            }
        }
    }

    // Parse outputs array (simple string array)
    auto outputsPos = content.find("\"outputs\"");
    if (outputsPos != std::string::npos) {
        auto arrStart = content.find('[', outputsPos);
        auto arrEnd = content.find(']', arrStart);
        if (arrStart != std::string::npos && arrEnd != std::string::npos) {
            std::string arr = content.substr(arrStart + 1, arrEnd - arrStart - 1);
            size_t pos = 0;
            while (pos < arr.size()) {
                auto q1 = arr.find('"', pos);
                if (q1 == std::string::npos) break;
                auto q2 = arr.find('"', q1 + 1);
                if (q2 == std::string::npos) break;
                m_schema.outputs.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
                pos = q2 + 1;
            }
        }
    }

    std::cout << "[WallMachine] Loaded schema: " << m_schema.name
              << " (" << m_schema.displayName << ")"
              << " inputs=" << m_schema.inputs.size()
              << " outputs=" << m_schema.outputs.size() << std::endl;
    return true;
}

WallMachineInstance& WallMachine::getOrCreateInstance(int machineFrameIndex) {
    for (auto& inst : m_instances) {
        if (inst.machineFrameIndex == machineFrameIndex) return inst;
    }
    m_instances.push_back({});
    m_instances.back().machineFrameIndex = machineFrameIndex;
    return m_instances.back();
}

WallMachineInstance* WallMachine::findInstance(int machineFrameIndex) {
    for (auto& inst : m_instances) {
        if (inst.machineFrameIndex == machineFrameIndex) return &inst;
    }
    return nullptr;
}

bool WallMachine::execute(int machineFrameIndex, const std::string& imagePath,
                          const std::string& outputDir,
                          const std::vector<WallMachineInstance::CollectedParam>& params) {
    auto& inst = getOrCreateInstance(machineFrameIndex);
    if (inst.state == MachineState::Running) {
        std::cerr << "[WallMachine] Machine already running" << std::endl;
        return false;
    }

    inst.state = MachineState::Running;
    inst.logLines.clear();
    inst.outputGLBPath.clear();
    inst.collectedParams = params;
    inst.logLines.push_back("Starting Hunyuan3D generation...");

    // Encode image
    std::string imageBase64 = Hunyuan3DClient::base64EncodeFile(imagePath);
    if (imageBase64.empty()) {
        inst.state = MachineState::Error;
        inst.logLines.push_back("ERROR: Failed to read input image: " + imagePath);
        return false;
    }

    inst.logLines.push_back("Image encoded (" + std::to_string(imageBase64.size() / 1024) + " KB)");

    // Extract optional params
    int steps = 5;
    int octreeRes = 256;
    int maxFaces = 10000;
    bool texture = true;
    for (const auto& p : params) {
        if (p.name == "steps") steps = std::stoi(p.value);
        else if (p.name == "octree_resolution") octreeRes = std::stoi(p.value);
        else if (p.name == "max_faces") maxFaces = std::stoi(p.value);
        else if (p.name == "texture") texture = (p.value == "true" || p.value == "1");
    }

    // Start generation
    std::string uid = m_client.startGeneration("", imageBase64, steps, octreeRes,
                                                5.0f, maxFaces, texture);
    if (uid.empty()) {
        inst.state = MachineState::Error;
        inst.logLines.push_back("ERROR: Failed to start generation (server unreachable?)");
        return false;
    }

    inst.jobUID = uid;
    inst.logLines.push_back("Job UID: " + uid);

    // Store output directory for later
    std::filesystem::create_directories(outputDir);

    return true;
}

std::vector<int> WallMachine::poll() {
    std::vector<int> completed;

    for (auto& inst : m_instances) {
        if (inst.state != MachineState::Running) continue;
        if (inst.jobUID.empty()) continue;

        // Fetch log lines
        int logIndex = static_cast<int>(inst.logLines.size());
        std::vector<std::string> newLines;
        m_client.fetchLog(logIndex, newLines);
        for (auto& line : newLines) {
            inst.logLines.push_back(line);
        }

        // Check status
        std::string glbBase64;
        std::string status = m_client.checkStatus(inst.jobUID, glbBase64);

        if (status == "completed") {
            // Determine output path
            std::string outputDir;
            // Use the collected image path's directory as output location
            for (const auto& p : inst.collectedParams) {
                if (p.name == "image") {
                    std::filesystem::path imgPath(p.value);
                    outputDir = imgPath.parent_path().string();
                    break;
                }
            }
            if (outputDir.empty()) {
                const char* home = getenv("HOME");
                outputDir = home ? std::string(home) + "/Desktop" : "/tmp";
            }

            // Generate unique filename
            auto now = std::chrono::system_clock::now();
            auto epoch = now.time_since_epoch();
            auto secs = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
            std::string outPath = outputDir + "/ai_generated_" + std::to_string(secs) + ".glb";

            if (!glbBase64.empty() && Hunyuan3DClient::base64DecodeToFile(glbBase64, outPath)) {
                inst.outputGLBPath = outPath;
                inst.state = MachineState::Complete;
                inst.logLines.push_back("Complete! Output: " + outPath);
                completed.push_back(inst.machineFrameIndex);
                std::cout << "[WallMachine] Generation complete: " << outPath << std::endl;
            } else {
                inst.state = MachineState::Error;
                inst.logLines.push_back("ERROR: Failed to decode/save output GLB");
            }
        } else if (status == "error") {
            inst.state = MachineState::Error;
            inst.logLines.push_back("ERROR: Generation failed on server");
        }
    }

    return completed;
}

MachineState WallMachine::getState(int machineFrameIndex) const {
    for (const auto& inst : m_instances) {
        if (inst.machineFrameIndex == machineFrameIndex) return inst.state;
    }
    return MachineState::Idle;
}

const std::vector<std::string>& WallMachine::getLogLines(int machineFrameIndex) const {
    for (const auto& inst : m_instances) {
        if (inst.machineFrameIndex == machineFrameIndex) return inst.logLines;
    }
    return s_emptyLog;
}

std::string WallMachine::getOutputPath(int machineFrameIndex) const {
    for (const auto& inst : m_instances) {
        if (inst.machineFrameIndex == machineFrameIndex) return inst.outputGLBPath;
    }
    return "";
}

void WallMachine::cancelAll() {
    for (auto& inst : m_instances) {
        if (inst.state == MachineState::Running) {
            inst.state = MachineState::Error;
            inst.logLines.push_back("Cancelled");
        }
    }
}

} // namespace eden
