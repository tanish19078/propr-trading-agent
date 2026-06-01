cmake_minimum_required(VERSION 3.22)
project(propr_agent CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(
  -Wall -Wextra
  -Wno-unused-parameter
  -Wno-missing-field-initializers
)
# We use __int128 deliberately for percent/bps math without float lossage.
# -Wpedantic flags it as a GNU extension; opt out where it's used.
# -Werror is opt-in via PROPR_WERROR for CI.
if(PROPR_WERROR)
  add_compile_options(-Werror)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# --- Dependencies --------------------------------------------------------
# Most deps come from the system (vcpkg on Linux/MSVC, pacman on MSYS2/UCRT64).
# A few are not packaged everywhere (ixwebsocket, SQLiteCpp, concurrentqueue);
# fetch them from source so the build works on any host with cmake + a compiler.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(tl-expected CONFIG QUIET)
if(NOT tl-expected_FOUND)
  FetchContent_Declare(tl_expected
    GIT_REPOSITORY https://github.com/TartanLlama/expected.git
    GIT_TAG v1.1.0
    GIT_SHALLOW ON)
  set(EXPECTED_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(tl_expected)
endif()

# ixwebsocket: not packaged in MSYS2 / not always in vcpkg by name.
find_package(ixwebsocket CONFIG QUIET)
if(NOT ixwebsocket_FOUND)
  set(USE_TLS ON CACHE BOOL "" FORCE)
  set(USE_OPEN_SSL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG v11.4.5
    GIT_SHALLOW ON)
  FetchContent_MakeAvailable(ixwebsocket)
endif()

# SQLiteCpp: thin C++ wrapper around the system sqlite3.
find_package(SQLiteCpp CONFIG QUIET)
if(NOT SQLiteCpp_FOUND)
  set(SQLITECPP_RUN_CPPLINT OFF CACHE BOOL "" FORCE)
  set(SQLITECPP_RUN_CPPCHECK OFF CACHE BOOL "" FORCE)
  set(SQLITECPP_INTERNAL_SQLITE OFF CACHE BOOL "" FORCE)
  set(SQLITE_ENABLE_COLUMN_METADATA OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SQLiteCpp
    GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
    GIT_TAG 3.3.2
    GIT_SHALLOW ON)
  FetchContent_MakeAvailable(SQLiteCpp)
endif()

# concurrentqueue: header-only MPSC queue.
find_package(unofficial-concurrentqueue CONFIG QUIET)
if(NOT unofficial-concurrentqueue_FOUND)
  FetchContent_Declare(concurrentqueue
    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
    GIT_TAG v1.0.4
    GIT_SHALLOW ON)
  FetchContent_MakeAvailable(concurrentqueue)
  # FetchContent doesn't always emit the namespaced alias the repo expects;
  # synthesise an INTERFACE target named `unofficial::concurrentqueue::concurrentqueue`
  # so the rest of the project keeps the same link line on every host.
  if(NOT TARGET unofficial::concurrentqueue::concurrentqueue)
    add_library(_propr_concurrentqueue INTERFACE)
    target_include_directories(_propr_concurrentqueue INTERFACE
      ${concurrentqueue_SOURCE_DIR})
    add_library(unofficial::concurrentqueue::concurrentqueue ALIAS
      _propr_concurrentqueue)
  endif()
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

# Interface target — everything strategies and tests link against for the public ABI
add_library(propr_iface INTERFACE)
target_include_directories(propr_iface INTERFACE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(propr_iface INTERFACE
  nlohmann_json::nlohmann_json
  fmt::fmt
  tl::expected
  unofficial::concurrentqueue::concurrentqueue
)

# Core library - everything except app/main and strategy plugins
file(GLOB_RECURSE PROPR_CORE_SRC CONFIGURE_DEPENDS
  src/core/*.cpp
  src/account/*.cpp
  src/risk/*.cpp
  src/exec/*.cpp
  src/strategy/*.cpp
  src/persist/*.cpp
  src/config/*.cpp
  src/log/*.cpp
  src/net/*.cpp
  src/app/*.cpp
  src/schemas/*.cpp
  src/sim/*.cpp
)

add_library(propr_core STATIC ${PROPR_CORE_SRC})
target_link_libraries(propr_core
  PUBLIC propr_iface
  PRIVATE
    cpr::cpr
    ixwebsocket::ixwebsocket
    yaml-cpp::yaml-cpp
    spdlog::spdlog
    SQLiteCpp
    unofficial::concurrentqueue::concurrentqueue
    ${CMAKE_DL_LIBS}
)

add_subdirectory(app)
# Backtest binary is parked while we focus on demo-account testing. The CSV
# feeder + fake account compile cleanly, but main.cpp still references the
# pre-V1 Intent type. Re-enable once range_mr is validated against the demo
# account and we want offline historical runs.
# add_subdirectory(backtest)
add_subdirectory(strategies/range_mr)

enable_testing()
add_subdirectory(tests)
