//
// Created by Scott Harper on 2024 March 27.
//
module;

#include <GLFW/glfw3.h>
#include <cstdio>
#include <iostream>

export module scribble.App;

import scribble.graphics;

namespace scribble {
    export class scribble_app {
        std::unique_ptr<graphics_interface> graphics_{};
        GLFWwindow* p_window_ = nullptr;

        static auto error_callback(int error, const char* description) -> void
        {
            fprintf(stderr, "Error: %s\n", description);
        }

        static auto key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) -> void
        {
            if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
                glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

    public:
        enum class Result {
            Success = 0,
            InitializationFailed,
            WindowCreationFailed,
            InitializeGraphicsApiFailed,
        };

        auto set_graphics(std::unique_ptr<graphics_interface>&& graphics) -> void {
            // TODO: If the graphics was already like... doing stuff, we maybe should uh... transfer the stuff?
            graphics_ = std::move(graphics);
        }

        auto run() -> Result {
            if (!glfwInit()) {
                return Result::InitializationFailed;
            }

            glfwSetErrorCallback(error_callback);

            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            p_window_ = glfwCreateWindow(720, 480, "Hi There", nullptr, nullptr);
            if (!p_window_)
            {
                return Result::WindowCreationFailed;
            }
            glfwSetKeyCallback(p_window_, key_callback);


            try {
                const auto assets_path = std::string("shaders");
                graphics_->init(p_window_, assets_path);
            } catch (const std::exception& e) {
                std::cout << "Error initializing graphics api: " << e.what() << std::endl;
                return Result::InitializeGraphicsApiFailed;
            }

            while (!glfwWindowShouldClose(p_window_))
            {
                // Keep running
                glfwPollEvents();

                graphics_->update();
                graphics_->render();
            }

            graphics_->destroy();
            glfwDestroyWindow(p_window_);
            glfwTerminate();

            return Result::Success;
        }
    };
}
