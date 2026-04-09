
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include "Socket.hpp"

namespace argb
{

    class UdpSocket final : public Socket
    {

    public:

        UdpSocket() = default;

		UdpSocket(Address::Version version)
        {
            open (version);
        }

       ~UdpSocket() = default;

    public:

        void   open         (Address::Version version) override;
        void   disconnect   ();
        size_t send         (const std::span<const std::byte> data) const override;
        size_t send_to      (const std::span<const std::byte> data, const Address & remote_address, const Port & remote_port) const;
        size_t receive      (std::span<std::byte> buffer) const override;
        size_t receive_from (std::span<std::byte> buffer, Address & remote_address, Port & remote_port) const;

    private:

        void   handle_send_error    () const;
        size_t handle_receive_error () const;

    };

}
