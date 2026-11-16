# Hard-link the RTX Remix runtime (SRC) into the exe's folder (DST).
#   cmake -DSRC=<runtime dir> -DDST=<exe dir> -P LinkRemixRuntime.cmake
# Skips files that are already there, and anything Remix writes at runtime.

file(GLOB_RECURSE files RELATIVE "${SRC}" "${SRC}/*")
set(count 0)
foreach(file ${files})
    if(file MATCHES "^rtx-remix/" OR file MATCHES "\\.(log|exe|dxvk-cache)$" OR file STREQUAL "metrics.txt")
        if(NOT file STREQUAL "crashpad_handler.exe")
            continue()
        endif()
    endif()
    if(NOT EXISTS "${DST}/${file}")
        get_filename_component(dir "${DST}/${file}" DIRECTORY)
        file(MAKE_DIRECTORY "${dir}")
        file(CREATE_LINK "${SRC}/${file}" "${DST}/${file}" COPY_ON_ERROR)
        math(EXPR count "${count} + 1")
    endif()
endforeach()
if(count GREATER 0)
    message(STATUS "Linked ${count} RTX Remix runtime files into ${DST}")
endif()
