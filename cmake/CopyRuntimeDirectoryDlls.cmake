if( NOT DEFINED KISURF_RUNTIME_DEST OR KISURF_RUNTIME_DEST STREQUAL "" )
    message( FATAL_ERROR "KISURF_RUNTIME_DEST is required" )
endif()

if( NOT DEFINED KISURF_RUNTIME_DLL_DIRS OR KISURF_RUNTIME_DLL_DIRS STREQUAL "" )
    return()
endif()

string( REPLACE "," ";" _runtime_dll_dirs "${KISURF_RUNTIME_DLL_DIRS}" )

foreach( _runtime_dll_dir IN LISTS _runtime_dll_dirs )
    if( _runtime_dll_dir STREQUAL "" OR NOT IS_DIRECTORY "${_runtime_dll_dir}" )
        continue()
    endif()

    file( GLOB _runtime_dlls "${_runtime_dll_dir}/*.dll" )

    foreach( _runtime_dll IN LISTS _runtime_dlls )
        get_filename_component( _runtime_dll_name "${_runtime_dll}" NAME )

        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${_runtime_dll}"
                    "${KISURF_RUNTIME_DEST}/${_runtime_dll_name}"
            RESULT_VARIABLE _copy_result
        )

        if( NOT _copy_result EQUAL 0 )
            message( FATAL_ERROR "Failed to copy runtime DLL ${_runtime_dll}" )
        endif()
    endforeach()
endforeach()
