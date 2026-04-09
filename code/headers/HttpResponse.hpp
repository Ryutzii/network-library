
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include "HttpMessage.hpp"

namespace argb
{

    class HttpResponse : public HttpMessage
    {
    public:

        class Parser
        {
        };

        class Serializer
        {
            HttpResponse * response;

            bool status_serialized;
            bool header_serialized;

        public:

            Serializer(HttpResponse & target_response);

            Serializer & status     (int status);
            Serializer & header     (std::string_view name, std::string_view value);
            Serializer & end_header ();
            Serializer & body       (std::string_view content);
            Serializer & body       (std::span<const char> content);

        private:

            void serialize_protocol_version ();
            void serialize_status (int status);
            void serialize_header (std::string_view name, std::string_view value);

            std::string_view get_status_message () const;
        };

    private:

        int status;

    public:

        HttpResponse() : status{}
        {
        }

        HttpResponse(int given_status) : status(given_status)
        {
        }

    public:

        int get_status () const
        {
            return status;
        }

    };

}
