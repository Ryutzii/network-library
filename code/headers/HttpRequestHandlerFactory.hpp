
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <string_view>
#include <HttpRequestHandler.hpp>

namespace argb
{

	class HttpRequestHandlerFactory
	{
	public:

		virtual HttpRequestHandler::Ptr create_handler(HttpRequest::Method method, std::string_view request_path) = 0;

	};

}
