
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <algorithm>
#include <Address.hpp>
#include <Port.hpp>
#include <NetworkException.hpp>
#include <WinSock2.h>
#include <WS2tcpip.h>

namespace argb
{

    inline void clear (sockaddr_in  & address_data)
    {
        std::fill_n (reinterpret_cast<char *>(&address_data), sizeof(address_data), 0);
    }

    inline void clear (sockaddr_in6 & address_data)
    {
        std::fill_n (reinterpret_cast<char *>(&address_data), sizeof(address_data), 0);
    }

    template< typename TYPE >
    inline bool set_socket_option (SOCKET handle, int level, int option, const TYPE & value)
    {
        return ::setsockopt
        (
            handle, 
            level, 
            option, 
            reinterpret_cast<const char *>(&value),
            sizeof(value)
        )
        == 0;
    }

    inline void set_exclusive_address_use (SOCKET handle)
    {
        const int set_exclusive_address = 1;

        if (not set_socket_option (handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, set_exclusive_address))
        {
            throw NetworkException("Failed to set exclusive address use on socket.");
        }
    }

    void translate_address
    (
        Address::Version   specification,
        const Address    & address_in,
        const Port       & port_in,
        sockaddr_storage & address_data_out,
        int              & address_size_out
    );

    void translate_address
    (
        const sockaddr_storage & address_data_in,
        int                      address_size_in,
        Address                & address_out,
        Port                   & port_out
    );

}
