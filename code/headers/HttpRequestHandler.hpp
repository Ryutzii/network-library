
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <HttpRequest.hpp>
#include <HttpResponse.hpp>
#include <memory>

namespace argb
{

    /** This class serves as an interface for handling HTTP requests. It defines a pure virtual method `process` that
      * must be implemented by derived classes to provide custom logic for processing incoming HTTP requests and generating
      * appropriate HTTP responses.
      */
    class HttpRequestHandler
    {
    public:

        /** This class serves as a smart pointer wrapper for HttpRequestHandler objects, allowing the HttpServer to
          * manage both raw pointers and unique_ptr instances seamlessly. It provides a uniform interface for accessing
          * the underlying HttpRequestHandler, regardless of how it was created or passed to the server.
          * This design simplifies memory management and ensures that the server can handle request handlers without
          * worrying about ownership semantics.
          */
        class Ptr
        {

            HttpRequestHandler * raw_pointer;
            std::unique_ptr<HttpRequestHandler> smart_pointer;

        public:

            Ptr() : raw_pointer{}, smart_pointer{}
            {
            }

            Ptr(HttpRequestHandler * raw_pointer)
                :   raw_pointer{raw_pointer}
                , smart_pointer{}
            {
            }

            Ptr(std::unique_ptr<HttpRequestHandler> unique_ptr)
                :   raw_pointer{unique_ptr.get ()}
                , smart_pointer{std::move (unique_ptr)}
            {
            }

            Ptr(const Ptr & ) = delete;
            Ptr & operator = (const Ptr & ) = delete;

            Ptr(Ptr && other) noexcept
                :   raw_pointer{other.raw_pointer}
                , smart_pointer{std::move (other.smart_pointer)}
            {
                other.raw_pointer = nullptr;
            }

            Ptr & operator = (Ptr && other) noexcept
            {
                if (this != &other)
                {
                    this->  raw_pointer = other.raw_pointer;
                    this->smart_pointer = std::move (other.smart_pointer);
                    other.  raw_pointer = nullptr;
                }

                return *this;
            }

            HttpRequestHandler & operator * () const
            {
                return *raw_pointer;
            }

            HttpRequestHandler * operator -> () const
            {
                return raw_pointer;
            }

            bool operator == (const Ptr & other) const
            {
                return this->raw_pointer == other.raw_pointer;
            }   

            bool operator ! () const
            {
                return raw_pointer == nullptr;
            }

            operator bool () const
            {
                return raw_pointer != nullptr;
            }
        };

        void send_plain_text_response (HttpResponse & response, int status, std::string_view message)
        {
            HttpResponse::Serializer(response)
                .status     (status)
                .header     ("Content-Type",   "text/plain; charset=utf-8")
                .header     ("Content-Length",  std::to_string (message.size ()))
                .header     ("Connection",     "close")
                .end_header ()
                .body       (message);
        }

    public:

        /** Handles an incoming HTTP request and produces an appropriate response. This method must be implemented by
          * derived classes to provide custom request handling logic. The response can be populated incrementally,
          * allowing for asynchronous processing if needed. The method should return true when the response is fully
          * ready to be sent back to the client, or false if the handler is still processing.
          * 
          * @param request  The incoming HTTP request.
          * @param response The HTTP response to be populated.
          * 
          * @return True if the handler finished processing the request and the response is ready to be sent;
          *     false if the handler is still processing.
          */
        virtual bool process (const HttpRequest & request, HttpResponse & response) = 0;

    };

}
