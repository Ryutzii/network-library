/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <HttpServer.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <condition_variable>

using std::cout;
using std::endl;

namespace argb
{

    // Constructores / movimiento
    HttpServer::ConnectionContext::ConnectionContext()
        : state(RECEIVING_REQUEST)
        , last_activity(now())
        , request_parser(request)
        , response_bytes_sent(0)
    {
    }

    HttpServer::ConnectionContext::ConnectionContext(ConnectionContext&& other) noexcept
        : state(other.state)
        , last_activity(other.last_activity)
        , socket(std::move(other.socket))
        , request(std::move(other.request))
        , response(std::move(other.response))
        , request_parser(request) // rebind parser to this.request
        , handler(std::move(other.handler))
        , response_bytes_sent(other.response_bytes_sent)
    {
    }

    HttpRequestHandler::Ptr HttpServer::RequestHandlerManager::create_handler
    (
        HttpRequest::Method method,
        std::string_view    request_path
    )
        const
    {
        for (auto* factory : handler_factories)
        {
            if (auto handler = factory->create_handler(method, request_path))
            {
                return handler;
            }
        }

        return nullptr;
    }

    // run: lanza los hilos y vuelve al llamador. stop() se encarga del join/limpieza.
    void HttpServer::run(const Address& address, const Port& port)
    {
        listener.listen(address, port);
        running = true;

        // Hilo que acepta nuevas conexiones y las pone en la cola
        accept_thread = std::thread(&HttpServer::accept_thread_run, this);

        // Worker que procesa I/O y ejecuta handlers
        worker_thread = std::thread(&HttpServer::transfer_data, this);
    }

    // Hilo de aceptación: acepta sockets y los encola para el worker.
    void HttpServer::accept_thread_run()
    {
        using namespace std::chrono_literals;

        while (running)
        {
            try
            {
                if (auto new_socket = listener.accept())
                {
                    // Asegurar non-blocking para I/O gestionado por el worker
                    try
                    {
                        new_socket->set_blocking(false);
                    }
                    catch (...)
                    {
                        // Si no se puede setear, el worker lo detectará al intentar I/O
                    }

                    {
                        std::lock_guard<std::mutex> lock(accepted_queue_mutex);
                        accepted_queue.push(std::move(*new_socket));
                    }
                    accepted_cv.notify_one();
                }
                else
                {
                    // No había conexión; evitar busy-loop
                    std::this_thread::sleep_for(10ms);
                }
            }
            catch (const NetworkException& ex)
            {
                cout << "Error accepting: " << ex << endl;
                std::this_thread::sleep_for(50ms);
            }
        }
    }

    // Worker: consume accepted_queue, gestiona conexiones (I/O) y ejecuta handlers.
    void HttpServer::transfer_data()
    {
        using namespace std::chrono_literals;

        while (running)
        {
            // 1) Mover sockets de la cola aceptada al mapa de conexiones
            {
                std::unique_lock<std::mutex> qlock(accepted_queue_mutex);
                if (accepted_queue.empty())
                {
                    // Despertar periódicamente para tareas (timeouts, checks)
                    accepted_cv.wait_for(qlock, 200ms);
                }

                while (!accepted_queue.empty())
                {
                    TcpSocket sock = std::move(accepted_queue.front());
                    accepted_queue.pop();

                    ConnectionContext ctx;
                    ctx.state = ConnectionContext::RECEIVING_REQUEST;
                    ctx.last_activity = now();
                    ctx.socket = std::move(sock);
                    ctx.request = HttpRequest{};
                    ctx.response = HttpResponse{};
                    ctx.response_bytes_sent = 0;
                    ctx.handler = nullptr;

                    const auto handle = ctx.socket.get_handle();
                    std::lock_guard<std::mutex> conn_lock(connections_mutex);
                    connections.emplace(handle, std::move(ctx));
                }
            }

            // 2) Primera fase: I/O no bloqueante (lectura/escritura) para cada conexión
            {
                std::lock_guard<std::mutex> conn_lock(connections_mutex);
                for (auto it = connections.begin(); it != connections.end(); )
                {
                    ConnectionContext& ctx = it->second;

                    try
                    {
                        switch (ctx.state)
                        {
                        case ConnectionContext::RECEIVING_REQUEST:
                            receive_request(ctx);
                            break;

                        case ConnectionContext::WRITING_RESPONSE_HEADER:
                            write_response_header(ctx);
                            break;

                        case ConnectionContext::WRITING_RESPONSE_BODY:
                            write_response_body(ctx);
                            break;

                        case ConnectionContext::RUNNING_HANDLER:
                            // Ejecutar handlers en la fase siguiente (fuera del mutex)
                            break;

                        case ConnectionContext::CLOSED:
                            break;
                        }
                    }
                    catch (const NetworkException& ex)
                    {
                        cout << "I/O error on connection " << it->first << ": " << ex << endl;
                        ctx.state = ConnectionContext::CLOSED;
                    }

                    if (ctx.state == ConnectionContext::CLOSED)
                    {
                        try { ctx.socket.close(); }
                        catch (...) {}
                        it = connections.erase(it);
                    }
                    else
                        ++it;
                }
            }

            // 3) Segunda fase: ejecutar handlers para las conexiones que lo requieren.
            // Recolectar referencias a ejecutar fuera del lock para evitar bloquear I/O.
            std::vector<std::reference_wrapper<ConnectionContext>> to_process;
            {
                std::lock_guard<std::mutex> conn_lock(connections_mutex);
                to_process.reserve(connections.size());
                for (auto& kv : connections)
                {
                    ConnectionContext& ctx = kv.second;
                    if (ctx.state == ConnectionContext::RUNNING_HANDLER)
                    {
                        if (!ctx.handler)
                        {
                            ctx.handler = request_handler_manager.create_handler(ctx.request.get_method(), ctx.request.get_path());
                            if (!ctx.handler)
                            {
                                // Generar 404 si no hay handler
                                static constexpr std::string_view not_found_message = "File not found";

                                HttpResponse::Serializer(ctx.response)
                                    .status(404)
                                    .header("Content-Type", "text/plain; charset=utf-8")
                                    .header("Content-Length", std::to_string(not_found_message.size()))
                                    .header("Connection", "close")
                                    .end_header()
                                    .body(not_found_message);

                                ctx.state = ConnectionContext::WRITING_RESPONSE_HEADER;
                                ctx.response_bytes_sent = 0;
                                continue;
                            }
                        }

                        // if handler exists, schedule for processing
                        if (ctx.handler)
                            to_process.push_back(std::ref(ctx));
                    }
                }
            }

            // Ejecutar handlers fuera del mutex
            for (auto& ref_ctx : to_process)
            {
                ConnectionContext& ctx = ref_ctx.get();
                try
                {
                    const bool finished = ctx.handler->process(ctx.request, ctx.response);
                    if (finished)
                    {
                        std::lock_guard<std::mutex> conn_lock(connections_mutex);
                        ctx.state = ConnectionContext::WRITING_RESPONSE_HEADER;
                        ctx.response_bytes_sent = 0;
                        ctx.last_activity = now();
                    }
                    else
                    {
                        std::lock_guard<std::mutex> conn_lock(connections_mutex);
                        ctx.last_activity = now();
                        // mantiene RUNNING_HANDLER para intento posterior
                    }
                }
                catch (const std::exception& ex)
                {
                    cout << "Handler exception: " << ex.what() << endl;
                    std::lock_guard<std::mutex> conn_lock(connections_mutex);
                    ctx.state = ConnectionContext::CLOSED;
                }
                catch (...)
                {
                    cout << "Handler unknown exception." << endl;
                    std::lock_guard<std::mutex> conn_lock(connections_mutex);
                    ctx.state = ConnectionContext::CLOSED;
                }
            }

            // 4) Cerrar inactivas
            close_inactive_connections();
        }

        // Limpieza final: cerrar sockets y vaciar mapa
        {
            std::lock_guard<std::mutex> conn_lock(connections_mutex);
            for (auto& kv : connections)
            {
                try { kv.second.socket.close(); }
                catch (...) {}
            }
            connections.clear();
        }
    }

