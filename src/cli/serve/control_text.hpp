#ifndef GUFO_SERVER_CONTROL_TEXT_HPP_
#define GUFO_SERVER_CONTROL_TEXT_HPP_

#include <cstddef>
#include <string_view>

#include "src/cli/serve/quote_tracker.hpp"

namespace gufo::server {

/// True when the assistant text ends inside reasoning or an open quote. A stop
/// token the model emits there is the token spelled as text, not the end of the
/// turn: replies that discuss their own markup, or quote it, keep going.
///
/// The text handed in starts at the assistant framing marker, so the template's
/// own `<think>` opening already counts as reasoning.
[[nodiscard]] inline bool ControlTextIsLiteral(std::string_view text) {
  QuoteTracker quotes;
  quotes.Reset(text);
  if (quotes.OpenAtEnd()) {
    return true;
  }
  // Track the block, not a toggle. A reply that spells the tags out — an
  // example inside its own reasoning, a nested block — keeps its outer block
  // open, because the example pairs with itself. A close outside reasoning is
  // ignored rather than opening a block.
  int depth = 0;
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    const auto open = text.find(kThinkStart, cursor);
    const auto close = text.find(kThinkEnd, cursor);
    if (close != std::string_view::npos &&
        (open == std::string_view::npos || close < open)) {
      depth = depth > 0 ? depth - 1 : 0;
      cursor = close + kThinkEnd.size();
      continue;
    }
    if (open == std::string_view::npos) {
      break;
    }
    ++depth;
    cursor = open + kThinkStart.size();
  }
  return depth > 0;
}

}  // namespace gufo::server

#endif  // GUFO_SERVER_CONTROL_TEXT_HPP_
