
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <Address.hpp>
#include <Port.hpp>

namespace argb
{

    struct Endpoint final 
    {
        Address data;
        Port      port;

        bool operator == (const Endpoint & other) const = default;
    };

}
