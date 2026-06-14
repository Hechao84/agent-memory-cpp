cmake_minimum_required(VERSION 3.15)
project(agent-memory-cpp LANGUAGES C CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(AGENT_MEMORY_BUILD_TOOLS "Build memory-server" ON)
option(AGENT_MEMORY_BUILD_TESTS "Build agent memory tests" ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -fPIC)
endif()

find_package(CURL REQUIRED)
find_package(nlohmann_json QUIET)

set(AGENT_MEMORY_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(LOCAL_SQLITE_SRC "${AGENT_MEMORY_THIRD_PARTY_DIR}/src/sqlite3-src/sqlite3.c")

set(AGENT_MEMORY_INSTALL_SQLITE OFF)
if(TARGET sqlite3)
    set(AGENT_MEMORY_SQLITE_TARGET sqlite3)
elseif(EXISTS "${LOCAL_SQLITE_SRC}")
    add_library(sqlite3 SHARED ${LOCAL_SQLITE_SRC})
    target_include_directories(sqlite3 PUBLIC "${AGENT_MEMORY_THIRD_PARTY_DIR}/include")
    target_compile_definitions(sqlite3 PRIVATE SQLITE_ENABLE_FTS5)
    if(WIN32)
        target_compile_definitions(sqlite3 PRIVATE SQLITE_API=__declspec(dllexport))
    else()
        target_link_libraries(sqlite3 PUBLIC dl pthread)
    endif()
    set(AGENT_MEMORY_SQLITE_TARGET sqlite3)
    set(AGENT_MEMORY_INSTALL_SQLITE ON)
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

set(AGENT_MEMORY_CORE_SOURCES
    src/core/builtin_memory_runtime.cpp
    src/core/context_builder.cpp
    src/core/payload_service.cpp
    src/core/runtime_paths.cpp
    src/core/runtime_services.cpp
    src/core/runtime_store_initializer.cpp
)

set(AGENT_MEMORY_UTILS_SOURCES
    src/utils/curl_client.cpp
    src/utils/file_util.cpp
    src/utils/path_util.cpp
)

set(AGENT_MEMORY_SERVER_SOURCES
    src/server/server_common.cpp
)

set(AGENT_MEMORY_CONSOLIDATION_SOURCES
    src/consolidation/consolidation_batch_builder.cpp
    src/consolidation/consolidation_service.cpp
    src/consolidation/llm_processor.cpp
    src/consolidation/memory_update_writer.cpp
    src/consolidation/rule_based_processor.cpp
)

set(AGENT_MEMORY_STORAGE_SOURCES
    src/storage/sqlite_store.cpp
)

set(AGENT_MEMORY_MODEL_SOURCES
    src/model/model_config.cpp
    src/model/model_http_client.cpp
    src/model/model_client_factory.cpp
    src/model/provider/anthropic/anthropic_model_client.cpp
    src/model/provider/openai/openai_model_client.cpp
)

set(AGENT_MEMORY_SERIALIZATION_SOURCES
    src/serialization/json_memory_codec.cpp
)

set(AGENT_MEMORY_TRANSPORT_SOURCES
    src/transport/http/memory_http_server.cpp
    src/transport/mcp/memory_mcp_protocol.cpp
)

set(AGENT_MEMORY_PUBLIC_HEADERS
    include/agent_memory/builtin_memory_runtime.h
    include/agent_memory/config.h
    include/agent_memory/context.h
    include/agent_memory/error.h
    include/agent_memory/event.h
    include/agent_memory/export.h
    include/agent_memory/long_term_memory.h
    include/agent_memory/model_client.h
    include/agent_memory/payload.h
    include/agent_memory/runtime.h
    include/agent_memory/search.h
    include/agent_memory/stats.h
)

add_library(agent_memory SHARED
    ${AGENT_MEMORY_CORE_SOURCES}
    ${AGENT_MEMORY_UTILS_SOURCES}
    ${AGENT_MEMORY_CONSOLIDATION_SOURCES}
    ${AGENT_MEMORY_STORAGE_SOURCES}
    ${AGENT_MEMORY_MODEL_SOURCES}
    ${AGENT_MEMORY_SERIALIZATION_SOURCES}
    ${AGENT_MEMORY_TRANSPORT_SOURCES}
)

target_include_directories(agent_memory PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(agent_memory PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/consolidation
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/provider/anthropic
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/provider/openai
    ${CMAKE_CURRENT_SOURCE_DIR}/src/serialization
    ${CMAKE_CURRENT_SOURCE_DIR}/src/storage
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/http
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/mcp
    ${AGENT_MEMORY_THIRD_PARTY_DIR}/include
)
target_compile_definitions(agent_memory PRIVATE BUILDING_AGENT_MEMORY)
target_link_libraries(agent_memory PUBLIC nlohmann_json::nlohmann_json)
target_link_libraries(agent_memory PRIVATE ${AGENT_MEMORY_SQLITE_TARGET} CURL::libcurl pthread)

function(agent_memory_apply_common_includes target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${AGENT_MEMORY_THIRD_PARTY_DIR}/include
    )
endfunction()

function(agent_memory_add_test target)
    add_executable(${target} ${ARGN})
    agent_memory_apply_common_includes(${target})
    target_link_libraries(${target} PRIVATE agent_memory)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

if(AGENT_MEMORY_BUILD_TOOLS)
    add_executable(memory-server
        src/server/server_main.cpp
        src/server/server_cli.cpp
        src/server/server_common.cpp
        src/server/server_options.cpp
    )
    agent_memory_apply_common_includes(memory-server)
    target_include_directories(memory-server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model
        ${CMAKE_CURRENT_SOURCE_DIR}/src/server
        ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/http
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    )
    target_link_libraries(memory-server PRIVATE agent_memory nlohmann_json::nlohmann_json pthread)
endif()

if(AGENT_MEMORY_BUILD_TESTS)
    enable_testing()

    agent_memory_add_test(agent_memory_tests tests/test_memory_runtime.cpp)

    agent_memory_add_test(consolidation_tests tests/test_consolidation.cpp)
    target_include_directories(consolidation_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/consolidation
        ${CMAKE_CURRENT_SOURCE_DIR}/src/storage
    )

    agent_memory_add_test(model_tests tests/test_model.cpp)
    target_include_directories(model_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model/provider/anthropic
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model/provider/openai
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    )
    target_link_libraries(model_tests PRIVATE nlohmann_json::nlohmann_json)

    agent_memory_add_test(serialization_tests tests/test_serialization.cpp)
    target_include_directories(serialization_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/serialization
    )
    target_link_libraries(serialization_tests PRIVATE nlohmann_json::nlohmann_json)

    agent_memory_add_test(sqlite_store_tests tests/test_sqlite_store.cpp)
    target_include_directories(sqlite_store_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/consolidation
        ${CMAKE_CURRENT_SOURCE_DIR}/src/storage
    )

    agent_memory_add_test(server_tests
        tests/test_server.cpp
        src/server/server_cli.cpp
        src/server/server_common.cpp
        src/server/server_options.cpp
    )
    target_include_directories(server_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model
        ${CMAKE_CURRENT_SOURCE_DIR}/src/server
        ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/http
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    )
    target_link_libraries(server_tests PRIVATE nlohmann_json::nlohmann_json pthread)
endif()

install(TARGETS agent_memory
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
if(AGENT_MEMORY_INSTALL_SQLITE)
    install(TARGETS sqlite3
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)
endif()
if(AGENT_MEMORY_BUILD_TOOLS)
    install(TARGETS memory-server RUNTIME DESTINATION bin)
endif()
install(FILES ${AGENT_MEMORY_PUBLIC_HEADERS} DESTINATION include/agent_memory)
if(EXISTS "${AGENT_MEMORY_THIRD_PARTY_DIR}/include/nlohmann")
    install(DIRECTORY "${AGENT_MEMORY_THIRD_PARTY_DIR}/include/nlohmann" DESTINATION include)
endif()
install(DIRECTORY examples/ DESTINATION examples)
