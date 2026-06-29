#include <qwen_tts_bridge/protocol/control/ControlCodecInternal.hpp>

#include <type_traits>
#include <variant>

namespace qwen_tts_bridge::control_detail {

Json control_message_to_json(const ControlMessage& message) {
    return std::visit(
        [](const auto& value) -> Json {
            using Message = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<Message, HelloMessage>) {
                return Json{
                    {kMessageType, "hello"},
                    {"client_name", value.client_name},
                    {"client_version", value.client_version}
                };
            }
            else if constexpr (std::is_same_v<Message, SynthesizeMessage>) {
                return Json{
                    {kMessageType, "synthesize"},
                    {"text", value.text},
                    {"language", value.language},
                    {"speaker", value.speaker},
                    {"instruction", value.instruction},
                    {"output", audio_format_to_json(value.output)}
                };
            }
            else if constexpr (std::is_same_v<Message, CancelMessage>) {
                return Json{{kMessageType, "cancel"}};
            }
            else if constexpr (std::is_same_v<Message, PingMessage>) {
                Json out = {{kMessageType, "ping"}};
                if (value.has_sequence) {
                    out["sequence"] = value.sequence;
                }
                return out;
            }
            else if constexpr (std::is_same_v<Message, ShutdownMessage>) {
                return Json{
                    {kMessageType, "shutdown"},
                    {"mode", value.mode}
                };
            }
            else if constexpr (std::is_same_v<Message, ReadyMessage>) {
                Json out = {
                    {kMessageType, "ready"},
                    {"worker_version", value.worker_version},
                    {"session_id", value.session_id},
                    {"capabilities", capabilities_to_json(value.capabilities)}
                };
                if (value.has_warmed_up) {
                    out["warmed_up"] = value.warmed_up;
                }
                return out;
            }
            else if constexpr (std::is_same_v<Message, QueuedMessage>) {
                Json out = {{kMessageType, "queued"}};
                if (value.has_position) {
                    out["position"] = value.position;
                }
                return out;
            }
            else if constexpr (std::is_same_v<Message, StartedMessage>) {
                return Json{
                    {kMessageType, "started"},
                    {"audio_format", audio_format_to_json(value.audio_format)}
                };
            }
            else if constexpr (std::is_same_v<Message, CompletedMessage>) {
                return Json{{kMessageType, "completed"}};
            }
            else if constexpr (std::is_same_v<Message, CancelledMessage>) {
                return Json{{kMessageType, "cancelled"}};
            }
            else if constexpr (std::is_same_v<Message, PongMessage>) {
                Json out = {{kMessageType, "pong"}};
                if (value.has_sequence) {
                    out["sequence"] = value.sequence;
                }
                return out;
            }
            else {
                return Json{{kMessageType, "shutdown_ack"}};
            }
        },
        message);
}

} // namespace qwen_tts_bridge::control_detail
