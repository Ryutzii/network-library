
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

namespace argb
{

    class Address
    {
    public:

        /// Represents the version of an IP address which matches the Internet Protocol version used for the address.
        /// It determines how the address data is interpreted.
        enum Version
        {
            /// IPv4 address (32 bits) which is the most widely used version of the Internet Protocol.
            V4,
            
            /// IPv6 address (128 bits) which can also represent an IPv4-mapped IPv6 address (::ffff:a.b.c.d) to allow
            /// compatibility with IPv4-only applications and systems.
            V6,

            /// Represents an IP address which in some contexts can be used as a wildcard to match any IP address.
            /// It's all zeroes for both IPv4 and IPv6, so it can be used in contexts where either version is expected.
            /// However, it does not represent a valid IP address that can be assigned to a network interface or used
            /// for communication.
            ANY                                 
        };

        static const Address any;
        static const Address broadcast;
        static const Address multicast;
        static const Address null_v4;
        static const Address null_v6;

    private:

        /// The address data is stored in a union to allow efficient storage of either an IPv4 or IPv6 address without
        /// wasting memory.
        union alignas(16) Data
        {
            /// The 4 bytes of an IPv4 address, stored in network byte order (big-endian).
            std::array<uint8_t,  4> v4;

            /// The 8 16-bit words of an IPv6 address, stored in network byte order (big-endian).
            std::array<uint16_t, 8> v6;

            /// The default constructor initializes the address to zero, which corresponds to the "any" address for both
            /// IPv4 and IPv6.
            constexpr Data() : v6() {}
        };

        /// The actual address data, either IPv4 or IPv6 depending on the version.
        Data data;

        /// The version of the IP address, which determines how to interpret the address data.
        Version version;

    public:

        /// Default constructor initializes the IP address to the "any" address, which can be used as a wildcard in
        /// contexts where either IPv4 or IPv6 is expected. It does not represent a valid address that can be used for
        /// communication, but it can be useful for certain operations like binding a socket to all available interfaces.
        explicit constexpr Address() : version(ANY)
        {
        }

        constexpr Address(uint8_t  a, uint8_t  b, uint8_t  c, uint8_t  d)
        {
            version = V4;
            data.v4 = { a, b, c, d };
        }

        constexpr Address(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e, uint16_t f, uint16_t g, uint16_t h)
        {
            version = V6;
            data.v6 = { a, b, c, d, e, f, g, h };
        }

    public:

        Version get_version () const
        {
            return version;
        }

        bool is_v4 () const
        {
            return version == V4 || version == ANY;
        }

        bool is_v6 () const
        {
            return version == V6 || version == ANY;
        }

        bool is_any () const
        {
            return version == ANY;
        }

        const std::array<uint8_t, 4> * get_ipv4 () const
        {
            return is_v4 () ? &data.v4 : nullptr;
        }

        const std::array<uint16_t, 8> * get_ipv6 () const
        {
            return is_v6 () ? &data.v6 : nullptr;
        }

    public:

        bool operator == (const Address & other) const
        {
            return this->version == other.version &&
            (
                version == V4 || version == ANY
                    ? this->data.v4 == other.data.v4
                    : this->data.v6 == other.data.v6
            );
        }

        auto operator <=> (const Address & other) const
        {
            return this->version != other.version
                ? this->version <=> other.version
                : version == V4 || version == ANY
                    ? this->data.v4 <=> other.data.v4
                    : this->data.v6 <=> other.data.v6;
        }

    public:

        template< typename TYPE >
        static Address create_from (const TYPE & address_data);

        template< typename TYPE >
        void convert_to (TYPE & address_data) const;

    };

    static_assert(std::is_trivially_copyable_v<Address>);
    static_assert(alignof(Address) == 16);

}
