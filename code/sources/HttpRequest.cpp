
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <HttpRequest.hpp>
#include <snippets.hpp>

namespace argb
{

    bool HttpRequest::Parser::parse (std::span<const std::byte> chunk)
    {
        const auto data = reinterpret_cast<const char *>(chunk.data ());
        const auto size = chunk.size ();

        if (not header_complete)
        {
            size_t previous_size = request->header.size ();

            // Append the new chunk to the header buffer:

            request->header.insert (request->header.end (), data, data + size);

            // Check if the header is complete by looking for the "\r\n\r\n" sequence, which indicates the end of the header section:

            constexpr std::string_view delimiter{ "\r\n\r\n" };

            std::string_view view(request->header.data (), request->header.size ());

            size_t search_start = previous_size >= delimiter.size ()
                ? previous_size - delimiter.size () + 1
                : 0;

            auto delimiter_location = view.find (delimiter, search_start);

            if (delimiter_location == std::string_view::npos)
            {
                return false;                                       // Header not complete yet, wait for more data.
            }

            header_complete = true;

            // Extract the body (if any) that may have been included in the same chunk after the header:

            size_t body_offset = delimiter_location + delimiter.size ();

            if (body_offset < request->header.size ())
            {
                request->body.assign (request->header.begin () + body_offset, request->header.end ());
            }

            // Resize the header buffer to contain only the header part, discarding any body data that was included in the same chunk:

            request->header.resize (body_offset);

            // Now that we have the complete header, we can parse it to extract the protocol, method, path, query, and headers:

            parse_header ();

            auto content_length = request->get_header ("Content-Length");

            if (not content_length.empty ())
            {
                auto content_length_value = to<int> (content_length);

                if (content_length_value && *content_length_value >= 0)
                {
                    expected_body_size = static_cast<size_t> (*content_length_value);
                }
                else
                {
                    return true;            // Por el momento se descarta el body si Content-Length no es un número válido
                }
            }
            else
            {
                auto transfer_encoding = request->get_header ("Transfer-Encoding");

                if (transfer_encoding == "chunked")
                {
                    chunked_transfer = true;
                }
                else
                {
                    return true;
                }
            }
        }
        else
        {
            request->body.insert (request->body.end (), data, data + size);
        }

        if (chunked_transfer)
        {
            // Falta implementar la lógica de parsing de chunked transfer encoding.
            // Por ahora simplemente se asume que el body termina al recibir "0\r\n\r\n":

            std::string_view body_view (request->body.data (), request->body.size ());

            return body_view.ends_with ("0\r\n\r\n");
        }
        else
        {
            return request->body.size () >= expected_body_size;
        }
    }

    void HttpRequest::Parser::parse_header ()
    {
        std::string_view view (request->header.data (), request->header.size ());

        auto line_end = view.find ("\r\n");

        if (line_end == std::string_view::npos) return;

        parse_request_line  (view.substr (0, line_end));
        parse_header_fields (view.substr (line_end + 2));
    }

    void HttpRequest::Parser::parse_request_line (std::string_view line)
    {
        // Expected format: METHOD SP URI SP HTTP-VERSION

        auto method_end = line.find (' ');

        if (method_end == std::string_view::npos) return;

        translate_method (line.substr (0, method_end));

        auto uri_start = method_end + 1;
        auto uri_end   = line.find (' ', uri_start);

        if (uri_end == std::string_view::npos) return;

        parse_uri (line.substr (uri_start, uri_end - uri_start));

        request->protocol = line.substr (uri_end + 1);
    }

    void HttpRequest::Parser::parse_uri (std::string_view uri)
    {
        auto fragment_pos = uri.find ('#');

        if (fragment_pos != std::string_view::npos)
        {
            request->fragment = uri.substr (fragment_pos + 1);
            uri = uri.substr (0, fragment_pos);
        }

        auto query_pos = uri.find ('?');

        if (query_pos != std::string_view::npos)
        {
            parse_query (uri.substr (query_pos + 1));
            uri = uri.substr (0, query_pos);
        }

        request->path = uri;
    }

    void HttpRequest::Parser::parse_query (std::string_view query_string)
    {
        while (not query_string.empty ())
        {
            auto separator = query_string.find ('&');
            auto pair      = query_string.substr (0, separator);

            auto equals = pair.find ('=');

            if (equals != std::string_view::npos)
            {
                request->query[pair.substr (0, equals)] = pair.substr (equals + 1);
            }
            else
            {
                request->query[pair] = std::string_view{};
            }

            if (separator == std::string_view::npos) break;

            query_string = query_string.substr (separator + 1);
        }
    }

    void HttpRequest::Parser::parse_header_fields (std::string_view fields)
    {
        while (not fields.empty ())
        {
            auto line_end = fields.find ("\r\n");

            if (line_end == 0 || line_end == std::string_view::npos) break;

            auto line  = fields.substr (0, line_end);
            auto colon = line.find (':');

            if (colon != std::string_view::npos)
            {
                auto name  = line.substr (0, colon);
                auto value = line.substr (colon + 1);

                while (not value.empty () && (value.front () == ' ' || value.front () == '\t'))
                {
                    value.remove_prefix (1);
                }

                request->headers[name] = value;
            }

            fields = fields.substr (line_end + 2);
        }
    }

    void HttpRequest::Parser::translate_method (std::string_view method_string)
    {
        request->method = method_from_string (method_string);
    }

    HttpRequest::Method HttpRequest::Parser::method_from_string (std::string_view method_string)
    {
        if (method_string.length () >= 3)
        {
            switch (method_string[0])
            {
                case 'G': if (method_string == "GET"    ) return Method::GET;     break;
                case 'P': if (method_string == "POST"   ) return Method::POST;
                          if (method_string == "PUT"    ) return Method::PUT;     break;
                case 'D': if (method_string == "DELETE" ) return Method::DELETE;  break;
                case 'O': if (method_string == "OPTIONS") return Method::OPTIONS; break;
                case 'H': if (method_string == "HEAD"   ) return Method::HEAD;    break;
                case 'T': if (method_string == "TRACE"  ) return Method::TRACE;   break;
                case 'L': if (method_string == "LINK"   ) return Method::LINK;    break;
                case 'U': if (method_string == "UNLINK" ) return Method::UNLINK;  break;
            }
        }

        return Method::UNDEFINED;
    }

    std::string_view HttpRequest::Serializer::method_to_string (HttpRequest::Method method)
    {
        switch (method)
        {
            case Method::OPTIONS: return "OPTIONS";
            case Method::HEAD:    return "HEAD";
            case Method::GET:     return "GET";
            case Method::POST:    return "POST";
            case Method::PUT:     return "PUT";
            case Method::LINK:    return "LINK";
            case Method::UNLINK:  return "UNLINK";
            case Method::DELETE:  return "DELETE";
            case Method::TRACE:   return "TRACE";
        }

        return {};
    }

}
