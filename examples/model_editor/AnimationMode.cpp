#include "AnimationMode.hpp"
#include "Editor/SkinnedGLBLoader.hpp"

#include <nfd.h>
#include <imgui.h>
#include <tiny_gltf.h>

#include <iostream>
#include <algorithm>

using namespace eden;

AnimationMode::AnimationMode(EditorContext& ctx)
    : IEditorMode(ctx)
{
}

void AnimationMode::onActivate() {
    // Nothing special to do on activation
}

void AnimationMode::onDeactivate() {
    // Nothing special to do on deactivation
}

void AnimationMode::processInput(float deltaTime) {
    // Animation mode doesn't have special input handling beyond camera
    // Camera is handled by main editor
}

void AnimationMode::update(float deltaTime) {
    // Update animation playback
    if (m_skinnedModelHandle != UINT32_MAX) {
        if (m_animationPlaying && m_currentAnimationIndex >= 0 &&
            m_currentAnimationIndex < static_cast<int>(m_animations.size())) {
            m_animationTime += deltaTime * m_animationSpeed;
            const auto& clip = m_animations[m_currentAnimationIndex].clip;
            if (m_animationTime > clip.duration) {
                m_animationTime = std::fmod(m_animationTime, clip.duration);
            }
        }
        m_ctx.skinnedModelRenderer.updateAnimation(m_skinnedModelHandle, deltaTime);
    }
}

void AnimationMode::renderUI() {
    renderAnimationCombinerUI();
}

void AnimationMode::renderSceneOverlay(VkCommandBuffer cmd, const glm::mat4& viewProj) {
    // Render skinned model
    if (m_skinnedModelHandle != UINT32_MAX) {
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        modelMatrix = glm::rotate(modelMatrix, glm::radians(90.0f), glm::vec3(1, 0, 0));
        modelMatrix = glm::scale(modelMatrix, glm::vec3(0.012f));
        m_ctx.skinnedModelRenderer.render(cmd, viewProj, m_skinnedModelHandle, modelMatrix);
    }
}

void AnimationMode::drawOverlays(float vpX, float vpY, float vpW, float vpH) {
    // Animation mode doesn't have 2D overlays
}

