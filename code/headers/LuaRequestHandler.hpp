/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <HttpRequestHandler.hpp>

struct lua_State;

namespace argb
{

    /** Base class for HTTP handlers implemented on top of the shared Lua virtual machine.
      *
      * HttpServer owns the scheduling thread, but the single Lua state itself is managed here. Derived handlers can
      * override create_lua_coroutine(lua_State*) and create a real Lua coroutine with that state (for example with
      * lua_newthread/lua_resume). The returned function is resumed cooperatively by HttpServer: false means yielded,
      * true means finished.
      */
    class LuaRequestHandler : public HttpRequestHandler
    {
    public:

        class StateScope
        {
        public:

            StateScope();
            ~StateScope();

            StateScope(const StateScope&) = delete;
            StateScope& operator = (const StateScope&) = delete;
        };

    public:

        bool requires_lua() const override { return true; }

        std::function<bool(HttpRequest&, HttpResponse&)> create_lua_coroutine() override
        {
            return create_lua_coroutine(get_lua_state());
        }

        virtual std::function<bool(HttpRequest&, HttpResponse&)> create_lua_coroutine(lua_State* lua_state)
        {
            (void) lua_state;
            return {};
        }

        static bool is_lua_available() noexcept;
        static lua_State* get_lua_state() noexcept;

    private:

        static void initialize_lua_state();
        static void destroy_lua_state() noexcept;
    };

}
