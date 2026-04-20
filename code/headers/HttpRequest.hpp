
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <cstddef>

#include "HttpMessage.hpp"

namespace argb
{

    /** This class represents an HTTP request message, which is a specific type of HTTP message that contains information
      * about the client's request to the server. It includes components such as the HTTP method, request URI, protocol
      * version, headers, and body. The HttpRequest class provides methods to access these components and is designed to
      * be used in conjunction with the HttpMessage base class to handle the processing of HTTP requests.
      */
    class HttpRequest : public HttpMessage
    {
    public:

        /** Represents the HTTP methods supported by the HttpRequest class.
          */ 
        enum class Method
        {
            UNDEFINED = -1,         ///< Represents an undefined or unrecognized HTTP method
            OPTIONS,
            HEAD,
            GET,
            POST,
            PUT,
            LINK,
            UNLINK,
            DELETE,
            TRACE,
            COUNT,                  ///< Total number of defined HTTP methods (used for array sizing and iteration)
        };

    public:

        /** The Parser class is responsible for parsing raw HTTP request data and populating the HttpRequest object with
          * the parsed information. It processes the request line, headers, and body of the HTTP request, and updates the
          * HttpRequest object accordingly. The Parser class is designed to be used internally by the HttpRequest class
          * to handle the parsing logic.
          */
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

            void parse_header        ();
            void parse_request_line  (std::string_view line);
            void parse_uri           (std::string_view uri);
            void parse_query         (std::string_view query_string);
            void parse_header_fields (std::string_view fields);
            void translate_method    (std::string_view method_string);

        public:

            static HttpRequest::Method method_from_string (std::string_view method_string);

        };

        class Serializer
        {
        public:

            static std::string_view method_to_string (HttpRequest::Method method);

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
            method = Method::UNDEFINED;
        }

    public:

        /** Retrieves the HTTP protocol version used in the request. The protocol version is typically represented as a
          * string, such as "HTTP/1.1" or "HTTP/2".
          * @return A string view representing the HTTP protocol version used in the request.
          */
        std::string_view get_protocol () const
        {
            return protocol;
        }

        /** Retrieves the HTTP method of the request. The method indicates the desired action to be performed on the
          * resource identified by the request URI. Common HTTP methods include GET, POST, PUT, DELETE, etc.
          * @return The HTTP method of the request as an enumeration value.
          */
        Method get_method () const
        {
            return method;
        }

        /** Retrieves the path component of the request URI. The path represents the hierarchical structure of the resource
          * being * requested, and it follows the domain name in the URI. For example, in the URI "http://example.com/path/to/resource",
          * the path would be "/path/to/resource".
          * @return A string view representing the path component of the request URI.
          */
        std::string_view get_path () const
        {
            return path;
        }

        /** Retrieves the fragment component of the request URI. The fragment is an optional part of the URI that follows a
          * '#' character and is used to identify a specific section or element within the resource. For example, in the URI
          * "http://example.com/path#section1", the fragment would be "section1".
          * @return A string view representing the fragment component of the request URI.
          */
        std::string_view get_fragment () const
        {
            return fragment;
        }

        /** Retrieves the value of a query parameter by its key. Query parameters are typically included in the request URI
          * after a '?' character and consist of key-value pairs separated by '&' characters. For example, in the URI
          * "http://example.com/path?key1=value1&key2=value2", the query parameters would be "key1" with value "value1" and
          * "key2" with value "value2".
          * @param key The key of the query parameter to retrieve.
          * @return A string view representing the value of the query parameter associated with the specified key, or an empty
          *     string view if the key is not found in the query parameters.
          */
        std::string_view get_query (std::string_view key) const
        {
            auto   item  = query.find (key);
            return item != query. end () ? item->second : std::string_view{};
        }

    };

}
