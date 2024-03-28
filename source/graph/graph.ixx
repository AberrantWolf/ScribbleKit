//
// Created by Scott Harper on 2024 March 28.
//
module;

#include <vector>

export module scribble.graph;

export import :node;

namespace scribble::graph {
    export class Graph {
        std::vector<Node> m_nodes;

        static auto doUpdate(std::vector<Node> &nodes) -> void {
            for (auto &node: nodes) {
                node.update();

                if (node.m_children.size() > 0) {
                    doUpdate(node.m_children);
                }
            }
        }

    public:
        auto update() -> void {
            doUpdate(m_nodes);
        }
    };
}
