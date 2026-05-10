/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <HttpRequest.hpp>
#include <HttpResponse.hpp>
#include <memory>
#include <functional>


namespace argb
{
    class HttpRequestHandler
    {
    public:
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

        virtual ~HttpRequestHandler() = default;
        virtual bool process (const HttpRequest & request, HttpResponse & response) = 0;
        virtual bool requires_lua() const { return false; }
        virtual std::function<bool(HttpRequest&, HttpResponse&)> create_lua_coroutine()
        {
            return {};
        }

    };

}
