# dhewm3-radiant: Dear ImGui (docking) based level editor
# included from neo/CMakeLists.txt when RADIANT_IMGUI is ON
#
# Builds tools/radiant/Radiant.cpp with ID_RADIANT_IMGUI as its own executable
# (without that define, the same file is the MFC Radiant of the TOOLS build).
#
# Needs Dear ImGui v1.92.9b-docking with the OpenGL2 + SDL2/SDL3 backends.
# If RADIANT_IMGUI_PREFIX doesn't point to an install prefix that has it,
# it's installed with vcpkg (manifest in ./vcpkg, pinned by the overlay port
# in ./vcpkg/ports/imgui) into the build dir, so the global vcpkg installation
# (C:/vcpkg/installed) isn't touched.

set(RADIANT_IMGUI_VERSION_NUM 19290) # 1.92.9 (v1.92.9b-docking)

if(SDL3)
  set(radiant_sdl "sdl3")
else()
  set(radiant_sdl "sdl2")
endif()

# --- vcpkg settings

if(DEFINED ENV{VCPKG_ROOT})
  set(radiant_default_vcpkg_root "$ENV{VCPKG_ROOT}")
else()
  set(radiant_default_vcpkg_root "C:/vcpkg")
endif()
set(RADIANT_IMGUI_VCPKG_ROOT "${radiant_default_vcpkg_root}" CACHE PATH "vcpkg used to install Dear ImGui for dhewm3-radiant")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(radiant_arch "x64")
else()
  set(radiant_arch "x86")
endif()
if(MSVC)
  # static libs with the dynamic CRT (/MD, CMake's default) => no DLLs to ship
  set(radiant_default_triplet "${radiant_arch}-windows-static-md")
elseif(MINGW)
  set(radiant_default_triplet "${radiant_arch}-mingw-static")
elseif(APPLE)
  set(radiant_default_triplet "${radiant_arch}-osx")
else()
  set(radiant_default_triplet "${radiant_arch}-linux")
endif()
set(RADIANT_IMGUI_VCPKG_TRIPLET "${radiant_default_triplet}" CACHE STRING "vcpkg triplet for dhewm3-radiant's dependencies")

set(RADIANT_IMGUI_PREFIX "" CACHE PATH "Install prefix with Dear ImGui v1.92.9b-docking (+ OpenGL2/SDL backends). Empty: install it with vcpkg")

# --- checks if <prefix>/include has the right ImGui with the needed backends

function(radiant_check_imgui prefix out_var)
  set(${out_var} FALSE PARENT_SCOPE)
  set(inc "${prefix}/include")
  if(NOT EXISTS "${inc}/imgui.h" OR NOT EXISTS "${inc}/imgui_impl_opengl2.h" OR NOT EXISTS "${inc}/imgui_impl_${radiant_sdl}.h")
    return()
  endif()
  file(STRINGS "${inc}/imgui.h" version_line REGEX "^#define IMGUI_VERSION_NUM[ \t]+[0-9]+")
  file(STRINGS "${inc}/imgui.h" dock_line REGEX "^#define IMGUI_HAS_DOCK")
  string(REGEX MATCH "[0-9]+$" version_num "${version_line}")
  if(dock_line AND version_num AND version_num GREATER_EQUAL ${RADIANT_IMGUI_VERSION_NUM})
    set(${out_var} TRUE PARENT_SCOPE)
  endif()
endfunction()

if(RADIANT_IMGUI_PREFIX)
  radiant_check_imgui("${RADIANT_IMGUI_PREFIX}" imgui_ok)
  if(NOT imgui_ok)
    message(FATAL_ERROR "RADIANT_IMGUI_PREFIX=${RADIANT_IMGUI_PREFIX} doesn't have Dear ImGui >= 1.92.9 docking with the OpenGL2 and ${radiant_sdl} backends. Clear it to install it with vcpkg.")
  endif()
  set(radiant_prefix "${RADIANT_IMGUI_PREFIX}")
