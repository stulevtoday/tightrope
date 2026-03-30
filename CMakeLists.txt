cmake_minimum_required(VERSION 3.21)
project(tightrope-core LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# vcpkg integrates via CMAKE_TOOLCHAIN_FILE (set by cmake-js or CLI)

set(TIGHTROPE_NODE_INCLUDE_DIRS "")
set(TIGHTROPE_NODE_LIBS "")
set(NODE_PREFIX "")
set(TIGHTROPE_USING_CMAKE_JS OFF)
if(DEFINED CMAKE_JS_VERSION)
  set(TIGHTROPE_USING_CMAKE_JS ON)
endif()
if(CMAKE_JS_INC)
  list(APPEND TIGHTROPE_NODE_INCLUDE_DIRS ${CMAKE_JS_INC})
endif()
if(CMAKE_JS_LIB)
  list(APPEND TIGHTROPE_NODE_LIBS ${CMAKE_JS_LIB})
endif()

execute_process(
  COMMAND node -p "require('node-addon-api').include"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/app
  OUTPUT_VARIABLE NODE_ADDON_API_INCLUDE
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NODE_ADDON_API_INCLUDE)
  string(REPLACE "\"" "" NODE_ADDON_API_INCLUDE "${NODE_ADDON_API_INCLUDE}")
  list(APPEND TIGHTROPE_NODE_INCLUDE_DIRS ${NODE_ADDON_API_INCLUDE})
endif()

execute_process(
  COMMAND node -p "process.execPath"
  OUTPUT_VARIABLE NODE_EXEC_PATH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NODE_EXEC_PATH)
  get_filename_component(NODE_BIN_DIR "${NODE_EXEC_PATH}" DIRECTORY)
  get_filename_component(NODE_PREFIX "${NODE_BIN_DIR}/.." ABSOLUTE)
  list(APPEND NODE_HEADER_HINTS "${NODE_PREFIX}/include/node")
endif()
list(APPEND NODE_HEADER_HINTS "/opt/homebrew/include/node" "/usr/local/include/node" "/usr/include/node")

set(TIGHTROPE_HAS_NODE_API_INCLUDE OFF)
foreach(node_include_dir IN LISTS TIGHTROPE_NODE_INCLUDE_DIRS)
  if(EXISTS "${node_include_dir}/node_api.h")
    set(TIGHTROPE_HAS_NODE_API_INCLUDE ON)
    break()
  endif()
endforeach()

if(NOT TIGHTROPE_HAS_NODE_API_INCLUDE)
  find_path(NODE_API_INCLUDE_DIR node_api.h HINTS ${NODE_HEADER_HINTS})
  if(NODE_API_INCLUDE_DIR)
    list(APPEND TIGHTROPE_NODE_INCLUDE_DIRS ${NODE_API_INCLUDE_DIR})
  endif()
endif()
list(REMOVE_DUPLICATES TIGHTROPE_NODE_INCLUDE_DIRS)

if(NOT TIGHTROPE_NODE_INCLUDE_DIRS)
  message(FATAL_ERROR "Node addon headers were not found. Install Node and app/node_modules dependencies first.")
endif()

if(NOT TIGHTROPE_NODE_LIBS AND NOT TIGHTROPE_USING_CMAKE_JS)
  set(NODE_LIBRARY_HINTS "/opt/homebrew/lib" "/usr/local/lib")
  if(NODE_PREFIX)
    list(APPEND NODE_LIBRARY_HINTS "${NODE_PREFIX}/lib")
    file(GLOB NODE_LIB_GLOB "${NODE_PREFIX}/lib/libnode*.dylib" "${NODE_PREFIX}/lib/libnode*.so" "${NODE_PREFIX}/lib/libnode*.a")
    list(LENGTH NODE_LIB_GLOB NODE_LIB_GLOB_LEN)
    if(NODE_LIB_GLOB_LEN GREATER 0)
      list(GET NODE_LIB_GLOB 0 NODE_LIBRARY_FROM_GLOB)
      list(APPEND TIGHTROPE_NODE_LIBS ${NODE_LIBRARY_FROM_GLOB})
    endif()
  endif()
  find_library(NODE_LIBRARY NAMES node node.141 libnode.141 libnode HINTS ${NODE_LIBRARY_HINTS})
  if(NODE_LIBRARY AND NOT TIGHTROPE_NODE_LIBS)
    list(APPEND TIGHTROPE_NODE_LIBS ${NODE_LIBRARY})
  endif()
endif()

if(NOT TIGHTROPE_NODE_LIBS AND NOT TIGHTROPE_USING_CMAKE_JS)
  message(FATAL_ERROR "Node library was not found. Set CMAKE_JS_LIB or install Node development libraries.")
endif()

# ── vcpkg-managed packages ──
find_package(NuRaft CONFIG REQUIRED)
find_package(unofficial-uwebsockets CONFIG REQUIRED)
find_package(MbedTLS CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS log log_setup asio system uuid)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(SQLiteCpp CONFIG REQUIRED)
find_package(glaze CONFIG REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(quill CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(CURL REQUIRED)

if(TARGET NuRaft::nuraft)
  set(TIGHTROPE_NURAFT_TARGET NuRaft::nuraft)
elseif(TARGET NuRaft::static_lib)
  set(TIGHTROPE_NURAFT_TARGET NuRaft::static_lib)
else()
  message(FATAL_ERROR "NuRaft target not found (expected NuRaft::nuraft or NuRaft::static_lib)")
endif()

# ── Native module target ──
file(GLOB_RECURSE TIGHTROPE_SOURCES
  native/bridge/*.cpp
  native/core/*.cpp
  native/config/*.cpp
  native/db/*.cpp
  native/balancer/*.cpp
  native/proxy/*.cpp
  native/auth/*.cpp
  native/usage/*.cpp
  native/server/*.cpp
  native/sync/*.cpp
)

file(GLOB_RECURSE TIGHTROPE_NATIVE_DIR_CANDIDATES LIST_DIRECTORIES true
  ${CMAKE_SOURCE_DIR}/native/*
)
set(TIGHTROPE_NATIVE_INCLUDE_DIRS_INTERNAL "")
foreach(path IN LISTS TIGHTROPE_NATIVE_DIR_CANDIDATES)
  if(IS_DIRECTORY "${path}" AND path MATCHES "/include(/.*)?$")
    list(APPEND TIGHTROPE_NATIVE_INCLUDE_DIRS_INTERNAL "${path}")
  endif()
endforeach()
list(REMOVE_DUPLICATES TIGHTROPE_NATIVE_INCLUDE_DIRS_INTERNAL)

add_library(tightrope-core SHARED ${TIGHTROPE_SOURCES})

target_include_directories(tightrope-core PRIVATE
  ${CMAKE_SOURCE_DIR}/native
  ${TIGHTROPE_NATIVE_INCLUDE_DIRS_INTERNAL}
  ${CMAKE_SOURCE_DIR}/vendor
  ${TIGHTROPE_NODE_INCLUDE_DIRS}
)

target_link_libraries(tightrope-core PRIVATE
  ${TIGHTROPE_NURAFT_TARGET}
  unofficial::uwebsockets::uwebsockets
  MbedTLS::mbedtls MbedTLS::mbedcrypto MbedTLS::mbedx509
  Boost::log Boost::log_setup Boost::asio Boost::system Boost::uuid
  unofficial::sqlite3::sqlite3
  SQLiteCpp
  glaze::glaze
  lz4::lz4
  unofficial-sodium::sodium
  quill::quill
  tomlplusplus::tomlplusplus
  $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
  CURL::libcurl
  ${TIGHTROPE_NODE_LIBS}
)

set_target_properties(tightrope-core PROPERTIES
  PREFIX "" SUFFIX ".node"
)

# ── Tests ──
option(BUILD_TESTING "Build tests" OFF)
if(BUILD_TESTING)
  find_package(Catch2 CONFIG REQUIRED)
  file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/native/tests/*.cpp
  )
  add_executable(tightrope-tests ${TEST_SOURCES})
  target_include_directories(tightrope-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/native
    ${TIGHTROPE_NATIVE_INCLUDE_DIRS_INTERNAL}
    ${CMAKE_SOURCE_DIR}/vendor
  )
  target_link_libraries(tightrope-tests PRIVATE
    Catch2::Catch2WithMain
    tightrope-core
  )

  add_executable(tightrope-contract-capture
    ${CMAKE_SOURCE_DIR}/native/core/text/src/json_escape.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/contracts/src/contract_io.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/contracts/src/fixture_loader.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/contracts/src/source_contract_catalog.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/contracts/src/source_contract_model_index.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/contracts/src/source_contract_route_loader.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/contracts/src/source_contract_template.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/contracts/src/http_fixture_capture.cpp
    ${CMAKE_SOURCE_DIR}/native/tools/http_fixture_capture_main.cpp
  )
  target_include_directories(tightrope-contract-capture PRIVATE
    ${CMAKE_SOURCE_DIR}/native
    ${TIGHTROPE_NATIVE_INCLUDE_DIRS_INTERNAL}
    ${CMAKE_SOURCE_DIR}/vendor
  )

  add_executable(tightrope-streaming-contract-capture
    ${CMAKE_SOURCE_DIR}/native/core/text/src/json_escape.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/contracts/src/contract_io.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/contracts/src/source_contract_catalog.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/contracts/src/source_contract_model_index.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/contracts/src/source_contract_route_loader.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/contracts/src/source_contract_template.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/contracts/src/streaming_fixture_capture.cpp
    ${CMAKE_SOURCE_DIR}/native/tools/streaming_fixture_capture_main.cpp
  )
  target_include_directories(tightrope-streaming-contract-capture PRIVATE
    ${CMAKE_SOURCE_DIR}/native
    ${TIGHTROPE_NATIVE_INCLUDE_DIRS_INTERNAL}
    ${CMAKE_SOURCE_DIR}/vendor
  )
endif()
