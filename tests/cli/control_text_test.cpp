#include "src/cli/serve/control_text.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {
using gufo::server::ControlTextIsLiteral;

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

/// Printable form of a case's text: newlines and tabs spelled out, so a
/// failure shows the shape that decided it.
std::string Printable(std::string_view text) {
  std::string out;
  for (const char byte : text) {
    if (byte == '\n')
      out += "\\n";
    else if (byte == '\t')
      out += "\\t";
    else
      out += byte;
  }
  return out;
}

struct Case {
  std::string text;
  bool literal;
  std::string_view why;
};

std::size_t Check(const std::vector<Case>& cases) {
  for (const auto& item : cases) {
    const bool got = ControlTextIsLiteral(item.text);
    if (got != item.literal) {
      std::cerr << "control_text: " << item.why << "\n  text: \""
                << Printable(item.text) << "\"\n  expected literal="
                << (item.literal ? "true" : "false")
                << " got=" << (got ? "true" : "false") << '\n';
      std::exit(1);
    }
  }
  return cases.size();
}

/// The decision reads the reply as written up to the stop token, so every case
/// is the ending a real reply can have. The token counts as text when the
/// model is showing it as code: an inline code span opening right at the token,
/// or a fenced code block still open around it.
std::size_t TestInlineCodeSpan() {
  return Check({
      // A backtick immediately before the token: the model opened a span.
      {"see `", true, "an opening backtick writes the token as code"},
      {"see ``", true, "a longer opening run writes the token as code"},
      {"`", true, "a lone backtick writes the token as code"},
      {"``", true, "a lone opening run writes the token as code"},
      {"one ``two``, then `", true, "the run at the end decides"},
      {"a=\\`", true, "an escaping backslash is not special to the decision"},
      // A delimiter that is not immediately before the token.
      {"see `code", false, "an open span with content does not decide it"},
      {"see `code`", true,
       "a closing backtick reads as an opening one: the decision is local"},
      {"see `code`.", false, "prose after a span leaves the token as framing"},
      {"see `code` and more", false, "trailing prose leaves it as framing"},
      {"a `b` and `c`", true, "the last byte decides, not the span count"},
  });
}

/// A fenced code block is markdown's other code form: a run of three or more
/// backticks or tildes, the first non-blank thing on its line, closed by a run
/// at least as long alone on its own line.
std::size_t TestFencedCodeBlock() {
  return Check({
      {"```\n", true, "an open fence writes the token as code"},
      {"```cpp\n", true, "a fence with an info string is open"},
      {"   ```\n", true, "an indented fence is a fence"},
      {"\t```\n", true, "a tab-indented fence is a fence"},
      {"~~~\n", true, "a tilde fence is a fence"},
      {"~~~~\n", true, "a longer tilde fence is a fence"},
      {"```\ncode\n", true, "text inside the fence is still inside it"},
      {"```\n\ncode\n", true, "a blank line does not close a fence"},
      {"```\ncode\n```\n", false, "a closed fence leaves the token as framing"},
      {"```\ncode\n````\n", false, "a longer closer closes it"},
      {"```\ncode\n   ```\n", false, "an indented closer closes it"},
      {"````\ncode\n```\n", true,
       "a shorter run does not close a longer opener"},
      {"```\ncode\n```text\n", true,
       "a closer with text after it is not a closer"},
      {"```\ncode\n~~~\n", true, "a tilde run does not close a backtick fence"},
      {"```\ncode\n```\n```\n", true,
       "a fence opened after a closed one is open"},
      {"``\n", false, "a two-backtick line is not a fence"},
      {"```", true, "a fence opener at the very end reads as an opening run"},
      {"x ```\n", false, "a run that does not start its line is not a fence"},
      {"~~strike~~", false, "a short tilde run is not a fence"},
      {"a ~ b", false, "a lone tilde is not a fence"},
  });
}

