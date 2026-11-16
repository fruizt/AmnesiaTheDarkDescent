# Downloads the third-party libraries we build from source into external/.
#
#   cmake -P fetch-deps.cmake
#
# Each entry is pinned to an exact version and SHA-256, so everyone gets the
# same bytes. Already-downloaded libraries are skipped; delete the folder to
# fetch again.

cmake_minimum_required(VERSION 3.20)

set(EXTERNAL_DIR "${CMAKE_CURRENT_LIST_DIR}/external")

# fetch_dep(<folder> <url> <sha256> [SUBDIR <path inside the archive>])
# With SUBDIR, only that folder of the archive is kept, as external/<folder>.
function(fetch_dep folder url sha256)
    cmake_parse_arguments(ARG "" "SUBDIR" "" ${ARGN})
    set(dest "${EXTERNAL_DIR}/${folder}")
    if(EXISTS "${dest}")
        message(STATUS "${folder}: already present")
        return()
    endif()

    set(archive "${EXTERNAL_DIR}/${folder}.download")
    message(STATUS "${folder}: downloading ${url}")
    file(DOWNLOAD "${url}" "${archive}"
        EXPECTED_HASH SHA256=${sha256}
        STATUS status)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        file(REMOVE "${archive}")
        message(FATAL_ERROR "${folder}: download failed: ${status}")
    endif()

    if(ARG_SUBDIR)
        set(tmp "${EXTERNAL_DIR}/${folder}.extract")
        file(REMOVE_RECURSE "${tmp}")
        file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${tmp}"
            PATTERNS "*/${ARG_SUBDIR}/*")
        file(GLOB subdir LIST_DIRECTORIES true "${tmp}/*/${ARG_SUBDIR}")
        if(NOT subdir)
            message(FATAL_ERROR "${folder}: '${ARG_SUBDIR}' not found in archive")
        endif()
        file(RENAME "${subdir}" "${dest}")
        file(REMOVE_RECURSE "${tmp}")
    else()
        file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${dest}")
    endif()
    file(REMOVE "${archive}")
    message(STATUS "${folder}: extracted")
    set(FETCHED_${folder} TRUE PARENT_SCOPE)
endfunction()

# fetch_file(<path under external/> <url> <sha256>): a single file, not an archive
function(fetch_file path url sha256)
    set(dest "${EXTERNAL_DIR}/${path}")
    if(EXISTS "${dest}")
        return()
    endif()
    message(STATUS "${path}: downloading ${url}")
    file(DOWNLOAD "${url}" "${dest}"
        EXPECTED_HASH SHA256=${sha256}
        STATUS status)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        file(REMOVE "${dest}")
        message(FATAL_ERROR "${path}: download failed: ${status}")
    endif()
endfunction()

# patch_dep(<folder> <file inside it> <old text> <new text>)
# Our fixes to third-party code, applied right after a fresh download.
# Fails loudly if <old text> isn't there, so a changed download can't slip by.
function(patch_dep folder path old new)
    if(NOT FETCHED_${folder})
        return()
    endif()
    set(target "${EXTERNAL_DIR}/${folder}/${path}")
    file(READ "${target}" content)
    string(FIND "${content}" "${old}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "${folder}: patch for ${path} does not apply: '${old}' not found")
    endif()
    string(REPLACE "${old}" "${new}" content "${content}")
    file(WRITE "${target}" "${content}")
    message(STATUS "${folder}: patched ${path}")
endfunction()

# AngelScript 2.19.2: the exact version the game was built with (scripting).
# Its source already supports native calls on Windows x64.
fetch_dep(angelscript-2.19.2
    https://www.angelcode.com/angelscript/sdk/files/angelscript_2.19.2.zip
    534e62fd774c7c73899ea80700254d611db2f375d468094ef5ff9894d13bd256)

# Newton Dynamics 2.36 (physics). The game shipped with 2.00, whose source was
# never published; 2.36 is the closest version with source and keeps the API.
# Source files are zlib-licensed (Julio Jerez). Only the core library is kept.
fetch_dep(newton-2.36
    https://github.com/sigmaco/newton-v2.36/archive/3f442c286a26e6ad313a095ae2d16e90db89120c.zip
    ac36e7ee9bf9c433ba69a177903e894686b7f894c451ffd210f3ff2f39bd45c7
    SUBDIR coreLibrary_200)

# Newton detects 64-bit with GCC's __x86_64__ only. On MSVC x64 it picked the
# 32-bit memory block size, too small for 64-bit pointers: heap corruption and
# a crash inside NewtonCreate.
patch_dep(newton-2.36 source/core/dgMemory.h
    "#ifdef __x86_64__"
    "#if defined(__x86_64__) || defined(_M_X64)")

# RTX Remix SDK (API 0.6.5) from the remix-1.6.1 tag, matching the runtime below:
# the C API header, its C++ wrapper, and NVIDIA's minimal C example (a
# path-traced triangle), used to check that the runtime works.
set(REMIX_SDK_URL https://raw.githubusercontent.com/NVIDIAGameWorks/dxvk-remix/remix-1.6.1)
fetch_file(remix-sdk-1.6.1/include/remix/remix_c.h
    ${REMIX_SDK_URL}/public/include/remix/remix_c.h
    26a5607a07e8718298c35519e53bbc71d526186a947e5a2a5b613a0d6db04f3a)
fetch_file(remix-sdk-1.6.1/include/remix/remix.h
    ${REMIX_SDK_URL}/public/include/remix/remix.h
    29346184db11fe92b809a6c13ed31d8c53eea4fa28111a943e3a4ad8e1224136)
fetch_file(remix-sdk-1.6.1/example/remixapi_example_c.c
    ${REMIX_SDK_URL}/tests/rtx/apps/RemixAPI_C/remixapi_example_c.c
    869a06ea29832984b57a5b17bcdb834342bb7e30d085c51f78ef364756f23ef7)

# RTX Remix runtime 1.6.1 for x64 apps (path-traced renderer, Remix API 0.6.5).
# d3d9.dll is a thin proxy; the API implementation sits next to it.
fetch_dep(remix-1.6.1
    https://github.com/NVIDIAGameWorks/rtx-remix/releases/download/remix-1.6.1/rtx-remix-for-x64-games-release-remix-1.6.1.zip
    7dd1d44492a609050e44b0e3581cec31135d1ed88b4a91123b4faae91955716a)
