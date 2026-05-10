/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <HttpServer.hpp>
#include <LuaRequestHandler.hpp>
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
        , request_parser(request)
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

    void HttpServer::run(const Address& address, const Port& port)
    {
        listener.listen(address, port);
        running = true;
        io_worker_stopped = false;

        accept_thread = std::thread(&HttpServer::accept_thread_run, this);

        worker_thread = std::thread(&HttpServer::transfer_data, this);

        handler_thread = std::thread(&HttpServer::handler_thread_run, this);

        lua_thread = std::thread(&HttpServer::lua_thread_run, this);
    }

    void HttpServer::accept_thread_run()
    {
        using namespace std::chrono_literals;

        while (running || !io_worker_stopped.load())
        {
            close_pending_connections();

            if (running)
            {
                try
                {
                    if (auto new_socket = listener.accept())
                    {
                        try
                        {
                            new_socket->set_blocking(false);
                        }
                        catch (...)
                        {
                        }

                        {
                            std::lock_guard<std::mutex> lock(accepted_queue_mutex);
                            accepted_queue.push(std::move(*new_socket));
                        }
                        accepted_cv.notify_one();
                    }
                    else
                    {
                        std::unique_lock<std::mutex> close_lock(close_queue_mutex);
                        close_queue_cv.wait_for(close_lock, 10ms, [this]
                        {
                            return !running || !close_queue.empty();
                        });
                    }
                }
                catch (const NetworkException& ex)
                {
                    cout << "Error accepting: " << ex << endl;
                    std::this_thread::sleep_for(50ms);
                }
            }
            else
            {
                std::unique_lock<std::mutex> close_lock(close_queue_mutex);
                close_queue_cv.wait_for(close_lock, 10ms, [this]
                {
                    return io_worker_stopped.load() || !close_queue.empty();
                });
            }
        }

        close_pending_connections();
    }

    void HttpServer::transfer_data()
    {
        using namespace std::chrono_literals;

        while (running)
        {
            {
                std::unique_lock<std::mutex> qlock(accepted_queue_mutex);
                if (accepted_queue.empty())
                {
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
                            if (ctx.state == ConnectionContext::RUNNING_HANDLER)
                            {
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
                            break;

                        case ConnectionContext::CLOSED:
                        case ConnectionContext::CLOSING:
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
                        request_connection_close(it->first);
                        ctx.state = ConnectionContext::CLOSING;
                    }

                    ++it;
                }
            }

            close_inactive_connections();
        }

        {
            std::lock_guard<std::mutex> conn_lock(connections_mutex);
            for (auto& kv : connections)
            {
                if (kv.second.state != ConnectionContext::CLOSING)
                {
                    kv.second.state = ConnectionContext::CLOSED;
                    request_connection_close(kv.first);
                }
            }
        }

        io_worker_stopped = true;
        close_queue_cv.notify_all();
    }

    void HttpServer::handler_thread_run()
    {
        using namespace std::chrono_literals;

        while (running || !handler_queue.empty())
        {
            std::vector<TcpSocket::Handle> batch;

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
                continue;
            }

            for (auto handle : batch)
            {
                {
                    std::lock_guard<std::mutex> conn_lock(connections_mutex);
                    auto it = connections.find(handle);
                    if (it == connections.end()) continue;
                    if (it->second.state != ConnectionContext::RUNNING_HANDLER) continue;
                }

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

                HttpRequestHandler* handler_raw = nullptr;
                bool handler_requires_lua = false;
                {
                    std::lock_guard<std::mutex> conn_lock(connections_mutex);
                    auto it = connections.find(handle);
                    if (it == connections.end()) continue;
                    std::lock_guard<std::mutex> ctx_lock(it->second.mutex);
                    if (!it->second.handler) continue;
                    handler_raw = it->second.handler.operator->();
                    handler_requires_lua = handler_raw->requires_lua();
                }

                if (!handler_raw) continue;

                if (handler_requires_lua)
                {
                    schedule_lua_resume(handle);
                    continue;
                }

                HttpRequest request_snapshot;
                {
                    std::lock_guard<std::mutex> conn_lock(connections_mutex);
                    auto it = connections.find(handle);
                    if (it == connections.end()) continue;
                    std::lock_guard<std::mutex> ctx_lock(it->second.mutex);
                    if (it->second.state != ConnectionContext::RUNNING_HANDLER) continue;
                    request_snapshot = std::move(it->second.request);
                }

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

                        {
                            std::lock_guard<std::mutex> conn_lock(connections_mutex);
                            auto it = connections.find(handle);
                            if (it == connections.end()) return;
                            std::lock_guard<std::mutex> ctx_lock(it->second.mutex);

                            it->second.response = std::move(response_local);

                            if (finished)
                            {
                                it->second.state = ConnectionContext::WRITING_RESPONSE_HEADER;
                                it->second.response_bytes_sent = 0;
                                it->second.last_activity = now();
                            }
                            else
                            {
                                it->second.request = std::move(request_snapshot);
                                it->second.last_activity = now();
                                {
                                    std::lock_guard<std::mutex> hlock(handler_queue_mutex);
                                    handler_queue.push(handle);
                                }
                                handler_cv.notify_one();
                            }
                        }
                    };

                try
                {
                    thread_pool.submit(std::move(work_fn));
                }
                catch (...)
                {
                    std::lock_guard<std::mutex> conn_lock(connections_mutex);
                    auto it = connections.find(handle);
                    if (it != connections.end())
                    {
                        std::lock_guard<std::mutex> ctx_lock(it->second.mutex);
                        it->second.state = ConnectionContext::CLOSED;
                    }
                }

            }
        }
    }

    void HttpServer::request_connection_close(TcpSocket::Handle handle)
    {
        {
            std::lock_guard<std::mutex> close_lock(close_queue_mutex);
            close_queue.push(handle);
        }
        close_queue_cv.notify_one();
    }

    void HttpServer::close_pending_connections()
    {
        std::vector<TcpSocket::Handle> pending_closes;

        {
            std::lock_guard<std::mutex> close_lock(close_queue_mutex);
            while (!close_queue.empty())
            {
                pending_closes.push_back(close_queue.front());
                close_queue.pop();
            }
        }

        if (pending_closes.empty())
        {
            return;
        }

        std::lock_guard<std::mutex> conn_lock(connections_mutex);

        for (auto handle : pending_closes)
        {
            auto it = connections.find(handle);
            if (it == connections.end())
            {
                continue;
            }

            std::unique_lock<std::mutex> ctx_lock(it->second.mutex);

            if (it->second.state != ConnectionContext::CLOSING &&
                it->second.state != ConnectionContext::CLOSED)
            {
                continue;
            }

            {
                std::lock_guard<std::mutex> coroutine_lock(lua_coroutines_mutex);
                lua_coroutines.erase(handle);
            }

            try
            {
                it->second.socket.shutdown_send();
                it->second.socket.shutdown_receive();
            }
            catch (...)
            {
            }

            try
            {
                it->second.socket.close();
            }
            catch (...)
            {
            }

            ctx_lock.unlock();
            connections.erase(it);
        }
    }

    void HttpServer::schedule_lua_resume(TcpSocket::Handle handle)
    {
        try
        {
            thread_pool.submit([this, handle]
                {
                    if (!running) return;

                    {
                        std::lock_guard<std::mutex> lg(lua_task_queue_mutex);
                        lua_task_queue.push_back(handle);
                    }
                    lua_task_queue_cv.notify_one();
                });
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lg(lua_task_queue_mutex);
            lua_task_queue.push_back(handle);
            lua_task_queue_cv.notify_one();
        }
    }

    void HttpServer::lua_thread_run()
    {
        using namespace std::chrono_literals;

        LuaRequestHandler::StateScope lua_state_scope;

        if (!LuaRequestHandler::is_lua_available())
        {
            cout << "Lua VM is not available: Lua headers/runtime were not found when building this library." << endl;
        }

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
                if (!LuaRequestHandler::is_lua_available())
                {
                    std::lock_guard<std::mutex> conn_lock(connections_mutex);
                    auto it = connections.find(handle);
                    if (it != connections.end())
                    {
                        std::lock_guard<std::mutex> ctx_lock(it->second.mutex);
                        if (it->second.state == ConnectionContext::RUNNING_HANDLER && it->second.handler)
                        {
                            it->second.handler->send_plain_text_response
                            (
                                it->second.response,
                                500,
                                "Lua VM is not available"
                            );
                            it->second.state = ConnectionContext::WRITING_RESPONSE_HEADER;
                            it->second.response_bytes_sent = 0;
                            it->second.last_activity = now();
                        }
                    }
                    continue;
                }

                {
                    std::unique_lock<std::mutex> conn_lock(connections_mutex);
                    auto it = connections.find(handle);
                    if (it == connections.end()) continue;
                    std::unique_lock<std::mutex> ctx_lock(it->second.mutex);
                    conn_lock.unlock();

                    if (it->second.state != ConnectionContext::RUNNING_HANDLER) continue;
                    if (!it->second.handler) continue;

                    auto* handler_raw = it->second.handler.operator->();
                    bool finished = false;

                    try
                    {
                        std::function<bool(HttpRequest&, HttpResponse&)>* coroutine = nullptr;

                        {
                            std::lock_guard<std::mutex> coroutine_lock(lua_coroutines_mutex);
                            auto coroutine_it = lua_coroutines.find(handle);

                            if (coroutine_it == lua_coroutines.end())
                            {
                                auto created_coroutine = handler_raw->create_lua_coroutine();

                                if (!created_coroutine)
                                {
                                    created_coroutine = [handler_raw](HttpRequest& request, HttpResponse& response)
                                    {
                                        return handler_raw->process(request, response);
                                    };
                                }

                                coroutine_it = lua_coroutines.emplace(handle, std::move(created_coroutine)).first;
                            }

                            coroutine = &coroutine_it->second;
                        }

                        finished = (*coroutine)(it->second.request, it->second.response);
                    }
                    catch (const std::exception& ex)
                    {
                        cout << "Lua coroutine exception: " << ex.what() << endl;
                        it->second.state = ConnectionContext::CLOSED;
                        std::lock_guard<std::mutex> coroutine_lock(lua_coroutines_mutex);
                        lua_coroutines.erase(handle);
                        continue;
                    }
                    catch (...)
                    {
                        cout << "Lua coroutine unknown exception." << endl;
                        it->second.state = ConnectionContext::CLOSED;
                        std::lock_guard<std::mutex> coroutine_lock(lua_coroutines_mutex);
                        lua_coroutines.erase(handle);
                        continue;
                    }

                    if (finished)
                    {
                        {
                            std::lock_guard<std::mutex> coroutine_lock(lua_coroutines_mutex);
                            lua_coroutines.erase(handle);
                        }

                        it->second.state = ConnectionContext::WRITING_RESPONSE_HEADER;
                        it->second.response_bytes_sent = 0;
                        it->second.last_activity = now();
                    }
                    else
                    {
                        it->second.last_activity = now();
                        schedule_lua_resume(handle);
                        std::this_thread::sleep_for(1ms);
                    }
                }
            }
        }

        std::lock_guard<std::mutex> coroutine_lock(lua_coroutines_mutex);
        lua_coroutines.clear();
    }

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
            return;
        }

        bool parsed = context.request_parser.parse({ buffer.data(), received });

        context.last_activity = now();

        if (parsed)
        {
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

    void HttpServer::close_inactive_connections()
    {
        const auto current_time = now();

        std::lock_guard<std::mutex> conn_lock(connections_mutex);

        for (auto& [handle, ctx] : connections)
        {
            std::lock_guard<std::mutex> ctx_lock(ctx.mutex);

            if (ctx.state == ConnectionContext::CLOSED)
            {
                request_connection_close(handle);
                ctx.state = ConnectionContext::CLOSING;
                continue;
            }

            if (ctx.state != ConnectionContext::CLOSING &&
                current_time - ctx.last_activity > connection_timeout)
            {
                cout << "Closing connection " << handle << " due to timeout." << endl;
                ctx.state = ConnectionContext::CLOSING;
                request_connection_close(handle);
            }
        }
    }

}
