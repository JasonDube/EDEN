#include <eden/Eden.hpp>
#include <iostream>

int main() {
    try {
        eden::Core engine;
        engine.init({
            .title = "EDEN - Spinning Triangle",
            .width = 800,
            .height = 600
        });

        // Create a colorful triangle
        auto triangle = engine.createMesh({
            .vertices = {
                {0.0f, -0.5f},   // Top
                {0.5f, 0.5f},    // Bottom right
                {-0.5f, 0.5f}    // Bottom left
            },
            .colors = {
                {1.0f, 0.0f, 0.0f},  // Red
                {0.0f, 1.0f, 0.0f},  // Green
                {0.0f, 0.0f, 1.0f}   // Blue
            }
        });

        // Run the engine with an update callback
        engine.run([&](float deltaTime) {
            // Rotate 90 degrees per second
            triangle->rotate(deltaTime * 90.0f);
        });

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
