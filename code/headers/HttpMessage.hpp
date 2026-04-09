
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "Socket.hpp"

namespace argb
{

    class HttpMessage
    {
    public:

        using Id = Socket::Handle;

    protected:

        using Buffer     = std::vector<char>;
        using Dictionary = std::map<std::string_view, std::string_view>;

    protected:

        Buffer     header;
        Dictionary headers;
        Buffer     body;

    public:

        HttpMessage ()
        {
        }

        HttpMessage(HttpMessage && other) noexcept
            : header (std::move (other.header ))
            , headers(std::move (other.headers))
            , body   (std::move (other.body   ))
        {
        }

        HttpMessage & operator = (HttpMessage && other) noexcept
        {
            if (this != &other)
            {
                header  = std::move (other.header );
                headers = std::move (other.headers);
                body    = std::move (other.body   );
            }

            return *this;
        }

        HttpMessage(const HttpMessage & ) = delete;
        HttpMessage & operator = (const HttpMessage & ) = delete;

    public:

        std::string_view get_header (std::string_view name) const
        {
            auto   item  = headers.find (name);
            return item != headers. end () ? item->second : std::string_view{};
        }

        std::span<const char> get_serialized_header () const
        {
            return header;
        }

        std::span<const char> get_body () const
        {
            return body;
        }

    };

}
