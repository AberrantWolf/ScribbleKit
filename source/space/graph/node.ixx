//
// Created by Scott Harper on 2024 March 28.
//
module;

#include <vector>

export module scribble.space.graph:node;

namespace scribble::space::graph {
    class graph;

    export class node {
        std::vector<node> children_;
        friend graph;

    public:
        auto update() -> void {
            //
        }
    };
}
