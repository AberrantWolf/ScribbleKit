//
// Created by lareo on 2024 March 27.
//
module;

#include <GLFW/glfw3.h>
#include <cstdio>
#include <iostream>

export module App;

import GraphicsApiInterface;

namespace scribble {
    export class App {
        std::unique_ptr<GraphicsApiInterface> m_graphics;
        GLFWwindow* m_pwindow = nullptr;

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

        auto set_graphics(std::unique_ptr<GraphicsApiInterface>&& graphics) -> void {
            // TODO: If the graphics was already like... doing stuff, we maybe should uh... transfer the stuff?
            m_graphics = std::move(graphics);
        }

        auto run() -> Result {
            if (!glfwInit()) {
                return Result::InitializationFailed;
            }

            glfwSetErrorCallback(error_callback);

            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            m_pwindow = glfwCreateWindow(720, 480, "Hi There", nullptr, nullptr);
            if (!m_pwindow)
            {
                return Result::WindowCreationFailed;
            }
            glfwSetKeyCallback(m_pwindow, key_callback);


            try {
                const auto assets_path = std::string("shaders");
                m_graphics->Init(m_pwindow, assets_path);
            } catch (const std::exception& e) {
                std::cout << "Error initializing graphics api: " << e.what() << std::endl;
                return Result::InitializeGraphicsApiFailed;
            }

            while (!glfwWindowShouldClose(m_pwindow))
            {
                // Keep running
                glfwPollEvents();

                m_graphics->Update();
                m_graphics->Render();
            }

            m_graphics->Destroy();
            glfwDestroyWindow(m_pwindow);
            glfwTerminate();

            return Result::Success;
        }
    };
}
