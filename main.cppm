//
// Created by Scott Harper on 5/1/2023.
//

#include <iostream>

import GraphicsApiInterface;
import InterfaceDX12;
import App;

int main()
{
    auto app = scribble::App{};

    auto graphicsInterface = scribble::dx12::MakeGraphicsApi();
    app.set_graphics(std::move(graphicsInterface));

    const auto exitResult = app.run();
    if (exitResult != scribble::App::Result::Success) {
        std::cerr << "Did not exit correctly" << std::endl;
    }

    return 0;
}
