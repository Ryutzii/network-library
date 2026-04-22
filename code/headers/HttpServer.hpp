/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <HttpRequest.hpp>
#include <HttpResponse.hpp>
#include <HttpRequestHandler.hpp>
#include <HttpRequestHandlerFactory.hpp>
#include <snippets.hpp>
#include <TcpListener.hpp>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <chrono>

namespace argb
{

    class HttpServer
    {

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
            size_t                   response_bytes_sent;
            HttpRequestHandler::Ptr  handler;

            ConnectionContext();

            ConnectionContext(const ConnectionContext&) = delete;
            ConnectionContext(ConnectionContext&& other) noexcept;

            ConnectionContext& operator = (const ConnectionContext&) = delete;
            ConnectionContext& operator = (ConnectionContext&&) noexcept = delete;
        };

        class RequestHandlerManager
        {
            using HandlerFactoryContainer = std::vector<HttpRequestHandlerFactory*>;

            HandlerFactoryContainer handler_factories;

        public:

            void register_handler_factory(HttpRequestHandlerFactory& factory)
            {
                handler_factories.push_back(&factory);
            }

            HttpRequestHandler::Ptr create_handler(HttpRequest::Method method, std::string_view request_path) const;
        };

        struct ListenerScopeGuard
        {
            TcpListener& listener;

            ~ListenerScopeGuard() { listener.close(true); }
        };


        using  IoBuffer = std::array<std::byte, 4096>;
        using  ConnectionMap = std::map<TcpSocket::Handle, ConnectionContext>;

        static constexpr std::chrono::seconds connection_timeout{ 10 };

    private:

        ConnectionMap         connections;
        TcpListener           listener;
        std::atomic<bool>     running{};
        RequestHandlerManager request_handler_manager;

        // Nueva cola y mutex para aceptar conexiones en un hilo separado
        std::queue<TcpSocket> accepted_queue;
        std::mutex            accepted_queue_mutex;
        std::condition_variable accepted_cv; // notifica al worker de nuevas conexiones

        // Mutex para proteger acceso a 'connections' si fuera necesario (stop/join)
        std::mutex            connections_mutex;

        // Hilo que acepta conexiones y cierra inactivas
        std::thread           accept_thread;

        // Segundo hilo: realiza lectura/escritura y ejecuta handlers
        std::thread           worker_thread;

    public:

        HttpServer()
        {
        }

        void register_handler_factory(HttpRequestHandlerFactory& factory)
        {
            request_handler_manager.register_handler_factory(factory);
        }

        void run(const Port& local_port)
        {
            run(Address::any, local_port);
        }

        void run(const Address& local_address, const Port& local_port);

        void stop()
        {
            running = false;
        }

    private:

        void accept_connections();
        void transfer_data();
        void receive_request(ConnectionContext& context);
        void write_response_header(ConnectionContext& context);
        void write_response_body(ConnectionContext& context);
        void run_handlers();
        void close_inactive_connections();

        // Función ejecutada por el hilo de aceptación
        void accept_thread_run();

    };

}