//
// Created by Scott Harper on 2024 March 13.
//

module;

#include <MathLib.h>

export module types;

namespace scribble {
    export struct double2 {
        double u;
        double v;
    };
    export using ::double3;
    export using ::double4;


    export struct float2 {
        float u;
        float v;
    };
    export using ::float3;
    export using ::float4;
}