/// Language punctuation decides nothing: the trigger is a backtick, not a
/// quote of any kind, so a reply is never at the mercy of how its language
/// writes apostrophes, dashes or brackets.
std::size_t TestLanguagePunctuation() {
  return Check({
      {"today's logs say", false, "an apostrophe is not a delimiter"},
      {"there's the answer", false,
       "an apostrophe in prose is not a delimiter"},
      {"it's 'quoted' here", false,
       "straight single quotes are not delimiters"},
      {"it's \u201cquoted\u201d here", false,
       "curly double quotes are not delimiters"},
      {"\u2018single\u2019 quotes", false,
       "curly single quotes are not delimiters"},
      {"\u00abguillemets\u00bb", false, "guillemets are not delimiters"},
      {"\u201egerman\u201c quotes", false,
       "low-high quotes are not delimiters"},
      {"\u65e5\u672c\u8a9e\u306e\u300c\u304b\u304e\u62ec\u5f27\u300d", false,
       "corner brackets are not delimiters"},
      {"a \\ backslash", false, "a backslash is not a delimiter"},
      {"an \u2014 em dash", false, "a dash is not a delimiter"},
      {"a \u2026 ellipsis", false, "an ellipsis is not a delimiter"},
      {"semicolons; colons: parens (like this)", false,
       "paired punctuation is not quoting"},
  });
}

/// The failure this rule was written for: a reply that is merely still
/// reasoning, or that ends in plain prose, must end the turn when it asks.
std::size_t TestOrdinaryEndings() {
  return Check({
      {"...and where those costs are coming from.\n\n", false,
       "an ordinary end of a reply ends the turn"},
      {"reasoning\n</parameter>\n", false,
       "a bare closer without code ends the turn"},
      {"answer done\n\n", false, "prose alone ends the turn"},
      {"\n\n", false, "whitespace alone ends the turn"},
      {"", false, "an empty reply ends the turn"},
      {"assistant\n<think>\n", false,
       "an open reasoning block no longer decides on its own"},
      {"assistant\n<think>\nplain reasoning text", false,
       "reasoning prose leaves the token as framing"},
      {"assistant\n<think>\nreasoning\n</think>\n\nthe answer", false,
       "a closed block leaves the token as framing"},
      {"assistant\n<think>\nan example <think> nested </think> more", false,
       "a nested example no longer holds a block open"},
      {"assistant\n<think>\nsee `", true,
       "the backtick inside reasoning decides, not the block"},
      {"assistant\n<think>\n```\n", true,
       "a fence inside reasoning decides, not the block"},
  });
}

/// Only the ending decides: a delimiter opened kilobytes earlier, or one
/// closed far behind, must not reach a decision taken now.
std::size_t TestDistantText() {
  return Check({
      {std::string(4000, 'a') + " `opened long ago and never closed", false,
       "an unmatched delimiter far back does not decide"},
      {std::string(2000, 'x') + "\n```\n" + std::string(1200, 'y') + "\n```\n",
       false, "a fence closed far behind does not decide"},
      {std::string(4000, 'a') + " `", true,
       "the byte before the token still decides in a long reply"},
  });
}

/// The decision is taken once per sampled control token, reading the text it is
/// handed: no state may carry from one call to the next.
void TestRepeatability() {
  const std::string literal = "quoted `";
  Expect(ControlTextIsLiteral(literal), "the chunked text is literal");
  Expect(ControlTextIsLiteral(literal), "the decision is repeatable");
  Expect(!ControlTextIsLiteral("plain prose without code"),
         "a later call does not inherit the earlier decision");
  Expect(ControlTextIsLiteral(literal),
         "and the literal text still decides after a framing call");
}

}  // namespace

int main() {
  std::size_t cases = 0;
  cases += TestInlineCodeSpan();
  cases += TestFencedCodeBlock();
  cases += TestLanguagePunctuation();
  cases += TestOrdinaryEndings();
  cases += TestDistantText();
  TestRepeatability();
  std::cout << "control_text_test passed: " << cases << " cases\n";
  return 0;
}
