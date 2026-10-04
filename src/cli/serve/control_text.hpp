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
  // The markers alternate, so the last one decides. A closing marker inside a
  // quote still ends the block: `</think>` keeps its framing meaning.
  bool reasoning = false;
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    if (reasoning) {
      const auto close = text.find(kThinkEnd, cursor);
      if (close == std::string_view::npos) {
        break;
      }
      reasoning = false;
      cursor = close + kThinkEnd.size();
    } else {
      const auto open = text.find(kThinkStart, cursor);
      if (open == std::string_view::npos) {
        break;
      }
      reasoning = true;
      cursor = open + kThinkStart.size();
    }
  }
  return reasoning;
}

}  // namespace gufo::server

#endif  // GUFO_SERVER_CONTROL_TEXT_HPP_
