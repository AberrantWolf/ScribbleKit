//
// Created by Scott Harper on 5/1/2023.
//

#include <iostream>

#include <fmt/ostream.h>

import scribble.graphics.dx12;
import scribble.app;

int main()
{
    fmt::println("Starting the app");
    auto app = scribble::scribble_app{};

    auto graphics_interface = scribble::dx12::make_graphics_api();
    app.set_graphics(std::move(graphics_interface));

    const auto exit_result = app.run();
    if (exit_result != scribble::scribble_app::Result::success) {
        fmt::print(std::cerr, "Did not exit correctly");
    }

    return 0;
}
