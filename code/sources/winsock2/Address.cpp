
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <Address.hpp>
#include <NetworkException.hpp>
#include "winsock2.hpp"

namespace argb
{

    const Address Address::any{};
    const Address Address::null_v4{ 0, 0, 0, 0 };
    const Address Address::null_v6{ 0, 0, 0, 0, 0, 0, 0, 0 };

    template< >
    void Address::convert_to (sockaddr_in & address_data) const
    {
        if (version == V6)
        {
            throw NetworkException("Trying to convert an IPv6 address to sockaddr_in.");
        }

        clear (address_data);

        address_data.sin_family = AF_INET;

        if (version == ANY || this->data.v4 == any.data.v4)
        {
            address_data.sin_addr.s_addr = INADDR_ANY;
        }
        else
        {
            std::copy (data.v4.begin (), data.v4.end (), reinterpret_cast<uint8_t *>(&address_data.sin_addr.s_addr));
        }
    }

    template< >
    void Address::convert_to (sockaddr_in6 & address_data) const
    {
        clear (address_data);

        address_data.sin6_family = AF_INET6;

        if (version == ANY || this->data.v6 == any.data.v6)
        {
            address_data.sin6_addr = in6addr_any;
        }
        else
        {
            if (version == V4)
            {
                // The ::ffff: prefix is in bytes 10 and 11:

                address_data.sin6_addr.s6_addr[10] = 0xFF;
                address_data.sin6_addr.s6_addr[11] = 0xFF;

                // Last 4 bytes are the IPv4 address:

                std::copy (data.v4.begin (), data.v4.end (), &address_data.sin6_addr.s6_addr[12]);
            }
            else
            {
                auto * target_ptr = reinterpret_cast<u_short *>(address_data.sin6_addr.s6_addr);

                for (const auto value : data.v6)
                {
                    *target_ptr++ = htons (value);
                }
            }
        }
    }

    template<>
    Address Address::create_from<sockaddr_in> (const sockaddr_in & address_data)
    {
        const auto * address_data_v4 = reinterpret_cast<const uint8_t *>(&address_data.sin_addr.s_addr);
    
        return Address(address_data_v4[0], address_data_v4[1], address_data_v4[2], address_data_v4[3]);
    }

    template<>
    Address Address::create_from<sockaddr_in6> (const sockaddr_in6 & address_data)
    {
        const auto * address_bytes = address_data.sin6_addr.s6_addr;

        // Detect if this is an IPv4-mapped IPv6 address (::ffff:a.b.c.d):

        bool is_ipv4_mapped = true;

        if (address_bytes[10] != 0xFF || address_bytes[11] != 0xFF)
        {
            is_ipv4_mapped = false;
        }
        else for (int i = 0; i < 10; ++i)
        {
            if (address_bytes[i] != 0)
            {
                is_ipv4_mapped = false;
                break;
            }
        }

        if (is_ipv4_mapped)
        {
            // Return an IPv4 address using the last 4 bytes of the IPv6 address:

            return Address(address_bytes[12], address_bytes[13], address_bytes[14], address_bytes[15]);
        }
        else
        {
            auto address_words = reinterpret_cast<const u_short *>(address_bytes);
        
            return Address
            (
                ntohs (address_words[0]), ntohs (address_words[1]), ntohs (address_words[2]), ntohs (address_words[3]),
                ntohs (address_words[4]), ntohs (address_words[5]), ntohs (address_words[6]), ntohs (address_words[7])
            );
        }
    }

}
