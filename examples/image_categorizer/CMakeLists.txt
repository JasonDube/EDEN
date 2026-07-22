add_executable(image_categorizer main.cpp)

target_link_libraries(image_categorizer PRIVATE eden)

# Need internal Renderer/* headers (VulkanApplicationBase, VulkanContext, ImGuiManager).
target_include_directories(image_categorizer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
