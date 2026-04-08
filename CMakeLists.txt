cmake_minimum_required(VERSION 3.21)
if(APPLE OR WIN32)
  set(VCPKG_INSTALL_OPTIONS "--allow-unsupported" CACHE STRING "Additional install options to pass to vcpkg" FORCE)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/ports")
  set(VCPKG_OVERLAY_PORTS "${CMAKE_SOURCE_DIR}/ports" CACHE STRING "Overlay ports to use for vcpkg install in manifest mode" FORCE)
endif()
project(tightrope-core LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
  # 0x0A00 = Windows 10; needed by firewall_repo.cpp and other code that
  # pulls in Win32 headers requiring an explicit target version.
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
endif()

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

find_package(NuRaft CONFIG REQUIRED)
find_package(unofficial-uwebsockets CONFIG REQUIRED)
find_package(MbedTLS CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS log log_setup asio system uuid)
find_package(sqlcipher CONFIG REQUIRED)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND NOT TARGET PkgConfig::sqlcipher)
  pkg_check_modules(sqlcipher REQUIRED IMPORTED_TARGET GLOBAL sqlcipher)
endif()
find_package(SQLiteCpp CONFIG REQUIRED)
find_package(glaze CONFIG REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(quill CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

if(NOT WIN32)
  set(OPENDHT_DOWNLOAD_DEPS OFF CACHE BOOL "" FORCE)
  set(OPENDHT_USE_PKGCONFIG ON CACHE BOOL "" FORCE)
  set(OPENDHT_TOOLS OFF CACHE BOOL "" FORCE)
  set(OPENDHT_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(OPENDHT_PROXY_SERVER OFF CACHE BOOL "" FORCE)
  set(OPENDHT_PROXY_CLIENT OFF CACHE BOOL "" FORCE)
  set(OPENDHT_HTTP OFF CACHE BOOL "" FORCE)
  set(OPENDHT_PEER_DISCOVERY OFF CACHE BOOL "" FORCE)
  set(OPENDHT_INDEX OFF CACHE BOOL "" FORCE)
  set(OPENDHT_PYTHON OFF CACHE BOOL "" FORCE)
  set(OPENDHT_C OFF CACHE BOOL "" FORCE)
  set(OPENDHT_CPACK OFF CACHE BOOL "" FORCE)
  set(OPENDHT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/opendht EXCLUDE_FROM_ALL)
endif()

set(TIGHTROPE_HAS_ZSTD_DECOMPRESSION OFF)
set(TIGHTROPE_ZSTD_INCLUDE_DIRS "")
set(TIGHTROPE_ZSTD_LINK_LIB "")
find_package(zstd CONFIG QUIET)
if(TARGET zstd::libzstd_shared)
  set(TIGHTROPE_HAS_ZSTD_DECOMPRESSION ON)
  set(TIGHTROPE_ZSTD_LINK_LIB zstd::libzstd_shared)
elseif(TARGET zstd::libzstd_static)
  set(TIGHTROPE_HAS_ZSTD_DECOMPRESSION ON)
  set(TIGHTROPE_ZSTD_LINK_LIB zstd::libzstd_static)
elseif(TARGET zstd::libzstd)
  set(TIGHTROPE_HAS_ZSTD_DECOMPRESSION ON)
  set(TIGHTROPE_ZSTD_LINK_LIB zstd::libzstd)
else()
  find_path(TIGHTROPE_ZSTD_INCLUDE_DIR zstd.h HINTS /opt/homebrew/include /usr/local/include)
  find_library(TIGHTROPE_ZSTD_LIBRARY NAMES zstd libzstd HINTS /opt/homebrew/lib /usr/local/lib)
  if(TIGHTROPE_ZSTD_INCLUDE_DIR AND TIGHTROPE_ZSTD_LIBRARY)
    set(TIGHTROPE_HAS_ZSTD_DECOMPRESSION ON)
    list(APPEND TIGHTROPE_ZSTD_INCLUDE_DIRS ${TIGHTROPE_ZSTD_INCLUDE_DIR})
    set(TIGHTROPE_ZSTD_LINK_LIB ${TIGHTROPE_ZSTD_LIBRARY})
  endif()
endif()
if(TIGHTROPE_HAS_ZSTD_DECOMPRESSION)
  add_compile_definitions(TIGHTROPE_HAS_ZSTD_DECOMPRESSION=1)
else()
  add_compile_definitions(TIGHTROPE_HAS_ZSTD_DECOMPRESSION=0)
endif()

set(TIGHTROPE_HAS_BROTLI_DECOMPRESSION OFF)
set(TIGHTROPE_BROTLI_INCLUDE_DIRS "")
set(TIGHTROPE_BROTLI_LINK_LIB "")
find_package(unofficial-brotli CONFIG QUIET)
if(TARGET unofficial::brotli::brotlidec)
  set(TIGHTROPE_HAS_BROTLI_DECOMPRESSION ON)
  set(TIGHTROPE_BROTLI_LINK_LIB unofficial::brotli::brotlidec)
else()
  find_path(TIGHTROPE_BROTLI_INCLUDE_DIR brotli/decode.h HINTS /opt/homebrew/include /usr/local/include)
  find_library(TIGHTROPE_BROTLI_LIBRARY NAMES brotlidec libbrotlidec HINTS /opt/homebrew/lib /usr/local/lib)
  if(TIGHTROPE_BROTLI_INCLUDE_DIR AND TIGHTROPE_BROTLI_LIBRARY)
    set(TIGHTROPE_HAS_BROTLI_DECOMPRESSION ON)
    list(APPEND TIGHTROPE_BROTLI_INCLUDE_DIRS ${TIGHTROPE_BROTLI_INCLUDE_DIR})
    set(TIGHTROPE_BROTLI_LINK_LIB ${TIGHTROPE_BROTLI_LIBRARY})
  endif()
endif()
if(TIGHTROPE_HAS_BROTLI_DECOMPRESSION)
  add_compile_definitions(TIGHTROPE_HAS_BROTLI_DECOMPRESSION=1)
else()
  add_compile_definitions(TIGHTROPE_HAS_BROTLI_DECOMPRESSION=0)
endif()

if(TARGET NuRaft::nuraft)
  set(TIGHTROPE_NURAFT_TARGET NuRaft::nuraft)
elseif(TARGET NuRaft::static_lib)
  set(TIGHTROPE_NURAFT_TARGET NuRaft::static_lib)
else()
  message(FATAL_ERROR "NuRaft target not found (expected NuRaft::nuraft or NuRaft::static_lib)")
endif()

# Keep CONFIGURE_DEPENDS here: the addon links with -undefined dynamic_lookup for
# Node-API symbols, so stale source globs can otherwise hide missing internal
# symbols until runtime (hard crash on first call-site).
file(GLOB_RECURSE TIGHTROPE_SOURCES CONFIGURE_DEPENDS
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
if(NOT WIN32)
  file(GLOB_RECURSE TIGHTROPE_P2P_SOURCES CONFIGURE_DEPENDS
    native/p2p/*.cpp
  )
  list(APPEND TIGHTROPE_SOURCES ${TIGHTROPE_P2P_SOURCES})
endif()

set(TIGHTROPE_SYNC_EVENT_EMITTER_NODE_SOURCE
  ${CMAKE_SOURCE_DIR}/native/sync/src/sync_event_emitter.cpp
)
set(TIGHTROPE_SYNC_EVENT_EMITTER_NOOP_SOURCE
  ${CMAKE_SOURCE_DIR}/native/sync/src/sync_event_emitter_noop.cpp
)
list(REMOVE_ITEM TIGHTROPE_SOURCES ${TIGHTROPE_SYNC_EVENT_EMITTER_NOOP_SOURCE})

set(TIGHTROPE_TEST_ONLY_CONSENSUS_SOURCES
  ${CMAKE_SOURCE_DIR}/native/sync/src/consensus/raft_log.cpp
  ${CMAKE_SOURCE_DIR}/native/sync/src/consensus/raft_rpc.cpp
  ${CMAKE_SOURCE_DIR}/native/sync/src/consensus/raft_scheduler.cpp
)
list(REMOVE_ITEM TIGHTROPE_SOURCES ${TIGHTROPE_TEST_ONLY_CONSENSUS_SOURCES})

set(TIGHTROPE_COMMON_LINK_LIBS
  ${TIGHTROPE_NURAFT_TARGET}
  unofficial::uwebsockets::uwebsockets
  MbedTLS::mbedtls MbedTLS::mbedcrypto MbedTLS::mbedx509
  Boost::log Boost::log_setup Boost::asio Boost::system Boost::uuid
  sqlcipher::sqlcipher
  SQLiteCpp
  glaze::glaze
  lz4::lz4
  unofficial-sodium::sodium
  quill::quill
  tomlplusplus::tomlplusplus
  $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
  CURL::libcurl
  OpenSSL::SSL OpenSSL::Crypto
)
if(TIGHTROPE_ZSTD_LINK_LIB)
  list(APPEND TIGHTROPE_COMMON_LINK_LIBS ${TIGHTROPE_ZSTD_LINK_LIB})
endif()
if(TIGHTROPE_BROTLI_LINK_LIB)
  list(APPEND TIGHTROPE_COMMON_LINK_LIBS ${TIGHTROPE_BROTLI_LINK_LIB})
endif()
if(NOT WIN32)
  list(APPEND TIGHTROPE_COMMON_LINK_LIBS opendht)
endif()
if(WIN32)
  list(APPEND TIGHTROPE_COMMON_LINK_LIBS ws2_32)
endif()

file(GLOB_RECURSE TIGHTROPE_NATIVE_DIR_CANDIDATES CONFIGURE_DEPENDS LIST_DIRECTORIES true
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
target_compile_definitions(tightrope-core PRIVATE
  TIGHTROPE_SYNC_EVENT_EMITTER_ENABLE_NODE_BRIDGE=1
)

if(CMAKE_JS_SRC)
  target_sources(tightrope-core PRIVATE ${CMAKE_JS_SRC})
endif()

target_include_directories(tightrope-core PRIVATE
  ${CMAKE_SOURCE_DIR}/native
  ${TIGHTROPE_NATIVE_INCLUDE_DIRS_INTERNAL}
  ${CMAKE_SOURCE_DIR}/vendor
  ${TIGHTROPE_ZSTD_INCLUDE_DIRS}
  ${TIGHTROPE_BROTLI_INCLUDE_DIRS}
  ${TIGHTROPE_NODE_INCLUDE_DIRS}
)

target_link_libraries(tightrope-core PRIVATE
  ${TIGHTROPE_COMMON_LINK_LIBS}
  ${TIGHTROPE_NODE_LIBS}
)

set_target_properties(tightrope-core PROPERTIES
  PREFIX "" SUFFIX ".node"
)

option(BUILD_TESTING "Build tests" OFF)
if(BUILD_TESTING)
  find_package(Catch2 CONFIG REQUIRED)
  file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/native/tests/*.cpp
  )
  set(POSIX_APPLE_ONLY_TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/native/tests/config/config_loader_test.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/db/migration_runner_test.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/integration/proxy/proxy_default_transport_integration_test.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/integration/proxy/proxy_upstream_transport_test.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/integration/sync/journal_sync_test.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/integration/sync/persistent_journal_test.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/integration/sync/sync_engine_test.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/integration/sync/sync_schema_test.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/server/settings_controller_test.cpp
    ${CMAKE_SOURCE_DIR}/native/tests/unit/logging/quill_logger_test.cpp
  )
  if(APPLE)
    # Keep POSIX-only tests enabled on Apple platforms.
  else()
    list(REMOVE_ITEM TEST_SOURCES ${POSIX_APPLE_ONLY_TEST_SOURCES})
  endif()

  set(TIGHTROPE_TESTABLE_SOURCES ${TIGHTROPE_SOURCES})
  list(APPEND TIGHTROPE_TESTABLE_SOURCES ${TIGHTROPE_TEST_ONLY_CONSENSUS_SOURCES})
  list(REMOVE_ITEM TIGHTROPE_TESTABLE_SOURCES
    ${CMAKE_SOURCE_DIR}/native/bridge/src/addon.cpp
    ${CMAKE_SOURCE_DIR}/native/bridge/src/addon_bindings_support.cpp
    ${TIGHTROPE_SYNC_EVENT_EMITTER_NODE_SOURCE}
    ${TIGHTROPE_SYNC_EVENT_EMITTER_NOOP_SOURCE}
  )
  list(APPEND TIGHTROPE_TESTABLE_SOURCES ${TIGHTROPE_SYNC_EVENT_EMITTER_NOOP_SOURCE})
  add_library(tightrope-core-testlib STATIC ${TIGHTROPE_TESTABLE_SOURCES})
  target_include_directories(tightrope-core-testlib PRIVATE
    ${CMAKE_SOURCE_DIR}/native
    ${TIGHTROPE_NATIVE_INCLUDE_DIRS_INTERNAL}
    ${CMAKE_SOURCE_DIR}/vendor
    ${TIGHTROPE_ZSTD_INCLUDE_DIRS}
    ${TIGHTROPE_BROTLI_INCLUDE_DIRS}
    ${TIGHTROPE_NODE_INCLUDE_DIRS}
  )
  target_link_libraries(tightrope-core-testlib PUBLIC
    ${TIGHTROPE_COMMON_LINK_LIBS}
    ${TIGHTROPE_NODE_LIBS}
  )

  add_executable(tightrope-tests ${TEST_SOURCES})
  target_include_directories(tightrope-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/native
    ${TIGHTROPE_NATIVE_INCLUDE_DIRS_INTERNAL}
    ${CMAKE_SOURCE_DIR}/vendor
    ${TIGHTROPE_ZSTD_INCLUDE_DIRS}
    ${TIGHTROPE_BROTLI_INCLUDE_DIRS}
    ${TIGHTROPE_NODE_INCLUDE_DIRS}
  )
  target_link_libraries(tightrope-tests PRIVATE
    Catch2::Catch2WithMain
    tightrope-core-testlib
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

  add_executable(tightrope-cluster-node
    ${CMAKE_SOURCE_DIR}/native/tools/cluster_node_main.cpp
  )
  target_include_directories(tightrope-cluster-node PRIVATE
    ${CMAKE_SOURCE_DIR}/native
    ${TIGHTROPE_NATIVE_INCLUDE_DIRS_INTERNAL}
    ${CMAKE_SOURCE_DIR}/vendor
    ${TIGHTROPE_NODE_INCLUDE_DIRS}
  )
  target_link_libraries(tightrope-cluster-node PRIVATE
    tightrope-core-testlib
  )

  add_executable(tightrope-dht-probe
    ${CMAKE_SOURCE_DIR}/native/tools/dht_probe_main.cpp
  )
  target_include_directories(tightrope-dht-probe PRIVATE
    ${CMAKE_SOURCE_DIR}/native
    ${TIGHTROPE_NATIVE_INCLUDE_DIRS_INTERNAL}
    ${CMAKE_SOURCE_DIR}/vendor
    ${TIGHTROPE_NODE_INCLUDE_DIRS}
  )
  target_link_libraries(tightrope-dht-probe PRIVATE
    tightrope-core-testlib
  )
endif()
