#ifndef GUFO_SERVER_CONTROL_TEXT_HPP_
#define GUFO_SERVER_CONTROL_TEXT_HPP_

#include <cstddef>
#include <string_view>

#include "src/cli/serve/quote_tracker.hpp"

namespace gufo::server {

/// True when a stop token the model emitted at this point is the token spelled
/// as text rather than the end of the turn: the model is writing it as code.
///
/// Both triggers are local by design, so nothing written earlier can decide
/// the question. The token counts as text when a backtick sits immediately
/// before it — the model just opened an inline code span — or when the text
/// ends inside an open fenced code block, markdown's other code form. An
/// unmatched delimiter kilobytes back, an apostrophe, a curly quote and any
/// other language punctuation leave the decision untouched, and a reply that
/// is merely still reasoning no longer reads as quoting on its own, which is
/// what left an ordinary end of a reply unable to end the turn.
///
/// The text handed in is the reply as written up to that point, starting at
/// the assistant framing marker.
[[nodiscard]] inline bool ControlTextIsLiteral(std::string_view text) {
  if (!text.empty() && text.back() == '`') {
    return true;
  }
  QuoteTracker fence;
  fence.Reset(text);
  return fence.FenceOpenAtEnd();
}

}  // namespace gufo::server

#endif  // GUFO_SERVER_CONTROL_TEXT_HPP_
