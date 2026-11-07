cmake_minimum_required(VERSION 3.20)
project(clinicflow-backend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Dependencies: Oat++ 1.3.0 + oatpp-swagger, libpq, OpenSSL, libargon2, nlohmann/json.
# CMAKE_PREFIX_PATH should point at their install prefix (see Dockerfile / docs/SETUP.md).
find_package(oatpp 1.3.0 REQUIRED)
find_package(oatpp-swagger 1.3.0 REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(PostgreSQL QUIET)
find_path(ARGON2_INCLUDE_DIR argon2.h REQUIRED)
find_library(ARGON2_LIBRARY NAMES libargon2.a argon2 REQUIRED)
find_path(NLOHMANN_INCLUDE_DIR nlohmann/json.hpp REQUIRED)
if(PostgreSQL_FOUND)
  set(PQ_INCLUDE ${PostgreSQL_INCLUDE_DIRS})
  set(PQ_LIB ${PostgreSQL_LIBRARIES})
else()
  find_path(PQ_INCLUDE libpq-fe.h REQUIRED)
  find_library(PQ_LIB NAMES pq libpq.so.5 REQUIRED)
endif()

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)

add_library(clinicflow-core STATIC ${CORE_SOURCES})
target_include_directories(clinicflow-core PUBLIC src ${PQ_INCLUDE} ${ARGON2_INCLUDE_DIR} ${NLOHMANN_INCLUDE_DIR})
target_link_libraries(clinicflow-core PUBLIC
  oatpp::oatpp oatpp::oatpp-swagger ${PQ_LIB} OpenSSL::Crypto ${ARGON2_LIBRARY} Threads::Threads)
target_compile_options(clinicflow-core PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_executable(clinicflow-server src/main.cpp)
target_link_libraries(clinicflow-server PRIVATE clinicflow-core)
target_compile_options(clinicflow-server PRIVATE -Wall -Wno-unused-parameter)

# Unit tests (pure logic, no database required)
enable_testing()
add_executable(clinicflow-unit-tests test/unit_tests.cpp)
target_link_libraries(clinicflow-unit-tests PRIVATE clinicflow-core)
add_test(NAME unit COMMAND clinicflow-unit-tests)
