
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <string_view>

namespace argb
{

    struct MimeType
    {

        const char * string{};

    public:

        MimeType() = default;

        MimeType(const char * const given_content_type) : string(given_content_type) 
        {
        }

    public:

        static MimeType from (const std::string_view & path_or_file_extension);

    };

}
