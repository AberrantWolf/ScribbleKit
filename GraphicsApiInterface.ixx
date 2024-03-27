//
// Created by lareo on 2024 March 13.
//

module;

#include <string>
#include <GLFW/glfw3.h>

export module GraphicsApiInterface;

namespace scribble {
    export class GraphicsApiInterface {
    public:
        virtual ~GraphicsApiInterface() = default;

        virtual void Init(GLFWwindow *p_window, const std::string &assets_path) = 0;

        virtual void Update() = 0;

        virtual void Render() = 0;

        virtual void Destroy() = 0;
    };
}
