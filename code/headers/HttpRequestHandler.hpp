
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <HttpRequest.hpp>
#include <HttpResponse.hpp>

namespace argb
{

    class HttpRequestHandler
    {
    public:

        virtual void handle_request (HttpMessage::Id id, const HttpRequest & request, HttpResponse & response) = 0;

    };

}