void AnimationMode::renderAnimationCombinerUI() {
    ImGui::SetNextWindowPos(ImVec2(0, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 500), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Animation Combiner")) {
        // Base model section
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Base Model");
        ImGui::Separator();

        if (m_skinnedModelHandle == UINT32_MAX) {
            ImGui::TextDisabled("No model loaded");
            if (ImGui::Button("Load Base Model...", ImVec2(-1, 0))) {
                openSkinnedModelDialog();
            }
        } else {
            size_t lastSlash = m_baseModelPath.find_last_of("/\\");
            std::string filename = (lastSlash != std::string::npos) ?
                m_baseModelPath.substr(lastSlash + 1) : m_baseModelPath;
            ImGui::Text("Model: %s", filename.c_str());
            ImGui::Text("Bones: %zu", m_boneNames.size());

            // Show bone prefix
            if (!m_boneNames.empty()) {
                std::string prefix = detectBonePrefix(m_boneNames[0]);
                ImGui::Text("Bone prefix: %s", prefix.c_str());
            }
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // Animations section
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Animations");
        ImGui::Separator();

        if (ImGui::Button("Add Animation...", ImVec2(-1, 0))) {
            addAnimationDialog();
        }

        ImGui::Spacing();

        if (m_animations.empty()) {
            ImGui::TextDisabled("No animations loaded");
        } else {
            for (size_t i = 0; i < m_animations.size(); i++) {
                auto& anim = m_animations[i];
                ImGui::PushID(static_cast<int>(i));

                bool isSelected = (m_currentAnimationIndex == static_cast<int>(i));
                if (ImGui::Selectable(anim.name.c_str(), isSelected)) {
                    m_currentAnimationIndex = static_cast<int>(i);
                    m_animationTime = 0.0f;
                    // Play this animation
                    if (m_skinnedModelHandle != UINT32_MAX) {
                        m_ctx.skinnedModelRenderer.playAnimation(m_skinnedModelHandle, anim.name, true);
                    }
                }

                // Context menu for rename/delete
                if (ImGui::BeginPopupContextItem()) {
                    ImGui::InputText("Name", m_newAnimationName, sizeof(m_newAnimationName));
                    if (ImGui::Button("Rename")) {
                        if (strlen(m_newAnimationName) > 0) {
                            anim.name = m_newAnimationName;
                            m_newAnimationName[0] = '\0';
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::Separator();
                    if (ImGui::Button("Delete")) {
                        m_animations.erase(m_animations.begin() + i);
                        if (m_currentAnimationIndex >= static_cast<int>(m_animations.size())) {
                            m_currentAnimationIndex = static_cast<int>(m_animations.size()) - 1;
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // Show duration
                ImGui::SameLine(200);
                ImGui::TextDisabled("%.1fs", anim.clip.duration);

                ImGui::PopID();
            }
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // Playback controls
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Playback");
        ImGui::Separator();

        if (m_currentAnimationIndex >= 0 && m_currentAnimationIndex < static_cast<int>(m_animations.size())) {
            const auto& clip = m_animations[m_currentAnimationIndex].clip;

            if (ImGui::Button(m_animationPlaying ? "Pause" : "Play")) {
                m_animationPlaying = !m_animationPlaying;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                m_animationTime = 0.0f;
            }

            ImGui::SliderFloat("Time", &m_animationTime, 0.0f, clip.duration, "%.2f");
            ImGui::SliderFloat("Speed", &m_animationSpeed, 0.1f, 2.0f, "%.1fx");
        } else {
            ImGui::TextDisabled("Select an animation to preview");
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // Export section
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Export");
        ImGui::Separator();

        bool canExport = m_skinnedModelHandle != UINT32_MAX && !m_animations.empty();
        if (!canExport) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Export Combined GLB...", ImVec2(-1, 30))) {
            exportCombinedGLB();
        }

        if (!canExport) {
            ImGui::EndDisabled();
            ImGui::TextDisabled("Load a model and add animations first");
        }
    }
    ImGui::End();

    // Camera controls window
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(m_ctx.window.getWidth()) - 220.0f, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(220, 100), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Camera")) {
        ImGui::SliderFloat("Speed", &m_ctx.cameraSpeed, 0.01f, 0.2f, "%.3f");
        ImGui::Separator();
        ImGui::TextWrapped("RMB + WASD to navigate");
    }
    ImGui::End();
}

void AnimationMode::openSkinnedModelDialog() {
    nfdchar_t* outPath = nullptr;
    nfdfilteritem_t filters[1] = {{"GLB Models", "glb"}};

    nfdresult_t result = NFD_OpenDialog(&outPath, filters, 1, nullptr);

    if (result == NFD_OKAY) {
        loadSkinnedModel(outPath);
        NFD_FreePath(outPath);
    }
}

void AnimationMode::loadSkinnedModel(const std::string& path) {
    try {
        SkinnedLoadResult result = SkinnedGLBLoader::load(path);

        if (!result.success) {
            std::cerr << "Failed to load skinned model: " << result.error << std::endl;
            return;
        }

        if (result.meshes.empty()) {
            std::cerr << "No meshes found in model: " << path << std::endl;
            return;
        }

        // Clean up old model
        if (m_skinnedModelHandle != UINT32_MAX) {
            m_ctx.skinnedModelRenderer.destroyModel(m_skinnedModelHandle);
        }
        m_animations.clear();
        m_currentAnimationIndex = -1;
        m_boneNames.clear();

        // Cache bone names before moving skeleton
        if (result.skeleton) {
            for (const auto& bone : result.skeleton->bones) {
                m_boneNames.push_back(bone.name);
            }
        }

        // Get mesh data
        const auto& mesh = result.meshes[0];

        // Create new model
        m_skinnedModelHandle = m_ctx.skinnedModelRenderer.createModel(
            mesh.vertices,
            mesh.indices,
            std::move(result.skeleton),
            std::move(result.animations),
            mesh.textureData.empty() ? nullptr : mesh.textureData.data(),
            mesh.textureWidth,
            mesh.textureHeight
        );

        m_baseModelPath = path;

        // Get animations back from the renderer to store locally
        auto* modelData = m_ctx.skinnedModelRenderer.getModelData(m_skinnedModelHandle);
        if (modelData) {
            for (const auto& clip : modelData->animations) {
                StoredAnimation anim;
                anim.name = clip.name;
                anim.sourceFile = path;
                anim.clip = clip;
                m_animations.push_back(anim);
            }
        }

        if (!m_animations.empty()) {
            m_currentAnimationIndex = 0;
            m_ctx.skinnedModelRenderer.playAnimation(m_skinnedModelHandle, m_animations[0].name, true);
        }

        std::cout << "Loaded skinned model: " << path << std::endl;
        std::cout << "  Bones: " << m_boneNames.size() << std::endl;
        std::cout << "  Animations: " << m_animations.size() << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Failed to load skinned model: " << e.what() << std::endl;
    }
}

void AnimationMode::addAnimationDialog() {
    if (m_skinnedModelHandle == UINT32_MAX) {
        std::cerr << "Load a base model first!" << std::endl;
        return;
    }

    nfdchar_t* outPath = nullptr;
    nfdfilteritem_t filters[1] = {{"GLB Animation", "glb"}};

    nfdresult_t result = NFD_OpenDialog(&outPath, filters, 1, nullptr);

    if (result == NFD_OKAY) {
        addAnimationFromFile(outPath);
        NFD_FreePath(outPath);
    }
}

std::string AnimationMode::detectBonePrefix(const std::string& boneName) {
    // Common Mixamo prefixes
    if (boneName.find("mixamorig:") == 0) return "mixamorig:";
    if (boneName.find("mixamorig1:") == 0) return "mixamorig1:";
    if (boneName.find("mixamorig2:") == 0) return "mixamorig2:";
    if (boneName.find("mixamorig3:") == 0) return "mixamorig3:";
    if (boneName.find("mixamorig4:") == 0) return "mixamorig4:";
    if (boneName.find("mixamorig5:") == 0) return "mixamorig5:";
    if (boneName.find("mixamorig6:") == 0) return "mixamorig6:";
    if (boneName.find("mixamorig7:") == 0) return "mixamorig7:";
    if (boneName.find("mixamorig8:") == 0) return "mixamorig8:";
    if (boneName.find("mixamorig9:") == 0) return "mixamorig9:";

    // No prefix or unknown
    return "";
}

std::string AnimationMode::remapBoneName(const std::string& srcName, const std::string& srcPrefix, const std::string& dstPrefix) {
    if (srcPrefix.empty() || srcName.find(srcPrefix) != 0) {
        return srcName; // No remapping needed
    }
    // Replace prefix
    return dstPrefix + srcName.substr(srcPrefix.length());
}

void AnimationMode::addAnimationFromFile(const std::string& path) {
    try {
        SkinnedLoadResult result = SkinnedGLBLoader::load(path);

        if (!result.success) {
            std::cerr << "Failed to load animation: " << result.error << std::endl;
            return;
        }

        if (result.animations.empty()) {
            std::cerr << "No animations found in file: " << path << std::endl;
            return;
        }

        // Detect bone prefixes
        std::string srcPrefix = "";
        std::string dstPrefix = "";

        // Get source bone names
        std::vector<std::string> srcBoneNames;
        if (result.skeleton) {
            for (const auto& bone : result.skeleton->bones) {
                srcBoneNames.push_back(bone.name);
            }
        }

        if (!srcBoneNames.empty()) {
            srcPrefix = detectBonePrefix(srcBoneNames[0]);
        }
        if (!m_boneNames.empty()) {
            dstPrefix = detectBonePrefix(m_boneNames[0]);
        }

        bool needsRemap = !srcPrefix.empty() && !dstPrefix.empty() && srcPrefix != dstPrefix;

        if (needsRemap) {
            std::cout << "Remapping bones: " << srcPrefix << " -> " << dstPrefix << std::endl;
        }

        // Build bone name map from source to destination indices
        std::map<int, int> boneIndexMap;
        for (size_t i = 0; i < srcBoneNames.size(); i++) {
            std::string srcBone = srcBoneNames[i];
            std::string dstBone = needsRemap ? remapBoneName(srcBone, srcPrefix, dstPrefix) : srcBone;

            // Find matching bone in our skeleton
            for (size_t j = 0; j < m_boneNames.size(); j++) {
                if (m_boneNames[j] == dstBone) {
                    boneIndexMap[static_cast<int>(i)] = static_cast<int>(j);
                    break;
                }
            }
        }

        std::cout << "Mapped " << boneIndexMap.size() << "/" << srcBoneNames.size() << " bones" << std::endl;

        // Extract filename for default animation name
        size_t lastSlash = path.find_last_of("/\\");
        size_t lastDot = path.find_last_of(".");
        std::string filename = path.substr(lastSlash + 1, lastDot - lastSlash - 1);

        // Add each animation from the file
        for (auto& clip : result.animations) {
            // Remap bone indices in the animation
            AnimationClip remappedClip;
            remappedClip.name = clip.name.empty() ? filename : clip.name;
            remappedClip.duration = clip.duration;

            for (const auto& srcChannel : clip.channels) {
                auto it = boneIndexMap.find(srcChannel.boneIndex);
                if (it != boneIndexMap.end()) {
                    AnimationChannel dstChannel = srcChannel;
                    dstChannel.boneIndex = it->second;
                    remappedClip.channels.push_back(dstChannel);
                }
            }

            // Make name unique
            std::string baseName = remappedClip.name;
            int counter = 1;
            while (animationNameExists(remappedClip.name)) {
                remappedClip.name = baseName + "_" + std::to_string(counter++);
            }

            StoredAnimation anim;
            anim.name = remappedClip.name;
            anim.sourceFile = path;
            anim.clip = remappedClip;
            m_animations.push_back(anim);

            // Register with renderer
            m_ctx.skinnedModelRenderer.addAnimation(m_skinnedModelHandle, remappedClip);

            std::cout << "Added animation: " << remappedClip.name
                      << " (" << remappedClip.channels.size() << " channels, "
                      << remappedClip.duration << "s)" << std::endl;
        }

        // Select first new animation
        if (m_currentAnimationIndex < 0) {
            m_currentAnimationIndex = 0;
            m_ctx.skinnedModelRenderer.playAnimation(m_skinnedModelHandle, m_animations[0].name, true);
        }

    } catch (const std::exception& e) {
        std::cerr << "Failed to add animation: " << e.what() << std::endl;
    }
}

bool AnimationMode::animationNameExists(const std::string& name) {
    for (const auto& anim : m_animations) {
        if (anim.name == name) return true;
    }
    return false;
}

void AnimationMode::exportCombinedGLB() {
    if (m_skinnedModelHandle == UINT32_MAX || m_animations.empty()) {
        std::cerr << "Nothing to export!" << std::endl;
        return;
    }

    nfdchar_t* outPath = nullptr;
    nfdfilteritem_t filters[1] = {{"GLB Model", "glb"}};

    nfdresult_t result = NFD_SaveDialog(&outPath, filters, 1, nullptr, "combined.glb");

    if (result == NFD_OKAY) {
        exportToGLB(outPath);
        NFD_FreePath(outPath);
    }
}

void AnimationMode::exportToGLB(const std::string& path) {
    // Re-load the base model to get the original glTF data
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    if (!loader.LoadBinaryFromFile(&model, &err, &warn, m_baseModelPath)) {
        std::cerr << "Failed to reload base model for export: " << err << std::endl;
        return;
    }

    // Clear existing animations
    model.animations.clear();

    // Add our combined animations
    for (const auto& storedAnim : m_animations) {
        tinygltf::Animation gltfAnim;
        gltfAnim.name = storedAnim.name;

        const auto& clip = storedAnim.clip;

        for (const auto& channel : clip.channels) {
            // Find the node index for this bone
            int nodeIndex = -1;
            if (channel.boneIndex >= 0 && channel.boneIndex < static_cast<int>(m_boneNames.size())) {
                const std::string& boneName = m_boneNames[channel.boneIndex];
                for (size_t i = 0; i < model.nodes.size(); i++) {
                    if (model.nodes[i].name == boneName) {
                        nodeIndex = static_cast<int>(i);
                        break;
                    }
                }
            }

            if (nodeIndex < 0) continue;

            // Add translation channel if we have position data
            if (!channel.positions.empty() && !channel.positionTimes.empty()) {
                tinygltf::AnimationSampler sampler;
                sampler.interpolation = "LINEAR";

                // Create time accessor
                int timeAccessorIdx = createFloatAccessor(model, channel.positionTimes, TINYGLTF_TYPE_SCALAR);
                sampler.input = timeAccessorIdx;

                // Create position accessor - flatten vec3 array
                std::vector<float> positions;
                for (const auto& pos : channel.positions) {
                    positions.push_back(pos.x);
                    positions.push_back(pos.y);
                    positions.push_back(pos.z);
                }
                int posAccessorIdx = createFloatAccessor(model, positions, TINYGLTF_TYPE_VEC3);
                sampler.output = posAccessorIdx;

                int sampIdx = static_cast<int>(gltfAnim.samplers.size());
                gltfAnim.samplers.push_back(sampler);

                tinygltf::AnimationChannel gltfChannel;
                gltfChannel.sampler = sampIdx;
                gltfChannel.target_node = nodeIndex;
                gltfChannel.target_path = "translation";
                gltfAnim.channels.push_back(gltfChannel);
            }

            // Add rotation channel if we have rotation data
            if (!channel.rotations.empty() && !channel.rotationTimes.empty()) {
                tinygltf::AnimationSampler sampler;
                sampler.interpolation = "LINEAR";

                int timeAccessorIdx = createFloatAccessor(model, channel.rotationTimes, TINYGLTF_TYPE_SCALAR);
                sampler.input = timeAccessorIdx;

                // Create rotation accessor - flatten quat array (x,y,z,w)
                std::vector<float> rotations;
                for (const auto& rot : channel.rotations) {
                    rotations.push_back(rot.x);
                    rotations.push_back(rot.y);
                    rotations.push_back(rot.z);
                    rotations.push_back(rot.w);
                }
                int rotAccessorIdx = createFloatAccessor(model, rotations, TINYGLTF_TYPE_VEC4);
                sampler.output = rotAccessorIdx;

                int sampIdx = static_cast<int>(gltfAnim.samplers.size());
                gltfAnim.samplers.push_back(sampler);

                tinygltf::AnimationChannel gltfChannel;
                gltfChannel.sampler = sampIdx;
                gltfChannel.target_node = nodeIndex;
                gltfChannel.target_path = "rotation";
                gltfAnim.channels.push_back(gltfChannel);
            }

            // Add scale channel if we have scale data
            if (!channel.scales.empty() && !channel.scaleTimes.empty()) {
                tinygltf::AnimationSampler sampler;
                sampler.interpolation = "LINEAR";

                int timeAccessorIdx = createFloatAccessor(model, channel.scaleTimes, TINYGLTF_TYPE_SCALAR);
                sampler.input = timeAccessorIdx;

                // Create scale accessor - flatten vec3 array
                std::vector<float> scales;
                for (const auto& scl : channel.scales) {
                    scales.push_back(scl.x);
                    scales.push_back(scl.y);
                    scales.push_back(scl.z);
                }
                int scaleAccessorIdx = createFloatAccessor(model, scales, TINYGLTF_TYPE_VEC3);
                sampler.output = scaleAccessorIdx;

                int sampIdx = static_cast<int>(gltfAnim.samplers.size());
                gltfAnim.samplers.push_back(sampler);

                tinygltf::AnimationChannel gltfChannel;
                gltfChannel.sampler = sampIdx;
                gltfChannel.target_node = nodeIndex;
                gltfChannel.target_path = "scale";
                gltfAnim.channels.push_back(gltfChannel);
            }
        }

        if (!gltfAnim.channels.empty()) {
            model.animations.push_back(gltfAnim);
        }
    }

    // Write the combined GLB
    tinygltf::TinyGLTF writer;
    if (!writer.WriteGltfSceneToFile(&model, path, true, true, true, true)) {
        std::cerr << "Failed to write GLB: " << path << std::endl;
        return;
    }

    std::cout << "Exported combined GLB: " << path << std::endl;
    std::cout << "  Animations: " << model.animations.size() << std::endl;
}

int AnimationMode::createFloatAccessor(tinygltf::Model& model, const std::vector<float>& data, int type) {
    // Create buffer view
    tinygltf::BufferView bufferView;
    bufferView.buffer = 0;
    bufferView.byteOffset = model.buffers[0].data.size();
    bufferView.byteLength = data.size() * sizeof(float);
    bufferView.target = 0; // Not a vertex/index buffer

    // Append data to buffer
    const unsigned char* dataPtr = reinterpret_cast<const unsigned char*>(data.data());
    model.buffers[0].data.insert(model.buffers[0].data.end(), dataPtr, dataPtr + bufferView.byteLength);

    int bufferViewIdx = static_cast<int>(model.bufferViews.size());
    model.bufferViews.push_back(bufferView);

    // Create accessor
    tinygltf::Accessor accessor;
    accessor.bufferView = bufferViewIdx;
    accessor.byteOffset = 0;
    accessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    accessor.type = type;

    if (type == TINYGLTF_TYPE_SCALAR) {
        accessor.count = data.size();
        // Set min/max for time values
        float minVal = *std::min_element(data.begin(), data.end());
        float maxVal = *std::max_element(data.begin(), data.end());
        accessor.minValues = {minVal};
        accessor.maxValues = {maxVal};
    } else if (type == TINYGLTF_TYPE_VEC3) {
        accessor.count = data.size() / 3;
    } else if (type == TINYGLTF_TYPE_VEC4) {
        accessor.count = data.size() / 4;
    }

    int accessorIdx = static_cast<int>(model.accessors.size());
    model.accessors.push_back(accessor);

    return accessorIdx;
}
