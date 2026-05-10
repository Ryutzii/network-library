/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <LuaRequestHandler.hpp>

#if defined(__has_include)
#   if __has_include(<lua.hpp>)
#       include <lua.hpp>
#       define ARGB_NETWORK_LIBRARY_HAS_LUA 1
#   elif __has_include(<lua.h>) && __has_include(<lauxlib.h>) && __has_include(<lualib.h>)
extern "C"
{
#       include <lua.h>
#       include <lauxlib.h>
#       include <lualib.h>
}
#       define ARGB_NETWORK_LIBRARY_HAS_LUA 1
#   else
#       define ARGB_NETWORK_LIBRARY_HAS_LUA 0
#   endif
#else
#   define ARGB_NETWORK_LIBRARY_HAS_LUA 0
#endif

namespace argb
{

#if ARGB_NETWORK_LIBRARY_HAS_LUA
    namespace
    {
        lua_State* shared_lua_state = nullptr;
        size_t     lua_state_users  = 0;
    }
#endif

    LuaRequestHandler::StateScope::StateScope()
    {
        initialize_lua_state();
    }

    LuaRequestHandler::StateScope::~StateScope()
    {
        destroy_lua_state();
    }

    bool LuaRequestHandler::is_lua_available() noexcept
    {
#if ARGB_NETWORK_LIBRARY_HAS_LUA
        return shared_lua_state != nullptr;
#else
        return false;
#endif
    }

    lua_State* LuaRequestHandler::get_lua_state() noexcept
    {
#if ARGB_NETWORK_LIBRARY_HAS_LUA
        return shared_lua_state;
#else
        return nullptr;
#endif
    }

    void LuaRequestHandler::initialize_lua_state()
    {
#if ARGB_NETWORK_LIBRARY_HAS_LUA
        if (lua_state_users++ == 0)
        {
            shared_lua_state = luaL_newstate();
            if (shared_lua_state != nullptr)
            {
                luaL_openlibs(shared_lua_state);
            }
        }
#endif
    }

    void LuaRequestHandler::destroy_lua_state() noexcept
    {
#if ARGB_NETWORK_LIBRARY_HAS_LUA
        if (lua_state_users == 0)
        {
            return;
        }

        if (--lua_state_users == 0)
        {
            if (shared_lua_state != nullptr)
            {
                lua_close(shared_lua_state);
                shared_lua_state = nullptr;
            }
        }
#endif
    }

}
