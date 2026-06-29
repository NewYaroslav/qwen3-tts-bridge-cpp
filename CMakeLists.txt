cmake_minimum_required(VERSION 3.16)

project(QwenTTSBridge
    VERSION 0.2.0
    DESCRIPTION "C++17 bridge for a persistent Qwen3-TTS worker"
    LANGUAGES CXX
)

option(QWEN_TTS_BRIDGE_BUILD_EXAMPLES "Build QwenTTSBridge examples" ON)
option(QWEN_TTS_BRIDGE_BUILD_TESTS "Build QwenTTSBridge tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(QWEN_TTS_BRIDGE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(QWEN_TTS_BRIDGE_WORKER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/worker")
set(QWEN_TTS_BRIDGE_EXTERNAL_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/cpp")
set(QWEN_TTS_BRIDGE_EXTERNAL_PYTHON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/python")

add_subdirectory("${QWEN_TTS_BRIDGE_EXTERNAL_CPP_DIR}/tiny-process-library" EXCLUDE_FROM_ALL)
set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory("${QWEN_TTS_BRIDGE_EXTERNAL_CPP_DIR}/nlohmann-json" EXCLUDE_FROM_ALL)

if(WIN32)
    target_compile_definitions(tiny-process-library
        PUBLIC
            UNICODE
            _UNICODE
    )
endif()

add_library(qwen_tts_bridge
    src/qwen_tts_bridge/protocol/control/ControlCodec.cpp
    src/qwen_tts_bridge/protocol/framing/FrameCodec.cpp
    src/qwen_tts_bridge/protocol/framing/FrameParser.cpp
    src/qwen_tts_bridge/transport/stdio/StdIoTransport.cpp
)
add_library(QwenTTSBridge::qwen_tts_bridge ALIAS qwen_tts_bridge)

target_compile_features(qwen_tts_bridge PUBLIC cxx_std_17)

target_include_directories(qwen_tts_bridge
    PUBLIC
        "${QWEN_TTS_BRIDGE_SRC_DIR}"
)

target_link_libraries(qwen_tts_bridge
    PRIVATE
        nlohmann_json::nlohmann_json
        tiny-process-library::tiny-process-library
)

if(QWEN_TTS_BRIDGE_BUILD_TESTS)
    enable_testing()

    add_executable(protocol_frame_test
        tests/protocol_frame_test.cpp
    )

    target_link_libraries(protocol_frame_test
        PRIVATE
            QwenTTSBridge::qwen_tts_bridge
    )

    add_test(NAME protocol_frame_test COMMAND protocol_frame_test)

    add_executable(control_codec_test
        tests/control_codec_test.cpp
    )

    target_link_libraries(control_codec_test
        PRIVATE
            QwenTTSBridge::qwen_tts_bridge
    )

    add_test(NAME control_codec_test COMMAND control_codec_test)

    find_package(Python3 COMPONENTS Interpreter QUIET)

    if(Python3_Interpreter_FOUND)
        add_executable(stdio_transport_test
            tests/stdio_transport_test.cpp
        )

        target_link_libraries(stdio_transport_test
            PRIVATE
                QwenTTSBridge::qwen_tts_bridge
        )

        target_compile_definitions(stdio_transport_test
            PRIVATE
                QWEN_TTS_BRIDGE_TEST_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
                QWEN_TTS_BRIDGE_TEST_WORKER_DIR="${QWEN_TTS_BRIDGE_WORKER_DIR}"
        )

        add_test(NAME stdio_transport_test COMMAND stdio_transport_test)
    else()
        message(WARNING "Python3 interpreter not found; stdio_transport_test will not be built")
    endif()
endif()
