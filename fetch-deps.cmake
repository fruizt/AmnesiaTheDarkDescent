# Downloads the third-party libraries we build from source into external/.
#
#   cmake -P fetch-deps.cmake
#
# Each entry is pinned to an exact version and SHA-256, so everyone gets the
# same bytes. Already-downloaded libraries are skipped; delete the folder to
# fetch again.

cmake_minimum_required(VERSION 3.20)

set(EXTERNAL_DIR "${CMAKE_CURRENT_LIST_DIR}/external")

# fetch_dep(<folder> <url> <sha256>)
function(fetch_dep folder url sha256)
    set(dest "${EXTERNAL_DIR}/${folder}")
    if(EXISTS "${dest}")
        message(STATUS "${folder}: already present")
        return()
    endif()

    get_filename_component(archive_name "${url}" NAME)
    set(archive "${EXTERNAL_DIR}/${archive_name}")

    message(STATUS "${folder}: downloading ${url}")
    file(DOWNLOAD "${url}" "${archive}"
        EXPECTED_HASH SHA256=${sha256}
        STATUS status)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        file(REMOVE "${archive}")
        message(FATAL_ERROR "${folder}: download failed: ${status}")
    endif()

    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${dest}")
    file(REMOVE "${archive}")
    message(STATUS "${folder}: extracted")
endfunction()

# AngelScript 2.19.2: the exact version the game was built with (scripting).
# Its source already supports native calls on Windows x64.
fetch_dep(angelscript-2.19.2
    https://www.angelcode.com/angelscript/sdk/files/angelscript_2.19.2.zip
    534e62fd774c7c73899ea80700254d611db2f375d468094ef5ff9894d13bd256)
