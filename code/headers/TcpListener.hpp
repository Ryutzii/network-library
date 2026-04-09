
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <optional>
#include <TcpSocket.hpp>

namespace argb
{

    class TcpListener final 
    {
    private:

        static const int max_backlog;

        TcpSocket listening_socket;

    public:

		TcpListener() = default;
        TcpListener(TcpListener && ) = default;
        TcpListener(const TcpListener & ) = delete;
        TcpListener & operator = (TcpListener && ) = default;
        TcpListener & operator = (const TcpListener & ) = delete;

    public:

        bool is_listening () const
        {
            return listening_socket.is_open ();
        }

        bool is_not_listening () const
        {
            return not is_listening ();
        }

        void listen (const Port & local_port, int backlog = max_backlog)
        {
            listen (Address::any, local_port, backlog);
        }

        void listen (const Address & local_address, const Port & local_port, int backlog = max_backlog);

        std::optional<TcpSocket> accept ();

        bool close (bool force_close = false) noexcept
        {
            return listening_socket.close (force_close);
        }

    };

}
