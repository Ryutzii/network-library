
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <NetworkException.hpp>
#include <WinSock2.h>

namespace argb
{

    int NetworkException::last_error_code () noexcept
    {
        return WSAGetLastError ();
    }

}
