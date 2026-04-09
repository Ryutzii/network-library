
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <cstddef>
#include "HttpMessage.hpp"

namespace argb
{

    class HttpRequest : public HttpMessage
    {
    public:

        enum Method
        {
            UNDEFINED,
            OPTIONS,
            HEAD,
            GET,
            POST,
            PUT,
            LINK,
            UNLINK,
            DELETE,
            TRACE,
        };

    public:

        class Parser
        {

            HttpRequest * request;

            bool   header_complete    = false;
            bool   chunked_transfer   = false;
            size_t expected_body_size = 0;

        public:

            Parser(HttpRequest & target_request) : request(&target_request)
            {
            }

            bool parse (std::span<const std::byte> chunk);

        private:

            void parse_header ();
            void parse_request_line  (std::string_view line);
            void parse_uri           (std::string_view uri);
            void parse_query         (std::string_view query_string);
            void parse_header_fields (std::string_view fields);
            void translate_method    (std::string_view method_string);
        };

        class Serializer
        {
        };

    private:

        std::string_view protocol;
        Method           method;
        std::string_view path;
        std::string_view fragment;
        Dictionary       query;

    public:

        HttpRequest()
        {
            method = UNDEFINED;
        }

    public:

        std::string_view get_protocol () const
        {
            return protocol;
        }

        Method get_method () const
        {
            return method;
        }

        std::string_view get_path () const
        {
            return path;
        }

        bool has_fragment () const
        {
            return not fragment.empty ();
        }

        std::string_view get_fragment () const
        {
            return fragment;
        }

        std::string_view get_query_parameter (std::string_view key) const
        {
            auto   item  = query.find (key);
            return item != query. end () ? item->second : std::string_view{};
        }

    };

}
