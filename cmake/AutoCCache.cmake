# AutoCCache.cmake - Automatic ccache detection and setup

# Prevent multiple configurations
if(DEFINED CCACHE_CONFIGURED)
    return()
endif()

find_program(CCACHE_PROGRAM ccache)

if(CCACHE_PROGRAM)
    message(STATUS "Found ccache: ${CCACHE_PROGRAM}")
    
    # Check ccache version
    execute_process(
        COMMAND ${CCACHE_PROGRAM} --version
        OUTPUT_VARIABLE CCACHE_VERSION_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    
    if(CCACHE_VERSION_OUTPUT MATCHES "ccache version ([0-9.]+)")
        set(CCACHE_VERSION ${CMAKE_MATCH_1})
        message(STATUS "ccache version: ${CCACHE_VERSION}")
    endif()
    
    # Set compiler launchers
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    
    # Optional: Set some ccache options
    if(WIN32)
        # Use existing CCACHE_DIR if set, otherwise use build directory
        if(NOT DEFINED ENV{CCACHE_DIR})
            set(ENV{CCACHE_DIR} "${CMAKE_BINARY_DIR}/.ccache")
        endif()
        set(ENV{CCACHE_MAXSIZE} "5G")
        set(ENV{CCACHE_SLOPPINESS} "pch_defines,time_macros")
    endif()
    
    # Show ccache stats
    execute_process(
        COMMAND ${CCACHE_PROGRAM} -s
        OUTPUT_VARIABLE CCACHE_STATS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    
    message(STATUS "ccache stats:")
    message(STATUS "${CCACHE_STATS}")
    
    # Mark as configured to prevent re-execution
    set(CCACHE_CONFIGURED TRUE CACHE INTERNAL "ccache has been configured")
    
else()
    message(STATUS "ccache not found - compilation will proceed without caching")
    set(CCACHE_CONFIGURED FALSE CACHE INTERNAL "ccache configuration attempted")
endif()