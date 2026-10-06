"""Qwen control tokens, mirrored from ``src/models/qwen/control_tokens.hpp``.

A control token is spelled in exactly one place per language: the C++ header
``src/models/qwen/control_tokens.hpp`` and this module. Every Python producer or
consumer imports the name instead of writing the literal, so a token has one
spelling and a typo cannot invent a new one.

The rule in ``AGENTS.md`` — never inline a control-token literal — applies to
Python exactly as it does to C++, and for the same reason: these tokens are
special to the inference engine, so a raw spelling in an agent's context can cut
generation short. Keep this file the only Python source that carries the
spellings.

The names deliberately match the C++ catalogue so one name means one token
across the repository.
"""

from __future__ import annotations

# The shared Qwen vocabulary, reused by Qwen3.8, Qwen-Image, Qwen3-TTS,
# Qwen3-ASR and MiniMax H3.
kEndOfText = "<|endoftext|>"
kImStart = "<|im_start|>"
kImEnd = "<|im_end|>"
kObjectRefStart = "<|object_ref_start|>"
kObjectRefEnd = "<|object_ref_end|>"
kBoxStart = "<|box_start|>"
kBoxEnd = "<|box_end|>"
kQuadStart = "<|quad_start|>"
kQuadEnd = "<|quad_end|>"
kVisionStart = "<|vision_start|>"
kVisionEnd = "<|vision_end|>"
kVisionPad = "<|vision_pad|>"
kImagePad = "<|image_pad|>"
kVideoPad = "<|video_pad|>"

# MiniMax H3 audio/video additions on top of the shared vocabulary, used by the
# H3 source-manifest and quality checks. They are not declared in the C++ header
# because no C++ translation unit consumes them.
kLyricsStart = "<|lyrics_start|>"
kLyricsEnd = "<|lyrics_end|>"
kCaptionStart = "<|caption_start|>"
kCaptionEnd = "<|caption_end|>"

# The special tokens the MiniMax H3 tokenizer config must declare, in the order
# the pinned manifest lists them.
H3_SPECIAL_TOKENS = (
    kImStart,
    kImEnd,
    kVisionStart,
    kVisionEnd,
    kImagePad,
    kVideoPad,
    kLyricsStart,
    kLyricsEnd,
    kCaptionStart,
    kCaptionEnd,
)
