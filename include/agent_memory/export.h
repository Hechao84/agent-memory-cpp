#pragma once

/** Symbol visibility macro for the agent_memory shared library public ABI. */
#if defined(_WIN32) || defined(__CYGWIN__)
    #ifdef BUILDING_AGENT_MEMORY
        #define AGENT_MEMORY_API __declspec(dllexport)
    #else
        #define AGENT_MEMORY_API __declspec(dllimport)
    #endif
#else
    #define AGENT_MEMORY_API __attribute__((visibility("default")))
#endif
