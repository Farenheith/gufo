#ifndef GUFO_TOKENIZATION_QWEN_CONTROL_TOKENS_HPP_
#define GUFO_TOKENIZATION_QWEN_CONTROL_TOKENS_HPP_

#include <string_view>

namespace gufo::tokenization {

/// Qwen's `<|...|>` control tokens, declared once so every producer and
/// consumer of the framing vocabulary references a name instead of spelling
/// the literal. Keep this the only header that carries the token spellings:
/// the chat template, the tokenizers, the servers and the tests all read them
/// here, so a typo in one call site can no longer invent a token.
///
/// The `<|...|>` family is the shared Qwen vocabulary, reused by Qwen3.8,
/// Qwen-Image, Qwen3-TTS, Qwen3-ASR and MiniMax H3. Non-`<|...|>` framing such
/// as `<tool_call>`, `<think>` and the DeepSeek `<｜DSML｜...>` markers is not
/// part of this table.
inline constexpr std::string_view kEndOfText = "<|endoftext|>";
inline constexpr std::string_view kImStart = "<|im_start|>";
inline constexpr std::string_view kImEnd = "<|im_end|>";
inline constexpr std::string_view kObjectRefStart = "<|object_ref_start|>";
inline constexpr std::string_view kObjectRefEnd = "<|object_ref_end|>";
inline constexpr std::string_view kBoxStart = "<|box_start|>";
inline constexpr std::string_view kBoxEnd = "<|box_end|>";
inline constexpr std::string_view kQuadStart = "<|quad_start|>";
inline constexpr std::string_view kQuadEnd = "<|quad_end|>";
inline constexpr std::string_view kVisionStart = "<|vision_start|>";
inline constexpr std::string_view kVisionEnd = "<|vision_end|>";
inline constexpr std::string_view kVisionPad = "<|vision_pad|>";
inline constexpr std::string_view kImagePad = "<|image_pad|>";
inline constexpr std::string_view kVideoPad = "<|video_pad|>";

}  // namespace gufo::tokenization

#endif  // GUFO_TOKENIZATION_QWEN_CONTROL_TOKENS_HPP_
