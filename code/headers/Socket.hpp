
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <chrono>
#include <cstddef>
#include "Endpoint.hpp"
#include "NetworkException.hpp"
#include <optional>
#include <span>
#include <type_traits>

namespace argb
{

    class Socket
    {
    public:

        using  Handle = uintptr_t;

        /// This constant represents the case where a receive operation on a socket returns successfully but there is
        /// currently no data available to receive. This is different from TcpSocket::receive_closed, which indicates
        /// that the socket has been closed by the peer and no more data can be received.
        static constexpr size_t receive_empty = 0;

    protected:

        static const Handle invalid_handle;

        Handle handle;
        Address::Version specification;

    public:

        Socket()
        {
            handle = invalid_handle;
            specification = Address::Version::ANY;
        }

        virtual ~Socket() noexcept
        {
            close ();
        }

    protected:

        Socket(Address::Version version, Handle given_handle)
            : handle(given_handle)
            , specification(version)
        {
        }

    public:

        Socket(Socket&& other) noexcept : specification(other.specification)
        {
            this->handle =   other.handle;
            other.handle = invalid_handle;
        }

        Socket & operator = (Socket && other) noexcept
        {
            if (this != &other)
            {
                this->close ();

                this->handle =   other.handle;
                other.handle = invalid_handle;
            }

            return *this;
        }

        Socket(const Socket & ) = delete;
        Socket & operator = (const Socket & ) = delete;

    public:

        bool is_open () const
        {
            return handle != invalid_handle;
        }

        Handle get_handle () const
        {
            return handle;
        }

        std::optional<Endpoint> get_local_endpoint  () const;
        std::optional<Endpoint> get_remote_endpoint () const;

        size_t get_available_bytes_for_receive () const;
        
    public:

        void set_blocking        (bool block);
        void set_send_timeout    (std::chrono::milliseconds timeout_duration);
        void set_receive_timeout (std::chrono::milliseconds timeout_duration);

    public:

        virtual void open (Address::Version version) = 0;

        /** Closes the socket and releases any associated resources.
          * @param force_close If true, forces an immediate closure of the socket by sending a RST (reset) packet to the
          *     peer, which can help avoid the TIME_WAIT state. If false, allows for a graceful shutdown, which may result
          *     in the socket entering the TIME_WAIT state if it was used for an active connection.
          *     Skipping the TIME_WAIT state may cause errors on the remote side if the remote peer is still trying to
          *     receive data which was sent but not yet received. However, it can be useful in some scenarios.
          * @return true if the socket was closed or was already closed, false if an error occurred during closure.
          */
        bool close (bool force_close = false) noexcept;     // force_close = false allows the socket to perform a graceful shutdown allowing the TIME_WAIT state if necessary

        void bind (const Address &  local_address, const Port &  local_port);
        void connect (const Address & remote_address, const Port & remote_port);

        virtual size_t send    (std::span<const std::byte> data  ) const = 0;
        virtual size_t receive (std::span<      std::byte> buffer) const = 0;

    public:

        void bind (const Port & local_port)
        {
            bind (Address::any, local_port);
        }

        template< typename TARGET_TYPE, typename SOURCE_TYPE >
        static TARGET_TYPE cast (const SOURCE_TYPE handle)
        {
            static_assert(std::is_integral_v<SOURCE_TYPE> && std::is_integral_v<TARGET_TYPE>);
            assert(static_cast<TARGET_TYPE>(handle) == handle);             // Ensure that the cast does not lose information
            return static_cast<TARGET_TYPE>(handle);
        }

    protected:

        void throw_exception_if_closed (const char * message = "Trying to operate on an invalid socket.") const
        {
            if (not is_open ())
            {
                throw NetworkException(message);
            }
        }

    };

}
