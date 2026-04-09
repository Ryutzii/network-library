
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include "winsock2.hpp"

namespace argb
{

    void translate_address
    (
        Address::Version   specification,
        const Address    & address,
        const Port       & port,
        sockaddr_storage & address_data,
        int              & address_size
    )
    {
        if (specification == Address::Version::V4)
        {
            if (address.is_v6 ())
            {
                throw NetworkException("Cannot use an IPv6 address with an IPv4 socket.");
            }

            auto & address_data_v4 = reinterpret_cast<sockaddr_in &>(address_data);

            address.convert_to (address_data_v4);

            address_data_v4.sin_port = htons (static_cast<uint16_t>(port));

            address_size = sizeof(sockaddr_in);
        }
        else
        {
            if (specification == Address::Version::V6 && address.is_v4())
            {
                throw NetworkException("Cannot use an IPv4 address with an IPv6 only socket.");
            }

            auto & address_data_v6 = reinterpret_cast<sockaddr_in6 &>(address_data);

            address.convert_to (address_data_v6);

            address_data_v6.sin6_port = htons (static_cast<uint16_t>(port));

            address_size = sizeof(sockaddr_in6);
        }
    }

    // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // // //

    void translate_address
    (
        const sockaddr_storage & address_data,
        int                      address_size, 
        Address                & address,
        Port                   & port
    )
    {
        if (address_data.ss_family == AF_INET)
        {
            const auto & address_data_v4 = reinterpret_cast<const sockaddr_in &>(address_data);

            address = Address::create_from (address_data_v4);
            port    = Port(ntohs (address_data_v4.sin_port));
        } 
        else
        if (address_data.ss_family == AF_INET6)
        {
            const auto & address_data_v6 = reinterpret_cast<const sockaddr_in6 &>(address_data);

            address = Address::create_from (address_data_v6);
            port    = Port(ntohs (address_data_v6.sin6_port));
        }
        else
        {
            throw NetworkException("Received data from an unknown address family.");
        }
    }

}
