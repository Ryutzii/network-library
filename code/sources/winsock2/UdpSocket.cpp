
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <cassert>
#include <NetworkException.hpp>
#include <UdpSocket.hpp>
#include "winsock2.hpp"

namespace argb
{

    void UdpSocket::open (Address::Version version)
    {
        if (is_open ())
        {
            throw NetworkException("Trying to open an already open socket.");
        }

        specification = version;

        handle = cast<Handle> (::socket (version == Address::Version::V4 ? AF_INET : AF_INET6, SOCK_DGRAM, IPPROTO_UDP));

        if (not is_open ())
        {
            throw NetworkException("Failed to create UDP socket.");
        }

        if (version == Address::Version::ANY)
        {
            // This socket will be used for both IPv4 and IPv6, so we need to disable the IPV6_V6ONLY option:

            const int disable_ipv6_only = 0;

            if (set_socket_option (cast<SOCKET> (handle), IPPROTO_IPV6, IPV6_V6ONLY, disable_ipv6_only) == false)
            {
                throw NetworkException("Failed to disable IPV6_V6ONLY option on UDP socket.");
            }
        }
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    void UdpSocket::disconnect ()
    {
        throw_exception_if_closed ("Trying to disconnect an invalid socket.");

        sockaddr_storage address_data{};

        address_data.ss_family = AF_UNSPEC;

        if (::connect (cast<SOCKET> (handle), reinterpret_cast<sockaddr *>(&address_data), sizeof(address_data)) == SOCKET_ERROR)
        {
            throw NetworkException("Failed to disconnect UDP socket.");
        }
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    size_t UdpSocket::send (const std::span<const std::byte> data) const
    {
        throw_exception_if_closed ("Trying to send data on an invalid socket.");

        int sent = ::send (cast<SOCKET> (handle), reinterpret_cast<const char*>(data.data ()), static_cast<int>(data.size ()), 0);

        if (sent == SOCKET_ERROR)
        {
            handle_send_error ();
            return 0;                               // If an exception was not thrown, we just return 0 to indicate no data sent
        }

        return static_cast<size_t>(sent);
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    size_t UdpSocket::send_to (const std::span<const std::byte> data, const Address & remote_address, const Port & remote_port) const
    {
        throw_exception_if_closed ("Trying to send data on an invalid socket.");

        sockaddr_storage address_data{};
        int address_size = 0;

        translate_address (specification, remote_address, remote_port, address_data, address_size);

        int sent = ::sendto
        (
            cast<SOCKET> (handle),
            reinterpret_cast<const char *>(data.data ()),
            static_cast<int>(data.size ()),
            0,
            reinterpret_cast<sockaddr *>(&address_data),
            address_size
        );

        if (sent == SOCKET_ERROR)
        {
            handle_send_error ();
            return 0;                               // If an exception was not thrown, we just return 0 to indicate no data sent
        }

        return static_cast<size_t>(sent);
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    size_t UdpSocket::receive (std::span<std::byte> buffer) const
    {
        throw_exception_if_closed ("Trying to receive data on an invalid socket.");

        int received = ::recv
        (
            cast<SOCKET> (handle),
            reinterpret_cast<char*>(buffer.data ()),
            static_cast<int>(buffer.size ()),
            0
        );

        if (received == SOCKET_ERROR)
        {
            return handle_receive_error ();
        }

        return static_cast<size_t>(received);
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    size_t UdpSocket::receive_from (std::span<std::byte> buffer, Address & remote_address, Port & remote_port) const
    {
        throw_exception_if_closed ("Trying to receive data on an invalid socket.");

        sockaddr_storage remote_info{};
        int remote_info_size = sizeof(sockaddr_storage);

        int received = ::recvfrom
        (
            cast<SOCKET>(handle),
            reinterpret_cast<char *>(buffer.data ()),
            static_cast<int>(buffer.size ()),
            0,
            reinterpret_cast<sockaddr *>(&remote_info),
            &remote_info_size
        );

        if (received == SOCKET_ERROR)
        {
            handle_receive_error ();
            return 0;                               // If an exception was not thrown, we just return 0 to indicate no data received
        }

        translate_address (remote_info, remote_info_size, remote_address, remote_port);

        return static_cast<size_t>(received);
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    void UdpSocket::handle_send_error () const
    {
        int error =  WSAGetLastError();

        if (error == WSAEWOULDBLOCK)
        {
            return;
        }

        throw NetworkException("Failed to send UDP data.", error);
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    size_t UdpSocket::handle_receive_error () const
    {
        switch (int error = WSAGetLastError ())
        {
            case WSAEMSGSIZE:                       // The message was too large to fit into the provided buffer and was truncated
            {
                throw NetworkException("Received packet is larger than the provided buffer.", WSAEMSGSIZE);
            }
            case WSAETIMEDOUT:                      // Timeout expired (if configured with setsockopt)
            {
                throw NetworkException("Receive operation timed out.", WSAETIMEDOUT);
            }
            case WSAEWOULDBLOCK:                    // No data available in non-blocking mode
            case WSAECONNRESET:                     // The remote host closed the connection (ICMP Port Unreachable for UDP)
            {
                return receive_empty;
            }
            default:
            {
                throw NetworkException("Error receiving UDP data.", error);
            }
        }
    }

}
