//
// Created by Scott Harper on 2024 March 28.
//
module;

#include <vector>

export module scribble.graph:node;

namespace scribble::graph {
    class Graph;

    export class Node {
        std::vector<Node> m_children;
        friend Graph;

    public:
        auto update() -> void {
            //
        }
    };
}
