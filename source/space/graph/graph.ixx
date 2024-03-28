//
// Created by Scott Harper on 2024 March 28.
//
module;

#include <vector>

export module scribble.space.graph;
export import :node;

import scribble.space;

namespace scribble::space::graph {
    export class graph final : public space_partition {
        std::vector<node> nodes_;

        static auto do_update(std::vector<node> &nodes) -> void {
            for (auto &node: nodes) {
                node.update();

                if (node.children_.size() > 0) {
                    do_update(node.children_);
                }
            }
        }

    public:
        void update() override {
            do_update(nodes_);
        }
    };
}
