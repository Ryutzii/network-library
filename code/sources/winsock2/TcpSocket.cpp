
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <cassert>
#include <NetworkException.hpp>
#include <TcpSocket.hpp>
#include "winsock2.hpp"

namespace argb
{

    void TcpSocket::open (Address::Version version)
    {
        if (is_open ())
        {
            throw NetworkException("Trying to open an already open TCP socket.");
        }

        specification = version;

        handle = cast<Handle> (::socket (version == Address::Version::V4 ? AF_INET : AF_INET6, SOCK_STREAM, IPPROTO_TCP));

        if (!is_open ())
        {
            throw NetworkException("Failed to create TCP socket.");
        }

        if (version == Address::Version::ANY)
        {
            const int disable_ipv6_only = 0;

            if (set_socket_option (cast<SOCKET> (handle), IPPROTO_IPV6, IPV6_V6ONLY, disable_ipv6_only) == false)
            {
                throw NetworkException("Failed to disable IPV6_V6ONLY on TCP socket.");
            }
        }
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    size_t TcpSocket::send (const std::span<const std::byte> data) const
    {
        throw_exception_if_closed ("Trying to send data on an invalid socket.");

        int sent = ::send (cast<SOCKET> (handle), reinterpret_cast<const char *>(data.data ()), static_cast<int>(data.size ()), 0);

        if (sent == SOCKET_ERROR)
        {
            int error =  WSAGetLastError ();
            
            if (error == WSAEWOULDBLOCK)
            {
                return 0;
            }

            throw NetworkException("Failed to send TCP data.", error);
        }

        return static_cast<size_t>(sent);
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    size_t TcpSocket::receive (std::span<std::byte> buffer) const
    {
        throw_exception_if_closed();

        int received = ::recv (cast<SOCKET> (handle), reinterpret_cast<char *>(buffer.data ()), static_cast<int>(buffer.size ()), 0);

        if (received == 0)                      // The connection has been gracefully closed by the remote peer, so
        {                                       // receive_closed is returned to indicate that no more data can be received
            return receive_closed;
        }

        if (received == SOCKET_ERROR)
        {
            return handle_receive_error ();
        }

        return static_cast<size_t>(received);
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    void TcpSocket::shutdown_send ()
    {
        throw_exception_if_closed ("Trying to shutdown send on an invalid socket.");

        ::shutdown (cast<SOCKET> (handle), SD_SEND);
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    void TcpSocket::shutdown_receive ()
    {
        throw_exception_if_closed ("Trying to shutdown receive on an invalid socket.");

        ::shutdown (cast<SOCKET> (handle), SD_RECEIVE);
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    size_t TcpSocket::handle_receive_error () const
    {
        switch (int error = WSAGetLastError ())
        {
            case WSAEWOULDBLOCK:
            {
                return receive_empty;
            }
            case WSAETIMEDOUT:
            {
                throw NetworkException("Receive operation timed out.", WSAETIMEDOUT);
            }
            case WSAECONNRESET:
            case WSAECONNABORTED:
            {
                throw NetworkException("Connection reset by peer.", error);
            }
            default:
            {
                throw NetworkException("Fatal error during TCP receive.", error);
            }
        }
    }

}
