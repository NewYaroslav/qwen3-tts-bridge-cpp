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

if(QWEN_TTS_BRIDGE_BUILD_EXAMPLES OR QWEN_TTS_BRIDGE_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter QUIET)
endif()

if(WIN32)
    target_compile_definitions(tiny-process-library
        PUBLIC
            UNICODE
            _UNICODE
    )
endif()

add_library(qwen_tts_bridge
    src/qwen_tts_bridge/client/QwenTtsClient.cpp
    src/qwen_tts_bridge/protocol/control/ControlCodec.cpp
    src/qwen_tts_bridge/protocol/control/ControlDecode.cpp
    src/qwen_tts_bridge/protocol/control/ControlEncode.cpp
    src/qwen_tts_bridge/protocol/control/ControlJson.cpp
    src/qwen_tts_bridge/protocol/control/ControlValidation.cpp
    src/qwen_tts_bridge/protocol/control/ErrorCodec.cpp
    src/qwen_tts_bridge/protocol/framing/FrameCodec.cpp
    src/qwen_tts_bridge/protocol/framing/FrameParser.cpp
    src/qwen_tts_bridge/session/WorkerSession.cpp
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

if(QWEN_TTS_BRIDGE_BUILD_EXAMPLES)
    set(QWEN_TTS_BRIDGE_EXAMPLE_PYTHON_EXECUTABLE "")
    if(Python3_Interpreter_FOUND)
        set(QWEN_TTS_BRIDGE_EXAMPLE_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")
    endif()

    add_executable(qwen_tts_save_wav
        examples/WavWriter.cpp
        examples/qwen_tts_save_wav.cpp
    )

    target_link_libraries(qwen_tts_save_wav
        PRIVATE
            QwenTTSBridge::qwen_tts_bridge
    )

    target_compile_definitions(qwen_tts_save_wav
        PRIVATE
            QWEN_TTS_BRIDGE_EXAMPLE_PYTHON_EXECUTABLE="${QWEN_TTS_BRIDGE_EXAMPLE_PYTHON_EXECUTABLE}"
            QWEN_TTS_BRIDGE_EXAMPLE_WORKER_DIR="${QWEN_TTS_BRIDGE_WORKER_DIR}"
    )
endif()

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

        add_executable(worker_session_test
            tests/worker_session_test.cpp
        )

        target_link_libraries(worker_session_test
            PRIVATE
                QwenTTSBridge::qwen_tts_bridge
        )

        target_compile_definitions(worker_session_test
            PRIVATE
                QWEN_TTS_BRIDGE_TEST_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
                QWEN_TTS_BRIDGE_TEST_WORKER_DIR="${QWEN_TTS_BRIDGE_WORKER_DIR}"
        )

        add_test(NAME worker_session_test COMMAND worker_session_test)

        add_executable(qwen_tts_client_test
            tests/qwen_tts_client_test.cpp
        )

        target_link_libraries(qwen_tts_client_test
            PRIVATE
                QwenTTSBridge::qwen_tts_bridge
        )

        target_compile_definitions(qwen_tts_client_test
            PRIVATE
                QWEN_TTS_BRIDGE_TEST_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
                QWEN_TTS_BRIDGE_TEST_WORKER_DIR="${QWEN_TTS_BRIDGE_WORKER_DIR}"
        )

        add_test(NAME qwen_tts_client_test COMMAND qwen_tts_client_test)

        if(QWEN_TTS_BRIDGE_BUILD_EXAMPLES)
            add_test(
                NAME qwen_tts_save_wav_mock_example
                COMMAND qwen_tts_save_wav
                    --mock
                    --output "${CMAKE_CURRENT_BINARY_DIR}/qwen_tts_save_wav_mock.wav"
                    --text "Example synthesis request"
                    --mock-chunks 2
                    --request-timeout-ms 10000
            )
        endif()
    else()
        message(WARNING "Python3 interpreter not found; stdio/worker session tests will not be built")
    endif()
endif()
