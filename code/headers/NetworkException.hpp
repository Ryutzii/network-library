
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <exception>
#include <iostream>
#include <source_location>

namespace argb
{

    class NetworkException : public std::exception 
    {
    private:

        const char         * message;
        int                  error_code;
        std::source_location throw_location;

    public:

        explicit NetworkException
        (
            const char         * message, 
            int                  error_code     = last_error_code (), 
            std::source_location throw_location = std::source_location::current ()
        ) noexcept
            : message(message)
            , error_code(error_code)
            , throw_location(throw_location) 
        {
        }

        const char * what () const noexcept override
        {
            return message ? message : "Socket error";
        }

        int get_native_error_code () const
        {
            return error_code;
        }

        const std::source_location & get_throw_location () const
        {
            return throw_location;
        }

    private:

        static int last_error_code () noexcept;

    };

    inline std::ostream & operator << (std::ostream & ostream, const NetworkException & exception)
    {
        return ostream
            << exception.what ()
            << " (error code: " << exception.get_native_error_code ()
            << ", at: " << exception.get_throw_location ().file_name () << ":" << exception.get_throw_location ().line () << ")";
    }

}
