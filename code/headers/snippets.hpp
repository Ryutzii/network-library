
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <chrono>
#include <charconv>

namespace argb
{
        
    using Timestamp = std::chrono::steady_clock::time_point;

    inline Timestamp now ()
    {
        return std::chrono::steady_clock::now ();
    }

    static constexpr std::errc no_error = std::errc{};

    template<typename TYPE>
    inline std::optional<TYPE> to (std::string_view string)
    {
        TYPE  value;
        auto [pointer, error] = std::from_chars (string.data (), string.data () + string.size (), value);

        if (error == no_error)
        {
            return value;
        }
        else
            return std::nullopt;
    }

}