else()
  set(radiant_manifest_dir "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg")
  set(radiant_install_root "${CMAKE_BINARY_DIR}/vcpkg_installed")
  set(radiant_prefix "${radiant_install_root}/${RADIANT_IMGUI_VCPKG_TRIPLET}")

  # re-run vcpkg when the manifest, the overlay port, the triplet or the backend change
  file(GLOB_RECURSE radiant_vcpkg_files "${radiant_manifest_dir}/*")
  list(SORT radiant_vcpkg_files)
  set(radiant_vcpkg_hash_input "${RADIANT_IMGUI_VCPKG_TRIPLET};${radiant_sdl}")
  foreach(f ${radiant_vcpkg_files})
    file(SHA256 "${f}" h)
    string(APPEND radiant_vcpkg_hash_input ";${h}")
  endforeach()
  string(SHA256 radiant_vcpkg_hash "${radiant_vcpkg_hash_input}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${radiant_vcpkg_files})

  set(radiant_stamp "${radiant_prefix}/.dhewm3-radiant-stamp")
  set(radiant_old_hash "")
  if(EXISTS "${radiant_stamp}")
    file(READ "${radiant_stamp}" radiant_old_hash)
  endif()
  radiant_check_imgui("${radiant_prefix}" imgui_ok)

  if(NOT imgui_ok OR NOT radiant_old_hash STREQUAL radiant_vcpkg_hash)
    if(CMAKE_HOST_WIN32)
      set(vcpkg_exe "${RADIANT_IMGUI_VCPKG_ROOT}/vcpkg.exe")
    else()
      set(vcpkg_exe "${RADIANT_IMGUI_VCPKG_ROOT}/vcpkg")
    endif()
    if(NOT EXISTS "${vcpkg_exe}")
      message(FATAL_ERROR "dhewm3-radiant needs Dear ImGui v1.92.9b-docking, but vcpkg wasn't found at ${RADIANT_IMGUI_VCPKG_ROOT}. Set RADIANT_IMGUI_VCPKG_ROOT (or VCPKG_ROOT), or RADIANT_IMGUI_PREFIX.")
    endif()

    set(old_path "$ENV{PATH}")
    if(MINGW)
      # vcpkg's MinGW triplets build with the gcc in PATH: use the one of this build
      get_filename_component(mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
      file(TO_NATIVE_PATH "${mingw_bin}" mingw_bin)
      set(ENV{PATH} "${mingw_bin};${old_path}")
    endif()

    message(STATUS "dhewm3-radiant: installing Dear ImGui v1.92.9b-docking (${radiant_sdl}, ${RADIANT_IMGUI_VCPKG_TRIPLET}) with vcpkg, this can take a while...")
    execute_process(
      COMMAND "${vcpkg_exe}" install
        --x-wait-for-lock
        --triplet=${RADIANT_IMGUI_VCPKG_TRIPLET}
        --host-triplet=${RADIANT_IMGUI_VCPKG_TRIPLET}
        --x-manifest-root=${radiant_manifest_dir}
        --x-install-root=${radiant_install_root}
        --x-feature=${radiant_sdl}
      WORKING_DIRECTORY "${RADIANT_IMGUI_VCPKG_ROOT}"
      RESULT_VARIABLE vcpkg_result
    )
    set(ENV{PATH} "${old_path}")
    if(NOT vcpkg_result EQUAL 0)
      message(FATAL_ERROR "dhewm3-radiant: vcpkg install failed (${vcpkg_result}), see the output above")
    endif()

    radiant_check_imgui("${radiant_prefix}" imgui_ok)
    if(NOT imgui_ok)
      message(FATAL_ERROR "dhewm3-radiant: vcpkg finished, but ${radiant_prefix} doesn't have the expected Dear ImGui")
    endif()
    file(WRITE "${radiant_stamp}" "${radiant_vcpkg_hash}")
  endif()
endif()

# --- find the packages only in that prefix, not the ones of the engine/system

if(SDL3)
  set(SDL3_DIR "${radiant_prefix}/share/sdl3")
else()
  set(SDL2_DIR "${radiant_prefix}/share/sdl2")
  find_package(SDL2 CONFIG REQUIRED PATHS "${radiant_prefix}" NO_DEFAULT_PATH)
endif()
find_package(imgui CONFIG REQUIRED PATHS "${radiant_prefix}" NO_DEFAULT_PATH)
message(STATUS "dhewm3-radiant: using Dear ImGui from ${radiant_prefix}")

find_package(OpenGL REQUIRED)

# --- the executable

# same source as the MFC Radiant: ID_RADIANT_IMGUI selects the ImGui version (with its own main())
set(src_radiant_imgui "${CMAKE_SOURCE_DIR}/tools/radiant/Radiant.cpp")
add_executable(${DHEWM3BINARY}-radiant WIN32 ${src_radiant_imgui})
source_group(TREE ${CMAKE_SOURCE_DIR} PREFIX neo FILES ${src_radiant_imgui})
target_compile_definitions(${DHEWM3BINARY}-radiant PRIVATE ID_RADIANT_IMGUI)

# neo/ for sys/proggyvector_font*.h (the engine's ImGui font)
target_include_directories(${DHEWM3BINARY}-radiant PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(${DHEWM3BINARY}-radiant PRIVATE imgui::imgui OpenGL::GL)

if(SDL3)
  target_compile_definitions(${DHEWM3BINARY}-radiant PRIVATE RADIANT_IMGUI_SDL3)
  # SDL3's main is header-only (SDL_main.h)
  target_link_libraries(${DHEWM3BINARY}-radiant PRIVATE $<IF:$<TARGET_EXISTS:SDL3::SDL3>,SDL3::SDL3,SDL3::SDL3-static>)
else()
  if(TARGET SDL2::SDL2main)
    target_link_libraries(${DHEWM3BINARY}-radiant PRIVATE SDL2::SDL2main)
  endif()
  target_link_libraries(${DHEWM3BINARY}-radiant PRIVATE $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>)
endif()

if(MINGW)
  # don't depend on the DLLs of the MinGW installation (libgcc, libstdc++, winpthread)
  target_link_options(${DHEWM3BINARY}-radiant PRIVATE -static)
endif()
