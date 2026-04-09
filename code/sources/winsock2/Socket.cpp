
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <cassert>
#include <Socket.hpp>
#include <NetworkException.hpp>
#include "winsock2.hpp"

namespace argb
{

    const Socket::Handle Socket::invalid_handle = Socket::cast<Handle> (INVALID_SOCKET);

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    std::optional<Endpoint> Socket::get_local_endpoint  () const
    {
        sockaddr_storage address_data{};
        int              address_size = sizeof(address_data);

        if (::getsockname (cast<SOCKET>(handle), reinterpret_cast<sockaddr*>(&address_data), &address_size) == SOCKET_ERROR)
        {
            int error =  WSAGetLastError ();

            if (error == WSAEINVAL || error == WSAENOTCONN)     // The socket is not binded to an address or not connected
            {
                return {};
            }

            throw NetworkException("Failed to query local endpoint of socket.", error);
        }

        Endpoint local_endpoint;

        translate_address (address_data, address_size, local_endpoint.data, local_endpoint.port);

        return local_endpoint;
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    std::optional<Endpoint> Socket::get_remote_endpoint () const
    {
        sockaddr_storage address_data{};
        int              address_size = sizeof(address_data);

        if (::getpeername (cast<SOCKET>(handle), reinterpret_cast<sockaddr*>(&address_data), &address_size) == SOCKET_ERROR)
        {
            int error =  WSAGetLastError ();

            if (error == WSAENOTCONN)               // The socket is not connected, so it does not have a remote endpoint
            {
                return {};
            }

            throw NetworkException("Failed to query remote endpoint of socket.", error);
        }

        Endpoint remote_endpoint;

        translate_address(address_data, address_size, remote_endpoint.data, remote_endpoint.port);

        return remote_endpoint;
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    size_t Socket::get_available_bytes_for_receive () const
    {
        throw_exception_if_closed ();

        u_long bytes_available = 0;

        if (::ioctlsocket (cast<SOCKET> (handle), FIONREAD, &bytes_available) == SOCKET_ERROR)
        {
            throw NetworkException("Failed to query available bytes.");
        }

        return static_cast<size_t>(bytes_available);
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    void Socket::set_blocking (bool block)
    {
        if (not is_open ())
        {
            throw NetworkException("Trying to set blocking mode on an invalid socket.");
        }

        u_long mode = block ? 0 : 1;

        if (::ioctlsocket (cast<SOCKET> (handle), FIONBIO, &mode) == SOCKET_ERROR)
        {
            throw NetworkException("Failed to set blocking mode on socket.");
        }
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    void Socket::set_send_timeout (std::chrono::milliseconds timeout_duration)
    {
		if (not is_open ())
        {
            throw NetworkException("Trying to set send timeout on an invalid socket.");
        }

        const auto timeout = static_cast<DWORD>(timeout_duration.count ());

        if (not set_socket_option (cast<SOCKET> (handle), SOL_SOCKET, SO_SNDTIMEO, timeout))
        {
            throw NetworkException("Failed to set send timeout.");
        }
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    void Socket::set_receive_timeout (std::chrono::milliseconds timeout_duration)
    {
        if (not is_open ()) 
        {
			throw NetworkException("Trying to set receive timeout on an invalid socket.");
        }

        const auto timeout = static_cast<DWORD>(timeout_duration.count ());
    
        if (not set_socket_option (cast<SOCKET> (handle), SOL_SOCKET, SO_RCVTIMEO, timeout))
        {
            throw NetworkException("Failed to set receive timeout.");
        }
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    void Socket::bind (const Address & local_address, const Port & local_port)
    {
        throw_exception_if_closed ("Trying to bind an invalid socket.");
        
        sockaddr_storage address_data{};
        int address_size = 0;

        translate_address (specification, local_address, local_port, address_data, address_size);

        set_exclusive_address_use (cast<SOCKET> (handle));

        if (::bind (cast<SOCKET> (handle), reinterpret_cast<sockaddr *>(&address_data), address_size) == SOCKET_ERROR)
        {
            if (WSAGetLastError () == WSAEADDRINUSE)
            {
                throw NetworkException("Address and port are already in use.");
            }
            throw NetworkException("Failed to bind UDP socket to address and port.");
        }
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    void Socket::connect (const Address & remote_address, const Port & remote_port)
    {
        throw_exception_if_closed ("Trying to connect an invalid socket.");

        sockaddr_storage address_data{};
        int address_size = 0;

        translate_address (specification, remote_address, remote_port, address_data, address_size);

        if (::connect (cast<SOCKET> (handle), reinterpret_cast<sockaddr *>(&address_data), address_size) == SOCKET_ERROR)
        {
            throw NetworkException("Failed to connect UDP socket to remote host.");
        }
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    bool Socket::close (bool force_close) noexcept
    {
        if (not is_open ())
        {
            return true;                    // Socket is already closed, so we can consider the close operation successful
        }

        if (force_close)
        {
          ::linger linger;
            linger.l_onoff  = 1;            // Enable linger
            linger.l_linger = 0;            // Zero linger time produces a RST (Reset) packet to be sent to the peer

            set_socket_option (cast<SOCKET> (handle), SOL_SOCKET, SO_LINGER, linger);
        }

        int result = ::closesocket (cast<SOCKET> (handle));

        handle = invalid_handle;

        return result != SOCKET_ERROR;
    }

}
