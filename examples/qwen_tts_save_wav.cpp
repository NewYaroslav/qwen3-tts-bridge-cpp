#include "SaveWavCallbacks.hpp"

#include <qwen_tts_bridge/client.hpp>
#include <qwen_tts_bridge/transport.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef QWEN_TTS_BRIDGE_EXAMPLE_PYTHON_EXECUTABLE
#define QWEN_TTS_BRIDGE_EXAMPLE_PYTHON_EXECUTABLE ""
#endif

#ifndef QWEN_TTS_BRIDGE_EXAMPLE_WORKER_DIR
#define QWEN_TTS_BRIDGE_EXAMPLE_WORKER_DIR ""
#endif

namespace {

using qwen_tts_bridge::AudioFormat;
using qwen_tts_bridge::QwenTtsClient;
using qwen_tts_bridge::QwenTtsClientOptions;
using qwen_tts_bridge::RequestId;
using qwen_tts_bridge::StdIoTransportOptions;
using qwen_tts_bridge::TtsRequest;
using qwen_tts_bridge::examples::SaveWavState;
using qwen_tts_bridge::examples::WavWriter;
using qwen_tts_bridge::examples::make_save_wav_callbacks;
using qwen_tts_bridge::examples::wait_for_save_wav_terminal;

struct ProgramOptions {
    bool help = false;
    bool use_mock_worker = false;
    std::string worker_executable;
    std::vector<std::string> worker_arguments;
    std::string working_directory;
    std::string output_path;
    std::string text;
    std::string language = "auto";
    std::string speaker = "default";
    std::string instruction;
    std::uint32_t sample_rate = 24000;
    std::uint32_t channels = 1;
    int mock_chunks = 3;
    int mock_chunk_ms = 100;
    double mock_chunk_delay = 0.0;
    std::chrono::milliseconds startup_timeout{30000};
    std::chrono::milliseconds request_timeout{60000};
};

void print_usage(std::ostream& out, const char* executable_name) {
    out << "Usage:\n"
        << "  " << executable_name << " --mock --output out.wav --text \"Hello\"\n"
        << "  " << executable_name << " --worker qwen_tts_worker.exe --output out.wav --text \"Hello\"\n\n"
        << "Options:\n"
        << "  --help                         Show this help.\n"
        << "  --mock                         Run the bundled Python mock worker.\n"
        << "  --worker <path>                Worker executable path.\n"
        << "  --worker-arg <arg>             Extra worker argument; may be repeated.\n"
        << "  --cwd <path>                   Worker working directory.\n"
        << "  --output <path>                Output WAV path.\n"
        << "  --text <utf8>                  Text to synthesize.\n"
        << "  --language <name>              Request language, default: auto.\n"
        << "  --speaker <name>               Request speaker, default: default.\n"
        << "  --instruction <utf8>           Natural-language style instruction.\n"
        << "  --sample-rate <hz>             Requested sample rate, default: 24000.\n"
        << "  --channels <count>             Requested channel count, default: 1.\n"
        << "  --request-timeout-ms <ms>      Request timeout, 0 disables it.\n"
        << "  --mock-chunks <count>          Mock worker chunk count, default: 3.\n"
        << "  --mock-chunk-ms <ms>           Mock chunk duration, default: 100.\n"
        << "  --mock-chunk-delay <seconds>   Mock delay between chunks, default: 0.\n";
}

std::string require_value(
    int& index,
    int argc,
    char** argv,
    const std::string& option) {
    const std::string prefix = option + '=';
    const std::string current = argv[index];
    if (current.rfind(prefix, 0) == 0) {
        return current.substr(prefix.size());
    }
    if (index + 1 >= argc) {
        throw std::runtime_error("missing value for " + option);
    }
    ++index;
    return argv[index];
}

std::uint32_t parse_u32(const std::string& value, const std::string& option) {
    std::size_t parsed = 0;
    const unsigned long result = std::stoul(value, &parsed, 10);
    if (parsed != value.size() ||
        result > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("invalid integer for " + option + ": " + value);
    }
    return static_cast<std::uint32_t>(result);
}

int parse_int(const std::string& value, const std::string& option) {
    std::size_t parsed = 0;
    const long result = std::stol(value, &parsed, 10);
    if (parsed != value.size() ||
        result < std::numeric_limits<int>::min() ||
        result > std::numeric_limits<int>::max()) {
        throw std::runtime_error("invalid integer for " + option + ": " + value);
    }
    return static_cast<int>(result);
}

double parse_double(const std::string& value, const std::string& option) {
    std::size_t parsed = 0;
    const double result = std::stod(value, &parsed);
    if (parsed != value.size() || !std::isfinite(result)) {
        throw std::runtime_error("invalid number for " + option + ": " + value);
    }
    return result;
}

ProgramOptions parse_options(int argc, char** argv) {
    ProgramOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];

