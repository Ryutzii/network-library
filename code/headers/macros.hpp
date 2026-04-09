
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#if defined _WIN32 || defined _WIN64

    #define WINDOWS_OS

#elif defined __ANDROID__ || defined __ANDROID_NDK__

    #define ANDROID_OS

#elif defined __APPLE__

    #include <TargetConditionals.h>

    #if defined TARGET_OS_IOS

        #define IOS

    #elif defined TARGET_OS_OSX

        #define MAC_OS

    #else
        #error "Unsupported Apple operating system."
    #endif

#elif defined __linux__ || defined __gnu_linux__ || defined __linux

    #define LINUX_OS

#else

    #error "Unsupported operating system."

#endif
