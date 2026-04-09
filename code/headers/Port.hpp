
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <cstdint>

namespace argb
{

    class Port
    {
        uint16_t value;

    public:

        Port() : value{}
        {
        }

        Port(uint16_t given_port) : value(given_port)
        {
        }

        auto operator <=> (const Port & other) const = default;

        explicit operator uint16_t () const
        {
            return value;
        }

    };

}
