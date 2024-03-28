//
// Created by Scott Harper on 2024 March 13.
//

module;

#include <string>
#include <GLFW/glfw3.h>

export module scribble.graphics;

namespace scribble {
    export class graphics_interface {
    public:
        virtual ~graphics_interface() = default;

        virtual void init(GLFWwindow *p_window, const std::string &assets_path) = 0;

        virtual void update() = 0;

        virtual void render() = 0;

        virtual void destroy() = 0;
    };
}
