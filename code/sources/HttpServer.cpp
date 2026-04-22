/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <HttpServer.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <condition_variable>
#include <deque>

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

        // Worker que procesa I/O y encola trabajos para handlers
        worker_thread = std::thread(&HttpServer::transfer_data, this);

        // Hilo que ejecuta handlers (creación + process)
        handler_thread = std::thread(&HttpServer::handler_thread_run, this);

        // Hilo único que ejecutará handlers que necesitan Lua (mismo diseño anterior:
        // la única instancia de lua::State debe residir en este hilo).
        lua_thread = std::thread(&HttpServer::lua_thread_run, this);
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

    // Worker: consume accepted_queue, gestiona conexiones (I/O) y encola handlers.
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
                            // si el parser terminó, receive_request seteará RUNNING_HANDLER
                            if (ctx.state == ConnectionContext::RUNNING_HANDLER)
                            {
                                // encolar para handlers (protección mínima)
                                const auto handle = it->first;
                                {
                                    std::lock_guard<std::mutex> hlock(handler_queue_mutex);
                                    handler_queue.push(handle);
                                }
                                handler_cv.notify_one();
                            }
                            break;

                        case ConnectionContext::WRITING_RESPONSE_HEADER:
                            write_response_header(ctx);
                            break;

                        case ConnectionContext::WRITING_RESPONSE_BODY:
                            write_response_body(ctx);
                            break;

                        case ConnectionContext::RUNNING_HANDLER:
                            // Ya encolada para handler; no hacer nada en I/O
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

            // 3) Cerrar inactivas
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

    // Hilo dedicado a ejecutar handlers. Consume handler_queue.
    void HttpServer::handler_thread_run()
    {
        using namespace std::chrono_literals;

        while (running || !handler_queue.empty())
        {
            std::vector<TcpSocket::Handle> batch;

            // Esperar hasta que haya trabajo o se pare el servidor
            {
                std::unique_lock<std::mutex> hl(handler_queue_mutex);
                if (handler_queue.empty())
                {
                    handler_cv.wait_for(hl, 200ms);
                }

                while (!handler_queue.empty())
                {
                    batch.push_back(handler_queue.front());
                    handler_queue.pop();
                }
            }

            if (batch.empty())
            {
                // nada que hacer ahora
                continue;
            }

            for (auto handle : batch)
            {
                // Confirmar estado bajo connections_mutex
                {
                    std::lock_guard<std::mutex> conn_lock(connections_mutex);
                    auto it = connections.find(handle);
                    if (it == connections.end()) continue;
                    if (it->second.state != ConnectionContext::RUNNING_HANDLER) continue;
                }

                // Snapshot de method/path si necesitamos crear handler (bloquea connections -> ctx.mutex en ese orden)
                HttpRequest::Method method_snapshot{};
                std::string path_snapshot;
                {
                    std::lock_guard<std::mutex> conn_lock(connections_mutex);
                    auto it = connections.find(handle);
                    if (it == connections.end()) continue;
                    std::lock_guard<std::mutex> ctx_lock(it->second.mutex);
                    if (!it->second.handler)
                    {
                        method_snapshot = it->second.request.get_method();
                        path_snapshot = std::string(it->second.request.get_path());
                    }
                }

                HttpRequestHandler::Ptr created_handler;
                if (!path_snapshot.empty())
                {
                    created_handler = request_handler_manager.create_handler(method_snapshot, path_snapshot);
                    // almacenar handler bajo lock (connections -> ctx.mutex)
                    std::lock_guard<std::mutex> conn_lock(connections_mutex);
                    auto it = connections.find(handle);
                    if (it == connections.end()) continue;
                    std::lock_guard<std::mutex> ctx_lock(it->second.mutex);
                    if (!it->second.handler)
                    {
                        if (created_handler)
                            it->second.handler = std::move(created_handler);
                        else
                        {
                            // No hay handler: generar 404 y pasar a escritura
                            static constexpr std::string_view not_found_message = "File not found";

                            HttpResponse::Serializer(it->second.response)
                                .status(404)
                                .header("Content-Type", "text/plain; charset=utf-8")
                                .header("Content-Length", std::to_string(not_found_message.size()))
                                .header("Connection", "close")
                                .end_header()
                                .body(not_found_message);

                            it->second.state = ConnectionContext::WRITING_RESPONSE_HEADER;
                            it->second.response_bytes_sent = 0;
                            it->second.last_activity = now();
                            continue;
                        }
                    }
                }

                // Ejecutar handler fuera del lock o delegar a worker Lua
                HttpRequestHandler* handler_raw = nullptr;
                HttpRequest request_snapshot;
                {
                    std::lock_guard<std::mutex> conn_lock(connections_mutex);
                    auto it = connections.find(handle);
                    if (it == connections.end()) continue;
                    std::lock_guard<std::mutex> ctx_lock(it->second.mutex);
                    if (!it->second.handler) continue; // ya tratado como 404
                    handler_raw = it->second.handler.operator->();
                    // Mover la request fuera del contexto para ejecutar en pool sin riesgo
                    request_snapshot = std::move(it->second.request);
                }

                if (!handler_raw) continue;

                // Si el handler requiere Lua, encolarlo para el worker Lua (hilo dedicado)
                if (handler_raw->requires_lua())
                {
                    {
                        std::lock_guard<std::mutex> lg(lua_task_queue_mutex);
                        lua_task_queue.push_back(handle);
                    }
                    lua_task_queue_cv.notify_one();
                    continue;
                }

                // Enviar ejecución del handler al ThreadPool (paralelismo)
                auto work_fn = [this, handle, handler_raw, request_snapshot = std::move(request_snapshot)]() mutable
                    {
                        HttpResponse response_local;
                        bool finished = false;

                        try
                        {
                            finished = handler_raw->process(request_snapshot, response_local);
                        }
                        catch (const std::exception& ex)
                        {
                            cout << "Handler (pool) exception: " << ex.what() << endl;
                            std::lock_guard<std::mutex> conn_lock(connections_mutex);
                            auto it = connections.find(handle);
                            if (it != connections.end())
                            {
                                std::lock_guard<std::mutex> ctx_lock(it->second.mutex);
                                it->second.state = ConnectionContext::CLOSED;
                            }
                            return;
                        }
                        catch (...)
                        {
                            cout << "Handler (pool) unknown exception." << endl;
                            std::lock_guard<std::mutex> conn_lock(connections_mutex);
                            auto it = connections.find(handle);
                            if (it != connections.end())
                            {
                                std::lock_guard<std::mutex> ctx_lock(it->second.mutex);
                                it->second.state = ConnectionContext::CLOSED;
                            }
                            return;
                        }

                        // Volver al mapa de conexiones y actualizar estado/respuesta
                        {
                            std::lock_guard<std::mutex> conn_lock(connections_mutex);
                            auto it = connections.find(handle);
                            if (it == connections.end()) return;
                            std::lock_guard<std::mutex> ctx_lock(it->second.mutex);

                            // Mover la respuesta generada al contexto
                            it->second.response = std::move(response_local);

                            if (finished)
                            {
                                it->second.state = ConnectionContext::WRITING_RESPONSE_HEADER;
                                it->second.response_bytes_sent = 0;
                                it->second.last_activity = now();
                            }
                            else
                            {
                                // No terminado: re-enqueue para intentar más tarde.
                                it->second.last_activity = now();
                                {
                                    std::lock_guard<std::mutex> hlock(handler_queue_mutex);
                                    handler_queue.push(handle);
                                }
                                handler_cv.notify_one();
                            }
                        }
                    };

                // Submit al pool (no esperamos aquí)
                try
                {
                    thread_pool.submit(std::move(work_fn));
                }
                catch (...)
                {
                    // Si la sumisión falla, cerrar la conexión
                    std::lock_guard<std::mutex> conn_lock(connections_mutex);
                    auto it = connections.find(handle);
                    if (it != connections.end())
                    {
                        std::lock_guard<std::mutex> ctx_lock(it->second.mutex);
                        it->second.state = ConnectionContext::CLOSED;
                    }
                }

                // pequeña pausa no necesaria aquí; el work se ejecutará en paralelo
            }
        }
    }

    // Hilo Lua: ejecuta corrutinas de forma serializada (única instancia de lua::State aquí)
    void HttpServer::lua_thread_run()
    {
        using namespace std::chrono_literals;

        // Inicializar estado Lua aquí si es necesario (vive en este hilo)
        // p.e. lua::State lua_state;

        while (running || !lua_task_queue.empty())
        {
            std::vector<TcpSocket::Handle> batch;

            {
                std::unique_lock<std::mutex> lk(lua_task_queue_mutex);
                if (lua_task_queue.empty())
                    lua_task_queue_cv.wait_for(lk, 200ms);

                while (!lua_task_queue.empty())
                {
                    batch.push_back(lua_task_queue.front());
                    lua_task_queue.pop_front();
                }
            }

            if (batch.empty())
            {
                continue;
            }

            for (auto handle : batch)
            {
                // Confirmar que la conexión y handler siguen válidos
                {
                    std::lock_guard<std::mutex> conn_lock(connections_mutex);
                    auto it = connections.find(handle);
                    if (it == connections.end()) continue;
                    if (it->second.state != ConnectionContext::RUNNING_HANDLER) continue;
                    if (!it->second.handler) continue;
                }

                // Bloquear connections_mutex -> ctx.mutex (en ese orden), luego liberar connections_mutex
                HttpRequestHandler* handler_raw = nullptr;
                {
                    std::unique_lock<std::mutex> conn_lock(connections_mutex);
                    auto it = connections.find(handle);
                    if (it == connections.end()) continue;
                    std::unique_lock<std::mutex> ctx_lock(it->second.mutex);
                    // Ahora podemos soltar connections_mutex y mantener ctx_lock mientras ejecutamos Lua
                    conn_lock.unlock();

                    handler_raw = it->second.handler.operator->();
                    if (!handler_raw) continue;

                    bool finished = false;
                    try
                    {
                        // Ejecutar en el hilo Lua con protección del mutex de la conexión
                        finished = handler_raw->process(it->second.request, it->second.response);
                    }
                    catch (const std::exception& ex)
                    {
                        cout << "Lua-handler exception: " << ex.what() << endl;
                        it->second.state = ConnectionContext::CLOSED;
                        continue;
                    }
                    catch (...)
                    {
                        cout << "Lua-handler unknown exception." << endl;
                        it->second.state = ConnectionContext::CLOSED;
                        continue;
                    }

                    // Actualizar estado según resultado (aún bajo ctx_lock)
                    if (finished)
                    {
                        it->second.state = ConnectionContext::WRITING_RESPONSE_HEADER;
                        it->second.response_bytes_sent = 0;
                        it->second.last_activity = now();
                    }
                    else
                    {
                        // No terminado: re-enqueue para intentar más tarde en el hilo Lua (ejecución cooperativa)
                        it->second.last_activity = now();
                        {
                            std::lock_guard<std::mutex> lg(lua_task_queue_mutex);
                            lua_task_queue.push_back(handle);
                        }
                        lua_task_queue_cv.notify_one();
                        std::this_thread::sleep_for(1ms);
                    }
                    // ctx_lock se libera al salir del scope
                }
            }
        }

        // Destruir estado Lua si fue inicializado
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

            // Adquirir el mutex de la conexión (si está ocupado, respetamos orden connections_mutex -> ctx.mutex)
            std::lock_guard<std::mutex> ctx_lock(ctx.mutex);

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
