#ifndef GUFO_SERVER_CONTROL_TOKEN_INDEX_HPP_
#define GUFO_SERVER_CONTROL_TOKEN_INDEX_HPP_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gufo::server {

/// The spellings a loaded tokenizer owns as control tokens, held in a trie for
/// a single linear scan of generated text.
///
/// The parser asks whether a pipe-wrapped spelling in model output is genuinely
/// one of these, because the shape of the text cannot say: a model may write a
/// lookalike as argument data, and that lookalike has to survive (#383).
///
/// Only the pipe-wrapped shape is indexed. A vocabulary also lists spellings
/// that are not control tokens in model output — envelope tags such as
/// <tool_call>, and ordinary text — and matching those would discard arguments
/// that legitimately contain them (#383).
///
/// Every indexed spelling starts with '<' and carries none after it, so a walk
/// that fails can only resume at the root: no proper suffix of a partial match
/// can begin a spelling. That is the condition a general multi-pattern matcher
/// needs failure links for, and it is why this walk needs none. The name
/// charset below is what guarantees it, rather than a property assumed of the
/// vocabulary.
class ControlTokensTrie {
public:
  ControlTokensTrie() = default;

  explicit ControlTokensTrie(std::span<const std::string> spellings) {
    for (const std::string& spelling : spellings) {
      if (IsPipeWrappedSpelling(spelling)) {
        Insert(spelling);
        longest_ = std::max(longest_, spelling.size());
      }
    }
  }

  /// Position of the first spelling starting before limit, or npos. A caller
  /// that only cares about a prefix passes limit to bound the scan.
  [[nodiscard]] std::size_t FindFirst(
      std::string_view text, std::size_t limit = std::string_view::npos) const {
    if (nodes_.size() < 2) {
      return std::string_view::npos;
    }
    const std::size_t end = std::min(limit, text.size());
    for (std::size_t start = 0; start < end;) {
      const std::size_t at = text.find('<', start);
      if (at == std::string_view::npos || at >= end) {
        return std::string_view::npos;
      }
      if (Walk(text, at) != std::string_view::npos) {
        return at;
      }
      // No spelling carries '<', so a restart deeper than the root is
      // impossible and the next candidate is the next '<'.
      start = at + 1;
    }
    return std::string_view::npos;
  }

  /// Length of the whole spelling starting at index, or zero. A tag that ends
  /// at a position is admitted only when the vocabulary owns it, so a lookalike
  /// outside the vocabulary is never framing. Walk's counterpart: same walk,
  /// reported as a length instead of a position.
  [[nodiscard]] std::size_t MatchAt(std::string_view text,
                                    std::size_t index) const {
    std::uint32_t node = 0;
    for (std::size_t cursor = index; cursor < text.size(); ++cursor) {
      node = Child(node, text[cursor]);
      if (node == kNone) {
        return 0;
      }
      if (nodes_[node].terminal) {
        return cursor - index + 1;
      }
    }
    return 0;
  }

  /// Length of the longest spelling, or zero when the vocabulary lists none.
  [[nodiscard]] std::size_t LongestSpelling() const { return longest_; }


private:
  /// Characters a token name may contain: '<' and '|' are delimiters, so an
  /// indexed spelling never carries one and a failed walk can restart only at
  /// the root.
  static constexpr bool IsPipeNameChar(char byte) {
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9') || byte == '_' || byte == '.' ||
           byte == '-';
  }

  [[nodiscard]] static bool IsPipeWrappedSpelling(std::string_view spelling) {
    if (spelling.size() < 4 || !spelling.starts_with("<|") ||
        !spelling.ends_with("|>")) {
      return false;
    }
    const std::string_view name = spelling.substr(2, spelling.size() - 4);
    return !name.empty() &&
           std::all_of(name.begin(), name.end(), IsPipeNameChar);
  }

  static constexpr std::uint32_t kNone = static_cast<std::uint32_t>(-1);

  struct Node {
    std::vector<std::pair<char, std::uint32_t>> children;
    bool terminal{false};
  };

  /// Start position when a whole spelling begins at start, else npos.
  [[nodiscard]] std::size_t Walk(std::string_view text,
                                 std::size_t start) const {
    std::uint32_t node = 0;
    for (std::size_t cursor = start; cursor < text.size(); ++cursor) {
      node = Child(node, text[cursor]);
      if (node == kNone) {
        return std::string_view::npos;
      }
      if (nodes_[node].terminal) {
        return start;
      }
    }
    return std::string_view::npos;
  }

  [[nodiscard]] std::uint32_t Child(std::uint32_t node, char byte) const {
    for (const auto& [edge, next] : nodes_[node].children) {
      if (edge == byte) {
        return next;
      }
    }
    return kNone;
  }

  /// Node reached from this one by a byte, adding it when absent.
  std::uint32_t ChildOrInsert(std::uint32_t node, char byte) {
    for (const auto& [edge, next] : nodes_[node].children) {
      if (edge == byte) {
        return next;
      }
    }
    nodes_.push_back(Node{});
    const std::uint32_t next = static_cast<std::uint32_t>(nodes_.size() - 1);
    nodes_[node].children.emplace_back(byte, next);
    return next;
  }

  void Insert(std::string_view spelling) {
    std::uint32_t node = 0;
    for (const char byte : spelling) {
      node = ChildOrInsert(node, byte);
    }
    nodes_[node].terminal = true;
  }

  std::vector<Node> nodes_{Node{}};
  std::size_t longest_{0};
};

}  // namespace gufo::server

#endif  // GUFO_SERVER_CONTROL_TOKEN_INDEX_HPP_