        if (arg == "--help" || arg == "-h") {
            options.help = true;
        }
        else if (arg == "--mock") {
            options.use_mock_worker = true;
        }
        else if (arg == "--worker" || arg.rfind("--worker=", 0) == 0) {
            options.worker_executable = require_value(index, argc, argv, "--worker");
        }
        else if (arg == "--worker-arg" || arg.rfind("--worker-arg=", 0) == 0) {
            options.worker_arguments.push_back(
                require_value(index, argc, argv, "--worker-arg"));
        }
        else if (arg == "--cwd" || arg.rfind("--cwd=", 0) == 0) {
            options.working_directory = require_value(index, argc, argv, "--cwd");
        }
        else if (arg == "--output" || arg.rfind("--output=", 0) == 0) {
            options.output_path = require_value(index, argc, argv, "--output");
        }
        else if (arg == "--text" || arg.rfind("--text=", 0) == 0) {
            options.text = require_value(index, argc, argv, "--text");
        }
        else if (arg == "--language" || arg.rfind("--language=", 0) == 0) {
            options.language = require_value(index, argc, argv, "--language");
        }
        else if (arg == "--speaker" || arg.rfind("--speaker=", 0) == 0) {
            options.speaker = require_value(index, argc, argv, "--speaker");
        }
        else if (arg == "--instruction" || arg.rfind("--instruction=", 0) == 0) {
            options.instruction = require_value(index, argc, argv, "--instruction");
        }
        else if (arg == "--sample-rate" || arg.rfind("--sample-rate=", 0) == 0) {
            options.sample_rate =
                parse_u32(require_value(index, argc, argv, "--sample-rate"), "--sample-rate");
        }
        else if (arg == "--channels" || arg.rfind("--channels=", 0) == 0) {
            options.channels =
                parse_u32(require_value(index, argc, argv, "--channels"), "--channels");
        }
        else if (arg == "--request-timeout-ms" ||
                 arg.rfind("--request-timeout-ms=", 0) == 0) {
            options.request_timeout = std::chrono::milliseconds(parse_u32(
                require_value(index, argc, argv, "--request-timeout-ms"),
                "--request-timeout-ms"));
        }
        else if (arg == "--mock-chunks" || arg.rfind("--mock-chunks=", 0) == 0) {
            options.mock_chunks =
                parse_int(require_value(index, argc, argv, "--mock-chunks"), "--mock-chunks");
        }
        else if (arg == "--mock-chunk-ms" || arg.rfind("--mock-chunk-ms=", 0) == 0) {
            options.mock_chunk_ms = parse_int(
                require_value(index, argc, argv, "--mock-chunk-ms"),
                "--mock-chunk-ms");
        }
        else if (arg == "--mock-chunk-delay" ||
                 arg.rfind("--mock-chunk-delay=", 0) == 0) {
            options.mock_chunk_delay = parse_double(
                require_value(index, argc, argv, "--mock-chunk-delay"),
                "--mock-chunk-delay");
        }
        else {
            throw std::runtime_error("unknown option: " + arg);
        }
    }

    return options;
}