    // Recibe datos desde el socket y alimenta el parser.
    void HttpServer::receive_request(ConnectionContext& context)
    {
        IoBuffer buffer;
        size_t received = context.socket.receive(buffer);

        if (received == TcpSocket::receive_closed)
        {
            context.state = ConnectionContext::CLOSED;
            return;
        }

        if (received == Socket::receive_empty)
        {
            // nothing to read now (non-blocking)
            return;
        }

        bool parsed = context.request_parser.parse({ buffer.data(), received });

        context.last_activity = now();

        if (parsed)
        {
            // marcar para ejecución de handler en la fase de handlers
            context.state = ConnectionContext::RUNNING_HANDLER;
        }
    }

    void HttpServer::write_response_header(ConnectionContext& context)
    {
        auto header = std::as_bytes(context.response.get_serialized_header());
        if (header.empty())
        {
            context.state = ConnectionContext::WRITING_RESPONSE_BODY;
            context.response_bytes_sent = 0;
            return;
        }

        auto remaining = header.subspan(context.response_bytes_sent);
        size_t sent = context.socket.send(remaining);

        if (sent > 0)
        {
            context.response_bytes_sent += sent;
            context.last_activity = now();
        }

        if (context.response_bytes_sent >= header.size())
        {
            context.response_bytes_sent = 0;
            context.state = ConnectionContext::WRITING_RESPONSE_BODY;
        }
    }

    void HttpServer::write_response_body(ConnectionContext& context)
    {
        auto body = std::as_bytes(context.response.get_body());

        if (body.empty())
        {
            context.state = ConnectionContext::CLOSED;
            return;
        }

        auto remaining = body.subspan(context.response_bytes_sent);
        size_t sent = context.socket.send(remaining);

        if (sent > 0)
        {
            context.response_bytes_sent += sent;
            context.last_activity = now();
        }

        if (context.response_bytes_sent >= body.size())
        {
            context.state = ConnectionContext::CLOSED;
            context.response_bytes_sent = 0;
        }
    }

    // Cierra conexiones que han estado inactivas más tiempo que connection_timeout.
    void HttpServer::close_inactive_connections()
    {
        const auto current_time = now();

        std::lock_guard<std::mutex> conn_lock(connections_mutex);

        for (auto it = connections.begin(); it != connections.end(); )
        {
            auto& ctx = it->second;

            if (ctx.state == ConnectionContext::CLOSED)
            {
                try { ctx.socket.close(); }
                catch (...) {}
                it = connections.erase(it);
                continue;
            }

            if (current_time - ctx.last_activity > connection_timeout)
            {
                cout << "Closing connection " << it->first << " due to timeout." << endl;
                try { ctx.socket.shutdown_send(); ctx.socket.shutdown_receive(); ctx.socket.close(); }
                catch (...) {}
                it = connections.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

} // namespace argb