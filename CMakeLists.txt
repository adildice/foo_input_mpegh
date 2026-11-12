cmake_minimum_required(VERSION 3.21)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
project(foo_input_mpegh LANGUAGES CXX)

include(FetchContent)

# foobar2000 SDK. By default it is downloaded (git mirror of the official SDK, pinned).
# To use your own copy, pass -DFB2K_ROOT=<folder that contains foobar2000/ and pfc/>.
set(FB2K_ROOT "" CACHE PATH "foobar2000 SDK root (contains foobar2000/ and pfc/); empty = download")
if(NOT FB2K_ROOT)
  set(FB2K_ROOT "${CMAKE_BINARY_DIR}/fb2k")
  FetchContent_Declare(fb2k_sdk_src
    GIT_REPOSITORY https://github.com/reupen/foobar2000-sdk.git
    GIT_TAG        f56be547d34468da2ff47fcf49eacee41b6f42b1   # SDK 2026-10-01
    SOURCE_DIR     "${FB2K_ROOT}/foobar2000"
    SOURCE_SUBDIR  _none_)
  FetchContent_Declare(fb2k_pfc_src
    GIT_REPOSITORY https://github.com/reupen/pfc.git
    GIT_TAG        75053edf649b41b2202fcc7b9c5c3848a0e9a8cb   # SDK 2026-10-01
    SOURCE_DIR     "${FB2K_ROOT}/pfc"
    SOURCE_SUBDIR  _none_)
  FetchContent_MakeAvailable(fb2k_sdk_src fb2k_pfc_src)
endif()
if(NOT EXISTS "${FB2K_ROOT}/foobar2000/SDK/foobar2000.h" OR NOT EXISTS "${FB2K_ROOT}/pfc/pfc.h")
  message(FATAL_ERROR "foobar2000 SDK not found under ${FB2K_ROOT}")
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") # static CRT, like the SDK
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# --- Fraunhofer decoder and MP4 reader ------------------------------------------------
set(mpeghdec_SYMBOL_PREFIX OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ilo
  GIT_REPOSITORY https://github.com/Fraunhofer-IIS/ilo.git
  GIT_TAG        r2.0.2)
FetchContent_Declare(mmtisobmff
  GIT_REPOSITORY https://github.com/Fraunhofer-IIS/mmtisobmff.git
  GIT_TAG        r1.0.4)
FetchContent_Declare(mpeghdec
  GIT_REPOSITORY https://github.com/Fraunhofer-IIS/mpeghdec.git
  GIT_TAG        4448b69738da2fa5f2f2f2b0ce29eea32509e046)
FetchContent_MakeAvailable(ilo mmtisobmff mpeghdec)

# --- foobar2000 SDK -------------------------------------------------------------------
file(GLOB PFC_SRC "${FB2K_ROOT}/pfc/*.cpp")
file(GLOB SDK_SRC "${FB2K_ROOT}/foobar2000/SDK/*.cpp")
if(WIN32)
  list(FILTER PFC_SRC EXCLUDE REGEX "(synchro_nix|nix-objects)\\.cpp$")
endif()

add_library(fb2k_pfc STATIC ${PFC_SRC})
add_library(fb2k_sdk STATIC ${SDK_SRC})
foreach(t fb2k_pfc fb2k_sdk)
  set_target_properties(${t} PROPERTIES CXX_STANDARD 20 POSITION_INDEPENDENT_CODE ON)
  target_include_directories(${t} PUBLIC "${FB2K_ROOT}" "${FB2K_ROOT}/foobar2000")
  target_compile_definitions(${t} PUBLIC UNICODE _UNICODE NOMINMAX $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
  if(MSVC)
    target_compile_options(${t} PRIVATE /MP /W3 /wd4996)
  else()
    target_compile_options(${t} PRIVATE -w "SHELL:-include ctime" "SHELL:-include climits" "SHELL:-include memory") # pfc relies on these being pulled in
  endif()
endforeach()
target_link_libraries(fb2k_sdk PUBLIC fb2k_pfc)

# --- the component --------------------------------------------------------------------
if(WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(FB2K_SHARED_LIB "${FB2K_ROOT}/foobar2000/shared/shared-x64.lib")
  else()
    set(FB2K_SHARED_LIB "${FB2K_ROOT}/foobar2000/shared/shared-Win32.lib")
  endif()
  add_library(foo_input_mpegh SHARED
    src/foo_input_mpegh.cpp
    "${FB2K_ROOT}/foobar2000/foobar2000_component_client/component_client.cpp")
  set_target_properties(foo_input_mpegh PROPERTIES CXX_STANDARD 20 PREFIX "")
  target_link_libraries(foo_input_mpegh PRIVATE fb2k_sdk fb2k_pfc mpeghdec mmtisobmff ilo
    "${FB2K_SHARED_LIB}" shlwapi)
  if(MSVC)
    target_compile_options(foo_input_mpegh PRIVATE /W3 /wd4996 /EHsc)
  endif()
endif()

# --- host-free self-test (any platform; used by CI on Linux) ----------------------------
option(MPEGH_BUILD_SELFTEST "Build the host-free decoder self-test" OFF)
if(MPEGH_BUILD_SELFTEST)
  add_executable(selftest test/selftest.cpp test/host_stubs.cpp)
  set_target_properties(selftest PROPERTIES CXX_STANDARD 20)
  target_link_libraries(selftest PRIVATE fb2k_sdk fb2k_pfc mpeghdec mmtisobmff ilo)
  if(NOT MSVC)
    target_compile_options(selftest PRIVATE -w)
  endif()
endif()
