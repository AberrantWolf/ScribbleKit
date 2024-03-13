//
// Created by Scott Harper on 5/1/2023.
//

//#include "IApplication.h"
//
//MainFunc() {
//    scribble::InitApplication(APP_ARGS);
//    return scribble::GetApplication()->Run();
//}

#include <GLFW/glfw3.h>
#include <cstdio>
#include <iostream>

import GraphicsApiInterface;

void error_callback(int error, const char* description)
{
    fprintf(stderr, "Error: %s\n", description);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

int main()
{
    if (!glfwInit())
    {
        // Initialization failed
        return -1;
    }
    printf("Initialized\n");

    glfwSetErrorCallback(error_callback);

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(720, 480, "Hi There", NULL, NULL);
    if (!window)
    {
        return -2;
    }
    printf("Window created\n");

    glfwSetKeyCallback(window, key_callback);

    scribble::GraphicsApiInterface* pInterface = scribble::MakeGraphicsApi();
    printf("Api interface created\n");

    try {
        const auto assets_path = std::string("shaders");
        pInterface->Init(window, assets_path);
    } catch (const std::exception& e) {
        std::cout << "Error initializing graphics api: " << e.what() << std::endl;
        return -3;
    }
    printf("Api interface initialized\n");

    while (!glfwWindowShouldClose(window))
    {
        // Keep running
        glfwPollEvents();

        pInterface->Update();
        pInterface->Render();
    }

    // Shutdown
    pInterface->Destroy();
    delete pInterface;
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
