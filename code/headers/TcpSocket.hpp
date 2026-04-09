
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <cstddef>
#include "Address.hpp"
#include "Port.hpp"
#include "Socket.hpp"
#include <span>

namespace argb
{

    class TcpSocket final : public Socket
    {
    public:

        /// Indicates that the socket has been closed by the peer, so no more data can be received.
        /// This is different from receive_empty which indicates that there is currently no data available to receive
        /// but the socket is still open.
        static constexpr size_t receive_closed = static_cast<size_t>(-1);

    public:

        TcpSocket() = default;

        TcpSocket(Address::Version version)
        {
            open (version);
        }

        TcpSocket(Address::Version version, Handle given_handle)
            : Socket(version, given_handle)
        {
        }
    
    public:

        void   open    (Address::Version version) override;
        size_t send    (const std::span<const std::byte> data) const override;
        size_t receive (std::span<std::byte> buffer) const override;

        void shutdown_send    ();
        void shutdown_receive ();

    private:

        size_t handle_receive_error () const;

    };

}
