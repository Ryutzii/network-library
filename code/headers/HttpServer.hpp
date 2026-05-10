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
#include <deque>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <future>
#include <vector>
#include <algorithm>
#include <tuple>


namespace argb
{

    class ThreadPool
    {
        std::vector<std::thread> workers;
        std::queue<std::function<void()>> tasks;
        std::mutex tasks_mutex;
        std::mutex exclusive_tasks_mutex;
        std::condition_variable tasks_cv;
        std::atomic<bool> stop_flag{ false };

    public:
        explicit ThreadPool(size_t thread_count = 0)
        {
            if (thread_count == 0)
                thread_count = std::max<size_t>(1, std::thread::hardware_concurrency());

            for (size_t i = 0; i < thread_count; ++i)
            {
                workers.emplace_back([this] {
                    for (;;)
                    {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lk(this->tasks_mutex);
                            this->tasks_cv.wait(lk, [this] { return this->stop_flag.load() || !this->tasks.empty(); });
                            if (this->stop_flag.load() && this->tasks.empty())
                                return;
                            task = std::move(this->tasks.front());
                            this->tasks.pop();
                        }
                        try { task(); }
                        catch (...) {}
                    }
                    });
            }
        }

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        ~ThreadPool()
        {
            stop_flag.store(true);
            tasks_cv.notify_all();
            for (auto& t : workers)
            {
                if (t.joinable()) t.join();
            }
        }

        template<typename F, typename... Args>
        auto submit(F&& f, Args&&... args)
        {
            using result_t = std::invoke_result_t<F, Args...>;
            auto task_ptr = std::make_shared<std::packaged_task<result_t()>>(
                [function = std::forward<F>(f),
                 arguments = std::make_tuple(std::forward<Args>(args)...)]() mutable -> result_t
                {
                    return std::apply(std::move(function), std::move(arguments));
                }
            );

            std::future<result_t> res = task_ptr->get_future();
            {
                std::lock_guard<std::mutex> lk(tasks_mutex);
                tasks.emplace([task_ptr]() { (*task_ptr)(); });
            }
            tasks_cv.notify_one();
            return res;
        }

        template<typename F, typename... Args>
        auto submit_exclusive(F&& f, Args&&... args)
        {
            return submit
            (
                [this, function = std::forward<F>(f),
                 arguments = std::make_tuple(std::forward<Args>(args)...)]() mutable
                {
                    std::lock_guard<std::mutex> exclusive_lock(exclusive_tasks_mutex);
                    return std::apply(std::move(function), std::move(arguments));
                }
            );
        }
    };

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
                CLOSING,
            };

            State                    state;
            Timestamp                last_activity;
            TcpSocket                socket;
            HttpRequest              request;
            HttpResponse             response;
            HttpRequest::Parser      request_parser;
            size_t                   response_bytes_sent;
            HttpRequestHandler::Ptr  handler;

            std::mutex               mutex;

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
        std::atomic<bool>     io_worker_stopped{ true };
        RequestHandlerManager request_handler_manager;

        std::queue<TcpSocket> accepted_queue;
        std::mutex            accepted_queue_mutex;
        std::condition_variable accepted_cv;

        std::queue<TcpSocket::Handle> handler_queue;
        std::mutex                    handler_queue_mutex;
        std::condition_variable       handler_cv;

        // Cierres materializados por accept_thread.
        std::queue<TcpSocket::Handle> close_queue;
        std::mutex                    close_queue_mutex;
        std::condition_variable       close_queue_cv;

        std::mutex            connections_mutex;

        // Acepta conexiones y cierra sockets solicitados.
        std::thread           accept_thread;

        // Lee/escribe sockets; no cierra físicamente.
        std::thread           worker_thread;

        std::thread           handler_thread;

        // Ejecuta Lua cooperativamente.
        std::thread           lua_thread;

        std::deque<TcpSocket::Handle> lua_task_queue;
        std::mutex                   lua_task_queue_mutex;
        std::condition_variable      lua_task_queue_cv;

        std::map<TcpSocket::Handle, std::function<bool(HttpRequest&, HttpResponse&)>> lua_coroutines;
        std::mutex lua_coroutines_mutex;

        // Pool único para handlers nativos y tareas auxiliares de Lua.
        ThreadPool thread_pool;

    public:

        HttpServer()
            : thread_pool(std::max<size_t>(1, std::thread::hardware_concurrency()))
        {
        }

        ~HttpServer() noexcept
        {
            try
            {
                stop();
            }
            catch (...)
            {
            }
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
            accepted_cv.notify_all();
            close_queue_cv.notify_all();
            {
                std::lock_guard<std::mutex> lg(handler_queue_mutex);
            }
            handler_cv.notify_all();

            {
                std::lock_guard<std::mutex> lg(lua_task_queue_mutex);
            }
            lua_task_queue_cv.notify_all();

            if (worker_thread.joinable()) worker_thread.join();
            if (handler_thread.joinable()) handler_thread.join();
            if (lua_thread.joinable()) lua_thread.join();
            close_queue_cv.notify_all();
            if (accept_thread.joinable()) accept_thread.join();
        }

    private:

        void accept_connections();
        void transfer_data();
        void receive_request(ConnectionContext& context);
        void write_response_header(ConnectionContext& context);
        void write_response_body(ConnectionContext& context);
        void run_handlers();
        void close_inactive_connections();
        void request_connection_close(TcpSocket::Handle handle);
        void close_pending_connections();
        void schedule_lua_resume(TcpSocket::Handle handle);

        void accept_thread_run();

        void handler_thread_run();

        void lua_thread_run();

    };

}
