//
// Created by Scott Harper on 5/1/2023.
//

#ifndef SCRIBBLEKIT_GRAPHICSAPIINTERFACE_H
#define SCRIBBLEKIT_GRAPHICSAPIINTERFACE_H

#include <GLFW/glfw3.h>

#include <string>

namespace scribble {

    template<typename T>
    void ThrowIfFailed(T value, const std::string& err_message)
    {
        if (value != S_OK) {
            throw std::runtime_error(err_message + " " + std::to_string(value));
        }
    }

    template<typename T>
    void ThrowIfFailed(T value) {
        ThrowIfFailed(value, "Unspecified failure");
    }

    class GraphicsApiInterface {
    public:
        virtual ~GraphicsApiInterface() = default;

        virtual void Init(GLFWwindow* p_window, std::string assets_path) = 0;
        virtual void Update() = 0;
        virtual void Render() = 0;
        virtual void Destroy() = 0;
    };

    extern GraphicsApiInterface* MakeGraphicsApi();

} // scribble

#endif //SCRIBBLEKIT_GRAPHICSAPIINTERFACE_H
