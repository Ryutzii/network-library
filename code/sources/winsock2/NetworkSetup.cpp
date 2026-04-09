
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <NetworkSetup.hpp>
#include "winsock2.hpp"

namespace argb
{

    NetworkSetup::NetworkSetup() : initialized(false)
    {
        WSADATA wsa_data;

        if (WSAStartup (MAKEWORD(2, 2), &wsa_data) != 0)
        {
            throw NetworkException ("Failed to initialize WinSock");
        }

        initialized = true;
    }

    NetworkSetup::~NetworkSetup()
    {
        if (initialized)
        {
            WSACleanup ();
        }
    }

}
