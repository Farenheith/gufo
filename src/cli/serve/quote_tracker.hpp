#ifndef GUFO_SERVER_QUOTE_TRACKER_HPP_
#define GUFO_SERVER_QUOTE_TRACKER_HPP_

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string_view>
#include <vector>

namespace gufo::server {

/// The server's own phase delimiters. Whatever the model wrote while reasoning,
/// the answer after `</think>` starts on a fresh line with no fence left open
/// behind it.
inline constexpr std::string_view kThinkStart = "<think>";
inline constexpr std::string_view kThinkEnd = "</think>";

/// Quotes in a response, answered without rescanning the line.
///
/// The parse-time checks ask "is this position quoted?" at positions that walk
/// backwards and at every '<' in the buffer. Answering each one by scanning
/// from the origin costs O(origin to position) per probe, which makes a long
/// answer dense in brackets quadratic, so the answer is cached instead:
/// toggles_ records where the reading flips as the text is walked once, and a
/// query becomes a binary search over it. scanned_to_ lets the same walk
/// continue when the text grows, so a buffer is walked once however many probes
/// land in it.
///
/// One pass does three jobs, because they are one pass over the output: it
/// reads the quoting rule, it records where the reading flips, and it records
/// the phase delimiters it crosses. Reading in slices is safe because the pass
/// only ever extends, so any split of the same text leaves the same answers,
/// which is what lets the decode path feed it per token and the parse path ask
/// it per marker. The origin is not an optimisation of this cache but the rule
/// it reproduces: leading reasoning is discarded at the phase delimiters, so a
/// query never reads past the delimiter that precedes it, and a fence opened
/// anywhere after that origin still counts.
///
/// The cache belongs to one response and to the request that owns it: never
/// share an instance across requests, and Reset() it when a response reuses
/// one.
class QuoteTracker {
public:
  /// Whether the position sits inside a quoted span, backticks or a fence.
  ///
  /// The answer is counted from the phase delimiter that governs the position,
  /// not from the response, so a question never costs more than a lookup once
  /// the text has been read. Callers may ask about positions in any order: a
  /// question about text the tracker already passed is answered from the
  /// record.
  [[nodiscard]] bool QuotedAt(std::string_view text, std::size_t position) {
    // The cache belongs to one buffer: callers hand in different views of the
    // response (the raw text, a trimmed one, a pending slice) whose positions
    // mean different things, so a buffer the tracker has not seen before drops
    // the cache rather than answering with another buffer's positions.
    if (text.data() != text_data_ || position > text.size()) {
      Reset();
      text_data_ = text.data();
    }
    if (position > scanned_to_) {
      Push(text, position);
    }
    const auto scope = std::prev(std::upper_bound(
        scopes_.begin(), scopes_.end(), position,
        [](std::size_t lhs, const Scope& rhs) { return lhs < rhs.from; }));
    const auto first_after =
        std::upper_bound(toggles_.begin(), toggles_.end(), position);
    const auto flips = std::distance(toggles_.begin(), first_after) -
                       static_cast<std::ptrdiff_t>(scope->flips_before);
    return flips % 2 == 1;
  }

  /// Drops the cache and starts again, for a new response.
  void Reset() {
    text_data_ = nullptr;
    scanned_to_ = 0;
    toggles_.clear();
    scopes_.assign(1, Scope{0, 0});
    line_start_ = 0;
    line_backticks_ = 0;
    fenced_ = false;
  }

  /// Bytes read since construction.
  ///
  /// Diagnostic, not state: a query pattern that restarts the reading, or
  /// thrashes between two buffers, shows up here as work growing faster than
  /// the text, which is what the linearity test asserts.
  [[nodiscard]] std::size_t bytes_scanned() const noexcept {
    return bytes_scanned_;
  }

private:
  /// Where a marker sits decides what it is: a tag inside quoted output is the
  /// model *naming* the framing, not writing it (#383). The phase delimiters
  /// reset the reading, so quoting is counted from the delimiter that governs
  /// the position. One scope per delimiter, recorded as the pass crosses it.
  struct Scope {
    std::size_t from{0};
    std::size_t flips_before{0};
  };

  /// Consumes (scanned_to_, position] one byte at a time, recording the flips
  /// and the phase delimiters the pass crosses. The pass only ever extends, so
  /// bytes are read once per response however many questions are asked.
  void Push(std::string_view text, std::size_t position) {
    const std::size_t from = scanned_to_;
    for (std::size_t cursor = from; cursor < position && cursor < text.size();
         ++cursor) {
      const bool before = Quoted();
      Read(text, cursor);
      if (Quoted() != before) {
        toggles_.push_back(cursor + 1);
      }
      // A delimiter ends its scope: the bytes after it are read as a reading
      // that started at that origin would read them, and the flips before it
      // stop counting for the positions that follow.
      for (const auto phase : {kThinkStart, kThinkEnd}) {
        if (cursor + 1 >= phase.size() &&
            text.substr(cursor + 1 - phase.size(), phase.size()) == phase) {
          scopes_.push_back(Scope{cursor + 1, toggles_.size()});
          line_start_ = cursor + 1;
          line_backticks_ = 0;
          fenced_ = false;
        }
      }
    }
    scanned_to_ = position;
    bytes_scanned_ += scanned_to_ - from;
  }

  /// Reads one byte of output: a completed line whose first non-blank bytes are
  /// three backticks or tildes toggles a fence, and each backtick opens or
  /// closes an inline span on the current line. The line is read where the text
  /// already is, so the tracker keeps no copy of the output.
  void Read(std::string_view text, std::size_t cursor) {
    const char byte = text[cursor];
    if (byte == '\n') {
      const auto line = text.substr(line_start_, cursor - line_start_);
      const auto first = line.find_first_not_of(" \t\r");
      if (first != std::string_view::npos) {
        const auto trimmed = line.substr(first);
        if (trimmed.starts_with("```") || trimmed.starts_with("~~~")) {
          fenced_ = !fenced_;
        }
      }
      line_start_ = cursor + 1;
      line_backticks_ = 0;
      return;
    }
    if (byte == '`') {
      ++line_backticks_;
    }
  }

  /// True when the last byte read sits inside a quoted region.
  [[nodiscard]] bool Quoted() const noexcept {
    return fenced_ || line_backticks_ % 2 != 0;
  }

  const char* text_data_{nullptr};
  std::size_t scanned_to_{0};
  std::size_t bytes_scanned_{0};
  std::size_t line_start_{0};
  std::size_t line_backticks_{0};
  bool fenced_{false};
  std::vector<std::size_t> toggles_{};
  std::vector<Scope> scopes_{{Scope{0, 0}}};
};

}  // namespace gufo::server

#endif  // GUFO_SERVER_QUOTE_TRACKER_HPP_
