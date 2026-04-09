
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <HttpRequest.hpp>
#include <HttpResponse.hpp>
#include <HttpRequestHandler.hpp>
#include <snippets.hpp>
#include <TcpListener.hpp>

#include <array>
#include <atomic>
#include <map>
#include <memory>

namespace argb
{

    class HttpServer
    {

        using IoBuffer              = std::array<std::byte, 4096>;
        using HttpRequestHandlerPtr = HttpRequestHandler *; //std::unique_ptr<HttpRequestHandler>;

        struct ConnectionContext
        {
            enum State
            {
                RECEIVING_REQUEST,
                RUNNING_HANDLER,
                WRITING_RESPONSE_HEADER,
                WRITING_RESPONSE_BODY,
                CLOSED,
            };

            State                    state;
            Timestamp                last_activity;
            TcpSocket                socket;
            HttpRequest              request;
            HttpResponse             response;
            HttpRequest::Parser      request_parser;
            HttpResponse::Serializer response_serializer;
            size_t                   response_bytes_sent;
            HttpRequestHandlerPtr    handler;

            ConnectionContext();
            ConnectionContext(const ConnectionContext & ) = delete;
            ConnectionContext(ConnectionContext && other) noexcept;

            ConnectionContext & operator = (const ConnectionContext & ) = delete;
            ConnectionContext & operator = (ConnectionContext && ) noexcept = delete;
        };

        using ConnectionMap = std::map<TcpSocket::Handle, ConnectionContext>;

        struct ListenerScopeGuard
        {
            TcpListener & listener;

            ~ListenerScopeGuard ()
            {
                listener.close (true);
            }
        };

        static constexpr std::chrono::seconds connection_timeout{ 10 };

    private:

        ConnectionMap         connections;
        TcpListener           listener;
        std::atomic<bool>     running;
        
        HttpRequestHandlerPtr default_handler;

    public:

        HttpServer() : default_handler{}
        {
            running = false;
        }

        void set_default_handler (HttpRequestHandlerPtr handler)
        {
            default_handler = handler;
        }

        void run (const Port& local_port)
        {
            run (Address::any, local_port);
        }

        void run (const Address & local_address, const Port & local_port);

        void stop ()
        {
            running = false;
        }

    private:

        void accept_connections ();
        void transfer_data ();
        void receive_request (ConnectionContext & context);
        void write_response_header (ConnectionContext & context);
        void write_response_body (ConnectionContext & context);
        void run_handlers ();
        void close_inactive_connections ();

    private:


    };

}
