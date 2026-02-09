# AI Companion Module for EDEN
# Provides pluggable AI conversation system

# Module library
add_library(ai_companion STATIC
    src/ConversationManager.cpp
    src/AICompanionModule.cpp
)

target_include_directories(ai_companion PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Need access to httplib from parent project
target_include_directories(ai_companion PRIVATE
    ${httplib_SOURCE_DIR}
)

# Dependencies
target_link_libraries(ai_companion PUBLIC
    glm::glm
    imgui
    nlohmann_json::nlohmann_json
)

# C++17 required
target_compile_features(ai_companion PUBLIC cxx_std_17)

# Alias for cleaner usage
add_library(eden::ai_companion ALIAS ai_companion)
