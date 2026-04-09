
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <HttpResponse.hpp>
#include <NetworkException.hpp>

namespace argb
{

    HttpResponse::Serializer::Serializer(HttpResponse & target_response) : response(&target_response)
    {
        status_serialized = false;
        header_serialized = false;

        response->header .clear ();
        response->headers.clear ();
        response->body   .clear ();

        serialize_protocol_version ();
    }

    HttpResponse::Serializer & HttpResponse::Serializer::status (int status)
    {
        if (status_serialized) throw NetworkException("Status has already been serialized.", -1);
        if (header_serialized) throw NetworkException("Cannot set status after headers have been serialized.", -1);
        
        if (status < 100 || status > 599)
        {
            throw NetworkException("Invalid HTTP status code.", -1);
        }

        response->status = status;

        serialize_status  (status);
        
        status_serialized = true;

        return *this;
    }

    HttpResponse::Serializer & HttpResponse::Serializer::end_header ()
    {
        if (not status_serialized) throw NetworkException("Status must be serialized before ending headers.", -1);
        if (not header_serialized) throw NetworkException("At least one header must be serialized before ending headers.", -1);

        static constexpr std::string_view crlf = "\r\n";

        response->header.insert (response->header.end (), crlf.begin (), crlf.end ());

        return *this;
    }

    HttpResponse::Serializer & HttpResponse::Serializer::header (std::string_view name, std::string_view value)
    {
        if (not status_serialized) throw NetworkException("Status must be serialized before headers.", -1);

        serialize_header (name, value);

        header_serialized = true;

        return *this;
    }

    HttpResponse::Serializer & HttpResponse::Serializer::body (std::string_view content)
    {
        response->body.insert (response->body.end (), content.begin (), content.end ());
        return *this;
    }

    HttpResponse::Serializer & HttpResponse::Serializer::body (std::span<const char> content)
    {
        response->body.insert (response->body.end (), content.begin (), content.end ());
        return *this;
    }

    void HttpResponse::Serializer::serialize_protocol_version ()
    {
        static constexpr std::string_view protocol_version = "HTTP/1.1 ";

        response->header.insert (response->header.end (), protocol_version.begin (), protocol_version.end ());
    }

    void HttpResponse::Serializer::serialize_status (int status)
    {
        const char status_code_string[4] = 
        {
            static_cast<char> ('0' + (status / 100) % 10),
            static_cast<char> ('0' + (status /  10) % 10),
            static_cast<char> ('0' + (status      ) % 10),
        };

        static constexpr std::string_view space = " ";
        static constexpr std::string_view crlf  = "\r\n";

        const std::string_view status_code   (status_code_string, 3);
        const std::string_view status_message(get_status_message ());

        response->header.insert (response->header.end (), status_code   .begin (), status_code   .end ());
        response->header.insert (response->header.end (), space         .begin (), space         .end ());
        response->header.insert (response->header.end (), status_message.begin (), status_message.end ());
        response->header.insert (response->header.end (), crlf          .begin (), crlf          .end ());
    }

    void HttpResponse::Serializer::serialize_header (std::string_view name, std::string_view value)
    {
        static constexpr std::string_view colon = ": ";
        static constexpr std::string_view crlf  = "\r\n";

        response->header.insert (response->header.end (), name .begin (), name .end ());
        response->header.insert (response->header.end (), colon.begin (), colon.end ());
        response->header.insert (response->header.end (), value.begin (), value.end ());
        response->header.insert (response->header.end (), crlf .begin (), crlf .end ());
    }

    std::string_view HttpResponse::Serializer::get_status_message () const
    {
        switch (response->status)
        {
            case 100: return "Continue";
            case 101: return "Switching Protocols";
            case 102: return "Processing";
            case 103: return "Early Hints";
            case 200: return "OK";
            case 201: return "Created";
            case 202: return "Accepted";
            case 203: return "Non-Authoritative Information";
            case 204: return "No Content";
            case 205: return "Reset Content";
            case 206: return "Partial Content";
            case 207: return "Multi-Status";
            case 208: return "Already Reported";
            case 226: return "IM Used";
            case 300: return "Multiple Choices";
            case 301: return "Moved Permanently";
            case 302: return "Found";
            case 303: return "See Other";
            case 304: return "Not Modified";
            case 305: return "Use Proxy";
            case 306: return "Switch Proxy";
            case 307: return "Temporary Redirect";
            case 308: return "Permanent Redirect";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 402: return "Payment Required";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 406: return "Not Acceptable";
            case 407: return "Proxy Authentication Required";
            case 408: return "Request Timeout";
            case 409: return "Conflict";
            case 410: return "Gone";
            case 411: return "Length Required";
            case 412: return "Precondition Failed";
            case 413: return "Payload Too Large";
            case 414: return "URI Too Long";
            case 415: return "Unsupported Media Type";
            case 416: return "Range Not Satisfiable";
            case 417: return "Expectation Failed";
            case 418: return "I'm a teapot";
            case 421: return "Misdirected Request";
            case 422: return "Unprocessable Entity";
            case 423: return "Locked";
            case 424: return "Failed Dependency";
            case 426: return "Upgrade Required";
            case 428: return "Precondition Required";
            case 429: return "Too Many Requests";
            case 431: return "Request Header Fields Too Largev";
            case 444: return "No Response";
            case 451: return "Unavailable For Legal Reasons";
            case 499: return "Client Closed Request";
            case 500: return "Internal Server Error";
            case 501: return "Not Implemented";
            case 502: return "Bad Gateway";
            case 503: return "Service Unavailable";
            case 504: return "Gateway Timeout";
            case 505: return "HTTP Version Not Supported";
            case 506: return "Variant Also Negotiates";
            case 507: return "Insufficient Storage";
            case 508: return "Loop Detected";
            case 510: return "Not Extended";
            case 511: return "Network Authentication Required";
            case 520: return "Unknown Error";
        }

        return "-";
    }

}
