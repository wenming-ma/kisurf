if( NOT DEFINED KISURF_RUNTIME_DEST OR KISURF_RUNTIME_DEST STREQUAL "" )
    message( FATAL_ERROR "KISURF_RUNTIME_DEST is required" )
endif()

if( NOT DEFINED KISURF_RUNTIME_DLLS OR KISURF_RUNTIME_DLLS STREQUAL "" )
    return()
endif()

string( REPLACE "," ";" _runtime_dlls "${KISURF_RUNTIME_DLLS}" )

foreach( _runtime_dll IN LISTS _runtime_dlls )
    if( _runtime_dll STREQUAL "" )
        continue()
    endif()

    if( NOT EXISTS "${_runtime_dll}" )
        message( FATAL_ERROR "Runtime DLL does not exist: ${_runtime_dll}" )
    endif()

    get_filename_component( _runtime_dll_name "${_runtime_dll}" NAME )
    file( COPY_FILE "${_runtime_dll}"
                    "${KISURF_RUNTIME_DEST}/${_runtime_dll_name}"
                    ONLY_IF_DIFFERENT )
endforeach()
