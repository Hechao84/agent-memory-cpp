cmake_minimum_required(VERSION 3.15)
project(agent-memory-cpp LANGUAGES C CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(AGENT_MEMORY_BUILD_TOOLS "Build memory-server and memory-mcp-server" ON)
option(AGENT_MEMORY_BUILD_TESTS "Build agent memory tests" ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -fPIC)
endif()

find_package(CURL REQUIRED)
find_package(nlohmann_json QUIET)

set(AGENT_MEMORY_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(LOCAL_SQLITE_SRC "${AGENT_MEMORY_THIRD_PARTY_DIR}/src/sqlite3-src/sqlite3.c")

if(TARGET sqlite3)
    set(AGENT_MEMORY_SQLITE_TARGET sqlite3)
elseif(EXISTS "${LOCAL_SQLITE_SRC}")
    add_library(sqlite3 STATIC ${LOCAL_SQLITE_SRC})
    target_include_directories(sqlite3 PUBLIC "${AGENT_MEMORY_THIRD_PARTY_DIR}/include")
    if(NOT WIN32)
        target_link_libraries(sqlite3 PUBLIC dl pthread)
    endif()
    set(AGENT_MEMORY_SQLITE_TARGET sqlite3)
else()
    find_package(SQLite3 REQUIRED)
    set(AGENT_MEMORY_SQLITE_TARGET SQLite::SQLite3)
endif()

if(NOT TARGET nlohmann_json::nlohmann_json)
    if(NOT nlohmann_json_FOUND)
        add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED GLOBAL)
        set_property(TARGET nlohmann_json::nlohmann_json PROPERTY INTERFACE_INCLUDE_DIRECTORIES
            "${AGENT_MEMORY_THIRD_PARTY_DIR}/include")
    endif()
endif()

add_library(agent_memory SHARED
    src/builtin_memory_runtime.cpp
    src/http_memory_runtime.cpp
    src/long_term_memory_processor.cpp
    src/memory_sqlite_store.cpp
    src/openai_memory_model_client.cpp
)

target_include_directories(agent_memory PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_include_directories(agent_memory PRIVATE
    ${AGENT_MEMORY_THIRD_PARTY_DIR}/include
)
target_compile_definitions(agent_memory PRIVATE BUILDING_AGENT_MEMORY)
target_link_libraries(agent_memory PUBLIC ${AGENT_MEMORY_SQLITE_TARGET} nlohmann_json::nlohmann_json)
target_link_libraries(agent_memory PRIVATE CURL::libcurl pthread)

if(AGENT_MEMORY_BUILD_TOOLS)
    add_executable(memory-server src/server_main.cpp)
    target_include_directories(memory-server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${AGENT_MEMORY_THIRD_PARTY_DIR}/include
    )
    target_link_libraries(memory-server PRIVATE agent_memory nlohmann_json::nlohmann_json pthread)

    add_executable(memory-mcp-server src/mcp_main.cpp)
    target_include_directories(memory-mcp-server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(memory-mcp-server PRIVATE agent_memory nlohmann_json::nlohmann_json pthread)
endif()

if(AGENT_MEMORY_BUILD_TESTS)
    add_executable(agent_memory_tests tests/test_memory_runtime.cpp)
    target_include_directories(agent_memory_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(agent_memory_tests PRIVATE agent_memory)
endif()

install(TARGETS agent_memory
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
if(AGENT_MEMORY_BUILD_TOOLS)
    install(TARGETS memory-server memory-mcp-server RUNTIME DESTINATION bin)
endif()
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY examples/ DESTINATION examples)
