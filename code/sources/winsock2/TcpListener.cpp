
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <cassert>
#include <TcpListener.hpp>
#include <NetworkException.hpp>
#include "winsock2.hpp"

namespace argb
{

    const int TcpListener::max_backlog = SOMAXCONN;

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    void TcpListener::listen (const Address & local_address, const Port & local_port, int backlog)
    {
        if (this->is_listening ())
        {
            throw NetworkException("Trying to listen for TCP connections while already listening.");
        }

        listening_socket.open (local_address.get_version ());

        if (not listening_socket.is_open ())
        {
            throw NetworkException("Failed to create TCP socket for listening.");
        }

        try
        {
            listening_socket.bind (local_address, local_port);

            if (::listen (static_cast<SOCKET> (listening_socket.get_handle ()), backlog) == SOCKET_ERROR)
            {
                throw NetworkException("Failed to put TCP socket in listen mode.");
            }

            listening_socket.set_blocking (false);
        }
        catch (...)
        {
            listening_socket.close (true);
            throw;
        }
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    std::optional<TcpSocket> TcpListener::accept ()
    {
        if (this->is_not_listening ())
        {
            throw NetworkException("Trying to accept a TCP connection while not listening.");
        }

        sockaddr_storage remote_address_data{};

        int remote_address_size = sizeof(remote_address_data);

        auto client_handle = ::accept
        (
            static_cast<SOCKET>(listening_socket.get_handle()), 
            reinterpret_cast<sockaddr*>(&remote_address_data), 
            &remote_address_size
        );

        if (client_handle == INVALID_SOCKET)
        {
            int error =  WSAGetLastError ();

            if (error == WSAEWOULDBLOCK)                        // No pending connections are present to be accepted,
            {                                                   // and the socket is marked as non-blocking
                return {};
            }

            throw NetworkException("Failed to accept new TCP connection.", error);
        }

        Address::Version remote_address_version;
        
        if (remote_address_data.ss_family == AF_INET)
        {
            remote_address_version = Address::Version::V4;
        }
        else if (remote_address_data.ss_family == AF_INET6)
        {
            remote_address_version = Address::Version::V6;
        }
        else
        {
            closesocket (client_handle);                        // The accepted connection has an unsupported address family,
            return {};                                          // so we close the handle and return an empty optional
        }

        return TcpSocket(remote_address_version, Socket::cast<Socket::Handle> (client_handle));
    }

}
