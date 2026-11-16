cmake_minimum_required (VERSION 2.8.11)
project(HPL2)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(BoilerPlate)

set(HPL2_VERSION_MAJOR 2)
set(HPL2_VERSION_MINOR 0)
if(APPLE AND IS_APPLE_64)
    set(BIN_LIBROOT "${PLATFORM_PREFIX}64")
else()
    set(BIN_LIBROOT ${PLATFORM_PREFIX})
endif()

set(DEP_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dependencies/lib/${BIN_LIBROOT}")
set(DEP_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dependencies/include")
find_path(PREBUILT_PLATFORM_ROOT .
    PATHS "${DEP_LIB_DIR}"
    NO_DEFAULT_PATH
)
set(DEP_LIB_DIR ${DEP_LIB_DIR} PARENT_SCOPE)
set(DEP_INCLUDE_DIR ${DEP_INCLUDE_DIR} PARENT_SCOPE)
mark_as_advanced(
    PREBUILT_PLATFORM_ROOT
)

include(UtilityFunctions)

IF(APPLE)
    SET(PRIVATE_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../dependencies CACHE PATH "")
    if(IS_APPLE_64)
        SET(PRIVATE_LIB_SUFFIX "lib/macosx64")
    else()
        SET(PRIVATE_LIB_SUFFIX "lib/macosx")
    endif()
ELSEIF(LINUX)
    SET(PRIVATE_LIB_SUFFIX "lib/linux")
    SET(PRIVATE_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../dependencies CACHE PATH "")
ENDIF()

OPTION(USE_SDL2 "Use SDL2 instead of SDL1.2" ON)

IF(WIN64)
    # 64-bit Windows: libraries from vcpkg (vcpkg.json in the repo root).
    # The old headers in ../dependencies/include are not used at all, so they
    # can't shadow the new ones; only Cg's headers are still needed, for a type
    # name in LowLevelGraphicsSDL.h (Cg itself is only used under WITH_CG).
    find_package(SDL2 CONFIG REQUIRED)
    find_package(Ogg CONFIG REQUIRED)
    find_package(Vorbis CONFIG REQUIRED)
    find_package(unofficial-theora CONFIG REQUIRED)
    find_package(GLEW REQUIRED)
    find_package(OpenAL CONFIG REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(DevIL REQUIRED)
    set(LEGACY_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/legacy-include)
    file(COPY ../dependencies/include/Cg DESTINATION ${LEGACY_INCLUDE_DIR})
ENDIF()

IF(WIN32)
    # OALWrapper's own CMakeLists has no Windows support, and it lives in the
    # unpacked (git-ignored) dependencies folder, so define its target here.
    file(GLOB oalwrapper_sources ../dependencies/OALWrapper/sources/*.cpp)
    add_library(OALWrapper STATIC ${oalwrapper_sources})
    target_include_directories(OALWrapper PUBLIC ../dependencies/OALWrapper/include)
    target_compile_definitions(OALWrapper PUBLIC $<$<BOOL:${USE_SDL2}>:USE_SDL2>)
    IF(WIN64)
        target_link_libraries(OALWrapper PUBLIC
            OpenAL::OpenAL Vorbis::vorbisfile Vorbis::vorbis Ogg::ogg SDL2::SDL2)
    ELSE()
        # Its headers pull in ogg/vorbis/OpenAL libs via #pragma comment(lib)
        target_include_directories(OALWrapper
            PUBLIC ../dependencies/include
            PRIVATE ../dependencies/include/SDL2
        )
        target_link_directories(OALWrapper PUBLIC ${DEP_LIB_DIR})
    ENDIF()
    set(OALWRAPPER_INCLUDE_DIR ../dependencies/OALWrapper/include)
ELSE()
    add_subdirectory(../dependencies/OALWrapper OALWrapper)
ENDIF()

SET(HPL2_DEFINES
    PUBLIC $<$<BOOL:${USE_SDL2}>:USE_SDL2>
)

add_definitions(
    -DUSE_OALWRAPPER
)

IF(WIN64)
    set(HPL2_INCLUDES
        PUBLIC include
        PRIVATE ${OALWRAPPER_INCLUDE_DIR}
        PUBLIC ${LEGACY_INCLUDE_DIR}
    )
ELSE()
set(HPL2_INCLUDES
    PUBLIC include
    #Important that this is before dependencies so the correct OpenAL headers are used
    PRIVATE ${OALWRAPPER_INCLUDE_DIR}
    PUBLIC ../dependencies/include
)
ENDIF()

IF(LINUX)
    add_library(binreloc STATIC ../dependencies/sources/binreloc/binreloc.c)
    set_target_properties(binreloc PROPERTIES COMPILE_DEFINITIONS "ENABLE_BINRELOC")

    list(APPEND HPL2_INCLUDES
        ../dependencies/sources/binreloc/
    )
ENDIF()


# common shared source
file(GLOB common_sources RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    sources/ai/*
    sources/engine/*
    sources/generate/*
    sources/graphics/*
    sources/gui/*
    sources/haptic/*
    sources/input/*
    sources/math/*
    sources/physics/*
    sources/resources/*
    sources/scene/*
    sources/sound/*
    sources/system/*

    include/ai/*
    include/engine/*
    include/generate/*
    include/graphics/*
    include/gui/*
    include/haptic/*
    include/input/*
    include/math/*
    include/physics/*
    include/resources/*
    include/scene/*
    include/sound/*
    include/system/*
    include/*.*
)
list(APPEND common_sources include/BuildID_HPL2_0.h)
list(REMOVE_DUPLICATES common_sources)

#setup various IMPL sources
file(GLOB impl_sources RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    include/impl/tinyXML/*
    include/impl/*.*

    # tinyXML
    sources/impl/tinyXml/*
    sources/impl/XmlDocumentTiny.cpp
    # scripting
    sources/impl/SqScript.cpp
    sources/impl/scriptarray.cpp
    sources/impl/scripthelper.cpp
    sources/impl/scriptstring.cpp
    sources/impl/scriptstring_utils.cpp
    # Image loading
    sources/impl/BitmapLoader*
    # theora video
    sources/impl/VideoStreamTheora.cpp
    # Newton
    sources/impl/*Newton.cpp
    # GL
    sources/impl/FrameBufferGL.cpp
    sources/impl/GLSL*
    sources/impl/OcclusionQueryOGL.cpp
    sources/impl/VertexBufferOGL_Array.cpp
    sources/impl/VertexBufferOGL_VBO.cpp
    sources/impl/VertexBufferOpenGL.cpp
    # SDL
    sources/impl/GamepadSDL.cpp
    sources/impl/GamepadSDL2.cpp
    sources/impl/KeyboardSDL.cpp
    sources/impl/MouseSDL.cpp
    sources/impl/MutexSDL.cpp
    sources/impl/ThreadSDL.cpp
    sources/impl/TimerSDL.cpp
    sources/impl/LowLevelGraphicsSDL.cpp
    sources/impl/LowLevelInputSDL.cpp
    sources/impl/LowLevelResourcesSDL.cpp
    sources/impl/LowLevelSystemSDL.cpp
    sources/impl/SDLEngineSetup.cpp
    sources/impl/SDLFontData.cpp
    sources/impl/SDLTexture.cpp
    # OpenAL
    sources/impl/LowLevelSoundOpenAL.cpp
    sources/impl/OpenAL*
    # mesh loader
    sources/impl/MeshLoaderCollada.cpp
    sources/impl/MeshLoaderColladaHelpers.cpp
    sources/impl/MeshLoaderColladaLoader.cpp
    sources/impl/MeshLoaderMSH.cpp
)

IF(APPLE)
    LIST(APPEND impl_sources
        sources/impl/PlatformMacOSX.mm
        sources/impl/PlatformUnix.cpp
        sources/impl/PlatformSDL.cpp
    )
    IF(NOT USE_SDL2)
        LIST(APPEND impl_sources
            SDLMain.m
            SDLMain.h
        )
    ENDIF()
ELSEIF(LINUX)
    LIST(APPEND impl_sources
        sources/impl/PlatformUnix.cpp
        sources/impl/PlatformSDL.cpp
    )
ELSEIF(WIN32)
    # Same platform files as the original Visual Studio project
    LIST(REMOVE_ITEM impl_sources
        sources/impl/MutexSDL.cpp
        sources/impl/ThreadSDL.cpp
    )
    LIST(APPEND impl_sources
        sources/impl/PlatformWin32.cpp
        sources/impl/MutexWin32.cpp
        sources/impl/ThreadWin32.cpp
        sources/impl/GamepadXInput.cpp
    )
    IF(WIN64)
        # RTX mode through RTX Remix (docs/decisions/0002); active at runtime with HPL_REMIX=1
        OPTION(HPL_REMIX "Build RTX mode through RTX Remix (64-bit Windows)" ON)
        IF(HPL_REMIX)
            LIST(APPEND impl_sources sources/impl/RemixContext.cpp sources/impl/RemixScene.cpp sources/impl/RemixTextureCache.cpp sources/impl/RendererRemix.cpp)
        ENDIF()
    ENDIF()
ENDIF()

# Setup buildID source

IF(APPLE)
    SET(BUILDID_SUFFIX MacOSX)
ELSEIF(LINUX)
    SET(BUILDID_SUFFIX Linux)
ELSEIF(WIN32)
    SET(BUILDID_SUFFIX Win32)
ELSE()
    message(FATAL_ERROR "Unknown platform")
ENDIF()
SET(BUILDID_SUFFIX ${BUILDID_SUFFIX} CACHE STRING "Build ID Suffix")
find_file(BUILDID_BIN buildcounter.pl
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}
    NO_DEFAULT_PATH
)
mark_as_advanced(BUILDID_SUFFIX BUILDID_BIN)

function(GenerateBuildID outvar idname includedir sourcedir)
    add_custom_command(
        OUTPUT
            ${sourcedir}/BuildID_${idname}_${BUILDID_SUFFIX}.cpp
            ${includedir}/BuildID_${idname}.h
        COMMAND
            ${BUILDID_BIN}
        ARGS
            ${idname} ${includedir} ${sourcedir}
    )
    set(${outvar} ${sourcedir}/BuildID_${idname}_${BUILDID_SUFFIX}.cpp
            ${includedir}/BuildID_${idname}.h PARENT_SCOPE)
endfunction()

# Windows can't run the Perl script directly; like the original VS project, use
# the committed BuildID files (nothing calls GetBuildID_HPL2_0 anyway).
IF(NOT WIN32)
GenerateBuildID(version_source
    HPL2_0
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/sources
)
ENDIF()

# setup source groups
source_group("AI" REGULAR_EXPRESSION "\\/ai\\/.+")
source_group("Engine" REGULAR_EXPRESSION "\\/engine\\/.+")
source_group("Generate" REGULAR_EXPRESSION "\\/generate\\/.+")
source_group("Graphics" REGULAR_EXPRESSION "\\/graphics\\/.+")
source_group("GUI" REGULAR_EXPRESSION "\\/gui\\/.+")
source_group("Input" REGULAR_EXPRESSION "\\/input\\/.+")
source_group("Math" REGULAR_EXPRESSION "\\/math\\/.+")
source_group("Physics" REGULAR_EXPRESSION "\\/physics\\/.+")
source_group("Resources" REGULAR_EXPRESSION "\\/resources\\/.+")
source_group("Scene" REGULAR_EXPRESSION "\\/scene\\/.+")
source_group("Sound" REGULAR_EXPRESSION "\\/sound\\/.+")
source_group("System" REGULAR_EXPRESSION "\\/system\\/.+")

source_group("Implementation Specific" REGULAR_EXPRESSION "\\/impl\\/.+")
source_group("TinyXML" REGULAR_EXPRESSION "\\/impl\\/tiny.+")
source_group("SDL Specific" REGULAR_EXPRESSION "\\/impl\\/.*SDL.+")
source_group("OpenGL Specific" REGULAR_EXPRESSION "\\/impl\\/.*GL.+")
source_group("HaptX Specific" REGULAR_EXPRESSION "\\/impl\\/.+HaptX.+")
source_group("Newton Specific" REGULAR_EXPRESSION "\\/impl\\/.+Newton.+")
source_group("Mesh Loaders" REGULAR_EXPRESSION "\\/impl\\/Mesh.+")
source_group("OpenAL Specific" REGULAR_EXPRESSION "\\/impl\\/.*OpenAL.+")
source_group("Win32" REGULAR_EXPRESSION "\\/impl\\/.+Win32.+")
source_group("Platform" REGULAR_EXPRESSION "\\/impl\\/Platform.+")


# setup libs

set(EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../external")

IF(WIN32)
    # AngelScript 2.19.2 (the game's version) from source: the bundled VS2010
    # angelscript.lib needs the old C runtime, and there is no 64-bit one.
    set(AS_DIR "${EXTERNAL_DIR}/angelscript-2.19.2/sdk/angelscript")
    if(NOT EXISTS "${AS_DIR}/source")
        message(FATAL_ERROR "AngelScript source missing: run 'cmake -P fetch-deps.cmake' in the repo root")
    endif()
    file(GLOB as_sources "${AS_DIR}/source/*.cpp")
    IF(WIN64)
        # Native x64 calls need the MASM file. ml64 can't create the nested
        # output folder CMake picks for sources outside the project, so copy it.
        enable_language(ASM_MASM)
        configure_file("${AS_DIR}/source/as_callfunc_x64_msvc.asm"
                       "${CMAKE_CURRENT_BINARY_DIR}/as_callfunc_x64_msvc.asm" COPYONLY)
        list(APPEND as_sources "${CMAKE_CURRENT_BINARY_DIR}/as_callfunc_x64_msvc.asm")
    ENDIF()
    add_library(angelscript STATIC ${as_sources})
    target_include_directories(angelscript PUBLIC "${AS_DIR}/include")
    set(ANGELSCRIPT_LIBRARY angelscript)
ELSE()
    FindPrebuiltLibrary(ANGELSCRIPT_LIBRARY angelscript)
ENDIF()

IF(WIN64)
    # Newton 2.36 from source (2.00's source was never published)
    set(NT_DIR "${EXTERNAL_DIR}/newton-2.36/source")
    if(NOT EXISTS "${NT_DIR}/newton")
        message(FATAL_ERROR "Newton source missing: run 'cmake -P fetch-deps.cmake' in the repo root")
    endif()
    file(GLOB nt_sources "${NT_DIR}/core/*.cpp" "${NT_DIR}/physics/*.cpp" "${NT_DIR}/newton/*.cpp")
    # Dead file: includes a header that doesn't exist
    list(FILTER nt_sources EXCLUDE REGEX "dgVehicleConstraint")
    add_library(newton STATIC ${nt_sources})
    target_include_directories(newton PRIVATE "${NT_DIR}/core" "${NT_DIR}/physics" PUBLIC "${NT_DIR}/newton")
    # HPL_NEWTON_2_36: map caches hold serialized Newton collision data, whose
    # format differs from the 2.00 caches shipped with the game (see WorldLoaderHplMap)
    target_compile_definitions(newton PUBLIC _NEWTON_STATIC_LIB HPL_NEWTON_2_36 PRIVATE _WIN_64_VER)
    set(NEWTON_LIBRARY newton)

    set(GLEW_LIBRARY GLEW::GLEW)
    set(THEORA_LIBRARY unofficial::theora::theora)
    # FindDevIL can pick the debug lib for every config; choose per config
    set(VCPKG_TRIPLET_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    set(DEVIL_LIBRARY
        optimized "${VCPKG_TRIPLET_DIR}/lib/DevIL.lib"
        debug "${VCPKG_TRIPLET_DIR}/debug/lib/DevIL.lib")
    set(ZLIB_LIBRARY ZLIB::ZLIB)
    set(SDL2_LIBRARY SDL2::SDL2)
ELSEIF(WIN32)
    FindPrebuiltLibrary(NEWTON_LIBRARY Newton)
    FindPrebuiltLibrary(GLEW_LIBRARY glew32)
    FindPrebuiltLibrary(THEORA_LIBRARY libtheora)
    FindPrebuiltLibrary(DEVIL_LIBRARY DevIL)
    FindPrebuiltLibrary(ZLIB_LIBRARY zlibwapi)
    FindPrebuiltLibrary(SDL2_LIBRARY SDL2)
ELSE()
    FindPrebuiltLibrary(NEWTON_LIBRARY Newton)
    FindPrebuiltLibrary(GLEW_LIBRARY GLEW)
    FindPrebuiltLibrary(THEORA_LIBRARY theora)
    FindPrebuiltLibrary(DEVIL_LIBRARY IL)
    SET(ZLIB_LIBRARY z)
ENDIF()

find_package(OpenGL)
mark_as_advanced(NEWTON_LIBRARY ANGELSCRIPT_LIBRARY GLEW_LIBRARY THEORA_LIBRARY DEVIL_LIBRARY)

# setup HPL2 compile target

add_library(HPL2 STATIC
    ${common_sources}
    ${version_sources}
    ${impl_sources}
)

target_include_directories(HPL2
    ${HPL2_INCLUDES}
)

target_compile_definitions(HPL2
    ${HPL2_DEFINES}
)

if(WIN64 AND HPL_REMIX)
    set(REMIX_SDK_DIR "${EXTERNAL_DIR}/remix-sdk-1.6.1")
    set(REMIX_RUNTIME_DIR "${EXTERNAL_DIR}/remix-1.6.1")
    if(NOT EXISTS "${REMIX_SDK_DIR}/include/remix/remix_c.h" OR NOT EXISTS "${REMIX_RUNTIME_DIR}/d3d9.dll")
        message(FATAL_ERROR "RTX Remix missing: run 'cmake -P fetch-deps.cmake' in the repo root (or configure with -DHPL_REMIX=OFF)")
    endif()
    target_include_directories(HPL2 PRIVATE "${REMIX_SDK_DIR}/include")
    target_compile_definitions(HPL2 PRIVATE HPL_REMIX)
endif()

# Remix loads its dependencies from the exe's folder, so the runtime has to sit
# next to the exe. Hard links: no extra disk space (falls back to copying).
function(HplAddRemixRuntime target)
    if(WIN64 AND HPL_REMIX)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -DSRC=${REMIX_RUNTIME_DIR} -DDST=$<TARGET_FILE_DIR:${target}>
                    -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cmake/LinkRemixRuntime.cmake
            VERBATIM)
    endif()
endfunction()
set(REMIX_RUNTIME_DIR ${REMIX_RUNTIME_DIR} PARENT_SCOPE)

if(LINUX)
    set(PLATFORM_LIBS
        binreloc
        pthread
        dl
    )
elseif(APPLE)
    FIND_LIBRARY(SC_LIBRARY SystemConfiguration)
    FIND_LIBRARY(COCOA_LIBRARY Cocoa)
    mark_as_advanced(COCOA_LIBRARY THEORA_LIBRARY SC_LIBRARY)

    set(PLATFORM_LIBS
        iconv ${SC_LIBRARY}
        ${COCOA_LIBRARY}
    )
elseif(WIN32)
    set(PLATFORM_LIBS
        ${SDL2_LIBRARY}
        XInput user32 gdi32 shell32 wininet dbghelp
    )
    if(WIN64)
        # The sources name the old 32-bit libs with #pragma comment(lib);
        # the 64-bit build links targets instead, so ignore those names.
        foreach(lib Cg CgGL TaskKeyHook SDL newton angelscript openal32
                    libogg libtheora libvorbis libvorbisfile)
            target_link_options(HPL2 PUBLIC /NODEFAULTLIB:${lib}.lib)
        endforeach()
    else()
        # As in the original VS project; Cg, TaskKeyHook, ogg etc. come in through
        # #pragma comment(lib) in the sources, found via this link directory.
        target_link_directories(HPL2 PUBLIC ${DEP_LIB_DIR})
    endif()
endif()

target_link_libraries(HPL2
    OALWrapper # also pulls in ogg,vorbis,SDL
    ${NEWTON_LIBRARY}
    ${ANGELSCRIPT_LIBRARY}
    ${OPENGL_gl_LIBRARY} ${OPENGL_glu_LIBRARY}
    ${GLEW_LIBRARY}
    ${THEORA_LIBRARY}
    ${DEVIL_LIBRARY}
    ${ZLIB_LIBRARY}
    ${PLATFORM_LIBS}
)


if(APPLE)   
    # helper function to copy the needed frameworks in at build time
    SET(HPL2_FRAMEWORKS ${HPL2_FRAMEWORKS} CACHE STRING "HPL2 copy frameworks")
    SET(HPL2_LIBRARIES libSDL2.dylib libIL.1.dylib) 
    SET(HPL2_LIBRARIES ${HPL2_LIBRARIES}  CACHE STRING "HPL2 copy libs")
    mark_as_advanced(HPL2_FRAMEWORKS HPL2_LIBRARIES)

    FUNCTION(PostBuildMacBundle APP_TARGET framework_list lib_list)
        INCLUDE(BundleUtilities)
        GET_TARGET_PROPERTY(_BIN_NAME ${APP_TARGET} LOCATION)
        GET_DOTAPP_DIR(${_BIN_NAME} _BUNDLE_DIR)

        set(_SCRIPT_FILE ${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}_prep.cmake)
        file(WRITE ${_SCRIPT_FILE}
            "# Generated Script file\n"
            "include(BundleUtilities)\n"
            "get_bundle_and_executable(\"\${BUNDLE_APP}\" bundle executable valid)\n"
            "if(valid)\n"
            "  set(framework_path \"\${bundle}/Contents/Frameworks\")\n"
            "  foreach(framework ${framework_list})\n"
            "    file(MAKE_DIRECTORY \"\${framework_path}/\${framework}.framework/Versions/A/\")\n"
            "    copy_resolved_framework_into_bundle(\${DEP_LIB_DIR}/\${framework}.framework/Versions/A/\${framework} \${framework_path}/\${framework}.framework/Versions/A/\${framework})\n"
            "  endforeach()\n"
            "  foreach(lib ${lib_list})\n"
            "    copy_resolved_item_into_bundle(\${DEP_LIB_DIR}/\${lib} \${framework_path}/\${lib})\n"
            "  endforeach()\n"
            "else()\n"
            "  message(ERROR \"App Not found? \${BUNDLE_APP}\")\n"
            "endif()\n"
            "#fixup_bundle(\"\${BUNDLE_APP}\" \"\" \"\${DEP_LIB_DIR}\")\n"
        )

        ADD_CUSTOM_COMMAND(TARGET ${APP_TARGET}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -DBUNDLE_APP="${_BUNDLE_DIR}" -DDEP_LIB_DIR="${DEP_LIB_DIR}" -P "${_SCRIPT_FILE}"
        )
    ENDFUNCTION()
ENDIF()

FUNCTION(AddTestTarget target_name)
    add_executable(${target_name} EXCLUDE_FROM_ALL
        ${ARGN}
    )
    target_link_libraries(${target_name} HPL2)
    set_target_properties(${target_name} PROPERTIES
        XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "${DEP_LIB_DIR}"
        INSTALL_RPATH "${DEP_LIB_DIR}"
    )
ENDFUNCTION()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../tests/CMakeLists.txt)
    add_subdirectory(../tests tests)
endif()
# The editors need FLTK and more work on Windows; the game comes first
if(NOT WIN32)
    add_subdirectory(../tools tools)
endif()


