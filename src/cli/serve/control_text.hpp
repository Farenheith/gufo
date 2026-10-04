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
  // ignored rather than opening a block. One scan per tag keeps the walk
  // linear: the reply can be long, this runs whenever the model asks to stop.
  int depth = 0;
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    const auto tag = text.find('<', cursor);
    if (tag == std::string_view::npos) {
      break;
    }
    const auto rest = text.substr(tag);
    if (rest.starts_with(kThinkStart)) {
      ++depth;
      cursor = tag + kThinkStart.size();
    } else if (rest.starts_with(kThinkEnd)) {
      depth = depth > 0 ? depth - 1 : 0;
      cursor = tag + kThinkEnd.size();
    } else {
      cursor = tag + 1;
    }
  }
  return depth > 0;
}

}  // namespace gufo::server

#endif  // GUFO_SERVER_CONTROL_TEXT_HPP_