void validate_options(const ProgramOptions& options) {
    if (options.help) {
        return;
    }
    if (options.output_path.empty()) {
        throw std::runtime_error("--output is required");
    }
    if (options.text.empty()) {
        throw std::runtime_error("--text is required");
    }
    if (!options.use_mock_worker && options.worker_executable.empty()) {
        throw std::runtime_error("--worker is required unless --mock is used");
    }
    if (options.sample_rate == 0) {
        throw std::runtime_error("--sample-rate must be greater than zero");
    }
    if (options.channels == 0 ||
        options.channels > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error("--channels must fit into uint16 and be greater than zero");
    }
    if (options.mock_chunks <= 0) {
        throw std::runtime_error("--mock-chunks must be greater than zero");
    }
    if (options.mock_chunk_ms <= 0) {
        throw std::runtime_error("--mock-chunk-ms must be greater than zero");
    }
    if (options.mock_chunk_delay < 0.0) {
        throw std::runtime_error("--mock-chunk-delay must be non-negative");
    }
}

StdIoTransportOptions make_transport_options(const ProgramOptions& options) {
    StdIoTransportOptions transport_options;
    transport_options.stderr_handler = [](std::string text) {
        std::cerr << text;
    };

    if (options.use_mock_worker) {
        const std::string python_executable = QWEN_TTS_BRIDGE_EXAMPLE_PYTHON_EXECUTABLE;
        const std::string worker_dir = QWEN_TTS_BRIDGE_EXAMPLE_WORKER_DIR;
        if (python_executable.empty() || worker_dir.empty()) {
            throw std::runtime_error(
                "--mock is unavailable because the example was built without Python discovery");
        }

        transport_options.arguments = {
            python_executable,
            "-m",
            "qwen_tts_bridge_worker.main",
            "--mock",
            "--mock-chunks",
            std::to_string(options.mock_chunks),
            "--mock-chunk-ms",
            std::to_string(options.mock_chunk_ms),
            "--mock-chunk-delay",
            std::to_string(options.mock_chunk_delay)
        };
        transport_options.working_directory = worker_dir;
        return transport_options;
    }

    transport_options.arguments.push_back(options.worker_executable);
    transport_options.arguments.insert(
        transport_options.arguments.end(),
        options.worker_arguments.begin(),
        options.worker_arguments.end());
    transport_options.working_directory = options.working_directory;
    return transport_options;
}

AudioFormat requested_audio_format(const ProgramOptions& options) {
    AudioFormat format;
    format.sample_format = "s16le";
    format.sample_rate = options.sample_rate;
    format.channels = options.channels;
    return format;
}

} // namespace

int main(int argc, char** argv) {
    try {
        ProgramOptions options = parse_options(argc, argv);
        validate_options(options);

        if (options.help) {
            print_usage(std::cout, argv[0]);
            return 0;
        }

        const AudioFormat audio_format = requested_audio_format(options);
        WavWriter writer(
            options.output_path,
            audio_format.sample_rate,
            static_cast<std::uint16_t>(audio_format.channels),
            16);

        QwenTtsClientOptions client_options;
        client_options.session.startup_timeout = options.startup_timeout;

        QwenTtsClient client;
        if (!client.start(make_transport_options(options), client_options)) {
            throw std::runtime_error("failed to start Qwen TTS worker");
        }

        SaveWavState state;
        TtsRequest request;
        request.text = options.text;
        request.language = options.language;
        request.speaker = options.speaker;
        request.instruction = options.instruction;
        request.output = audio_format;

        const RequestId request_id = client.synthesize_async(
            std::move(request),
            make_save_wav_callbacks(state, writer, audio_format));
        if (request_id == 0) {
            client.stop();
            throw std::runtime_error("failed to enqueue synthesis request");
        }

        if (!wait_for_save_wav_terminal(state, options.request_timeout)) {
            client.cancel(request_id);
            client.stop();
            throw std::runtime_error("synthesis request timed out");
        }

        client.stop();

        if (!state.success) {
            throw std::runtime_error(state.message);
        }

        std::cout << "Wrote " << state.audio_bytes
                  << " PCM bytes in " << state.audio_chunks
                  << " chunks to " << options.output_path << '\n';
        return 0;
    }
    catch (const std::exception& exc) {
        std::cerr << "qwen_tts_save_wav: " << exc.what() << '\n';
        std::cerr << "Run with --help for usage.\n";
        return 1;
    }
}
