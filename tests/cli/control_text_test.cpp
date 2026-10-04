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

void TestReasoningScope() {
  // The template writes the assistant marker and opens the block, so the text
  // handed in starts with it.
  Expect(ControlTextIsLiteral("assistant\n<think>\nconsidering <|im_end|>"),
         "an open reasoning block reads control tokens as text");
  Expect(!ControlTextIsLiteral(
             "assistant\n<think>\nreasoning\n</think>\n\nthe answer"),
         "closed reasoning leaves control tokens as framing");
  Expect(!ControlTextIsLiteral("assistant\n<think>\n\n</think>\n\nthe answer"),
         "no reasoning at all leaves control tokens as framing");
  Expect(ControlTextIsLiteral(
             "assistant\n<think>\n\n</think>\n\nanswer\n<think>\nagain"),
         "a reopened block reads control tokens as text");
}

void TestQuoteScope() {
  const std::string content = "assistant\n<think>\n\n</think>\n\n";
  Expect(ControlTextIsLiteral(content + "see `"),
         "an unfinished inline span reads control tokens as text");
  Expect(ControlTextIsLiteral(content + "see `code"),
         "an inline span with content stays open");
  Expect(!ControlTextIsLiteral(content + "see `<|im_end|>`"),
         "a completed inline span at the end leaves control tokens as framing");
  Expect(!ControlTextIsLiteral(content + "see `<|im_end|>` and"),
         "a completed inline span leaves control tokens as framing");
  Expect(ControlTextIsLiteral(content + "```\nsample\n"),
         "an unfinished fence reads control tokens as text");
  Expect(ControlTextIsLiteral(content + "```"),
         "a fence opener at the end reads control tokens as text");
  Expect(!ControlTextIsLiteral(content + "```\nsample\n```"),
         "a fence closed at the end leaves control tokens as framing");
  Expect(!ControlTextIsLiteral(content + "```\nsample\n```\nafter"),
         "a completed fence leaves control tokens as framing");
  Expect(!ControlTextIsLiteral(content + "plain ~~tiny"),
         "a short tilde run does not open quoting");
}

void TestMarkerInterplay() {
  // `</think>` keeps its framing meaning, so a quoted close tag ends the
  // block and the following text is content.
  Expect(!ControlTextIsLiteral(
             "assistant\n<think>\n```\n</think>\n```\nthe answer"),
         "a closing marker inside a quote still ends the block");
  Expect(ControlTextIsLiteral("assistant\n<think>\nplain reasoning text"),
         "reasoning alone stays literal without markup");
}

void TestByteWiseReading() {
  const std::string text = "assistant\n<think>\nquoted `snippet\n";
  Expect(ControlTextIsLiteral(text), "the chunked text is literal");
  // The decision reads the text once per sampled control token, so no state
  // may carry over between calls.
  Expect(ControlTextIsLiteral(text), "the decision is repeatable");
  Expect(!ControlTextIsLiteral("assistant\n<think>\n\n</think>\n\nplain"),
         "a later call sees only its own text");
}

}  // namespace

int main() {
  TestReasoningScope();
  TestQuoteScope();
  TestMarkerInterplay();
  TestByteWiseReading();
  std::cout << "control_text_test passed\n";
  return 0;
}
