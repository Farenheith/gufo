#ifndef GUFO_SERVER_QUOTE_TRACKER_HPP_
#define GUFO_SERVER_QUOTE_TRACKER_HPP_

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace gufo::server {

inline constexpr std::string_view kThinkStart = "<think>";
inline constexpr std::string_view kThinkEnd = "</think>";

/// Request-owned quoting state. Only completed spans quote a marker. During
/// streaming, a marker in an unfinished span remains a possible call; the
/// final parse decides whether a later closing fence made it documentation.
/// Own the scanned bytes rather than identifying a buffer by its address.
class QuoteTracker {
public:
  void Reset(std::string_view text = {}) {
    text_.clear();
    spans_.clear();
    first_nonblank_ = std::string_view::npos;
    run_begin_ = 0;
    run_size_ = 0;
    run_char_ = 0;
    inline_size_ = 0;
    fence_size_ = 0;
    fence_char_ = 0;
    closing_fence_ = false;
    Append(text);
  }

  void Append(std::string_view piece) {
    const auto begin = text_.size();
    text_.append(piece);
    for (auto cursor = begin; cursor < text_.size(); ++cursor) {
      Read(cursor);
      for (const auto phase : {kThinkStart, kThinkEnd}) {
        if (cursor + 1 >= phase.size() &&
            std::string_view(text_).substr(cursor + 1 - phase.size(),
                                           phase.size()) == phase) {
          inline_size_ = fence_size_ = run_size_ = 0;
          fence_char_ = run_char_ = 0;
          closing_fence_ = false;
          first_nonblank_ = std::string_view::npos;
        }
      }
    }
    bytes_scanned_ += piece.size();
  }

  [[nodiscard]] bool QuotedAt(std::size_t position) const {
    const auto next = std::upper_bound(
        spans_.begin(), spans_.end(), position,
        [](auto at, const Span& span) { return at < span.begin; });
    if (next != spans_.begin()) {
      const auto& span = *std::prev(next);
      if (position < span.end)
        return true;
    }
    // A closing delimiter can be the final bytes, without a following newline
    // or ordinary character to flush the pending run. This is provisional:
    // appending more text can make the last line cease to be a closing fence.
    if (fence_size_ > 0) {
      if (closing_fence_ && position >= fence_begin_ && position < close_begin_)
        return true;
      if (run_size_ >= fence_size_ && run_char_ == fence_char_ &&
          AtLineStart(run_begin_) && position >= fence_begin_ &&
          position < run_begin_)
        return true;
    } else if (inline_size_ > 0 && run_char_ == '`' &&
               run_size_ == inline_size_ && position >= inline_begin_ &&
               position < run_begin_) {
      return true;
    }
    return false;
  }

  [[nodiscard]] bool UnclosedAt(std::size_t position) const {
    return !QuotedAt(position) &&
           ((fence_size_ > 0 && position >= fence_begin_) ||
            (inline_size_ > 0 && position >= inline_begin_));
  }

  [[nodiscard]] std::size_t bytes_scanned() const noexcept {
    return bytes_scanned_;
  }

private:
  struct Span {
    std::size_t begin;
    std::size_t end;
  };

  [[nodiscard]] bool AtLineStart(std::size_t position) const {
    return first_nonblank_ == position;
  }

  void FlushRun() {
    if (run_size_ == 0)
      return;
    if (fence_size_ > 0) {
      if (run_char_ == fence_char_ && run_size_ >= fence_size_ &&
          AtLineStart(run_begin_)) {
        closing_fence_ = true;
        close_begin_ = run_begin_;
      }
    } else if (inline_size_ == 0 && run_size_ >= 3 && AtLineStart(run_begin_)) {
      fence_char_ = run_char_;
      fence_size_ = run_size_;
      fence_begin_ = run_begin_ + run_size_;
    } else if (run_char_ == '`') {
      if (inline_size_ == 0) {
        inline_size_ = run_size_;
        inline_begin_ = run_begin_ + run_size_;
      } else if (run_size_ == inline_size_) {
        spans_.push_back({inline_begin_, run_begin_});
        inline_size_ = 0;
      }
    }
    run_size_ = 0;
  }

  void Read(std::size_t cursor) {
    const auto byte = text_[cursor];
    if (first_nonblank_ == std::string_view::npos && byte != ' ' &&
        byte != '\t' && byte != '\r' && byte != '\n') {
      first_nonblank_ = cursor;
    }
    if (byte == '`' || byte == '~') {
      if (run_size_ != 0 && run_char_ != byte)
        FlushRun();
      if (run_size_ == 0) {
        run_begin_ = cursor;
        run_char_ = byte;
      }
      ++run_size_;
      return;
    }
    FlushRun();
    if (byte == '\n') {
      if (fence_size_ > 0 && closing_fence_) {
        spans_.push_back({fence_begin_, close_begin_});
        fence_size_ = 0;
        fence_char_ = 0;
      }
      inline_size_ = 0;
      closing_fence_ = false;
      first_nonblank_ = std::string_view::npos;
    } else if (byte != ' ' && byte != '\t' && byte != '\r') {
      closing_fence_ = false;
    }
  }

  std::string text_;
  std::vector<Span> spans_;
  std::size_t bytes_scanned_{0};
  std::size_t first_nonblank_{std::string_view::npos};
  std::size_t run_begin_{0};
  std::size_t run_size_{0};
  char run_char_{0};
  std::size_t inline_begin_{0};
  std::size_t inline_size_{0};
  std::size_t fence_begin_{0};
  std::size_t fence_size_{0};
  char fence_char_{0};
  std::size_t close_begin_{0};
  bool closing_fence_{false};
};

}  // namespace gufo::server

#endif  // GUFO_SERVER_QUOTE_TRACKER_HPP_
