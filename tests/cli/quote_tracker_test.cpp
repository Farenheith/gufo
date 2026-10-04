#include "src/cli/serve/quote_tracker.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gufo::server::kThinkEnd;
using gufo::server::kThinkStart;
using gufo::server::QuoteTracker;

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "Assertion failed: " << message << "\n";
    std::exit(1);
  }
}

/// The quoting rule, written out as the reference the tracker is compared
/// against. The test keeps its own reading so a tracker is never its own
/// oracle.
class NaiveQuoting {
public:
  void Push(std::string_view text) {
    for (const char byte : text) {
      Push(byte);
    }
  }

  void Push(char byte) {
    if (byte == '\n') {
      const auto first = line_.find_first_not_of(" \t\r");
      if (first != std::string::npos) {
        const std::string_view trimmed(line_.data() + first,
                                       line_.size() - first);
        if (trimmed.starts_with("```") || trimmed.starts_with("~~~")) {
          fenced_ = !fenced_;
        }
      }
      line_.clear();
      line_backticks_ = 0;
      return;
    }
    if (byte == '`') {
      ++line_backticks_;
    }
    line_.push_back(byte);
  }

  [[nodiscard]] bool in_quote() const noexcept {
    return fenced_ || line_backticks_ % 2 != 0;
  }

private:
  std::string line_;
  std::size_t line_backticks_{0};
  bool fenced_{false};
};

/// The reference the tracker replaced: one scanner, rebuilt per question from
/// the origin. The test keeps its own copy so the tracker is never its own
/// oracle.
[[nodiscard]] bool NaiveQuotedAt(std::string_view text, std::size_t position) {
  std::size_t origin = 0;
  for (const auto phase : {kThinkStart, kThinkEnd}) {
    const auto found = text.rfind(phase, position);
    if (found != std::string_view::npos && found + phase.size() <= position &&
        found + phase.size() > origin) {
      origin = found + phase.size();
    }
  }
  NaiveQuoting scanner;
  scanner.Push(text.substr(origin, position - origin));
  return scanner.in_quote();
}

/// Output shapes the parser has to read the same way through either door: prose
/// that names the framing, fences that document it, and ordinary answers.
const std::vector<std::string>& Corpus() {
  static const std::vector<std::string> corpus = {
      "",
      "\n",
      "plain prose with no quotes at all",
      "an inline `span` and more prose",
      "two `spans` and `another` here",
      "an unmatched `backtick runs to the end",
      "``\ndouble tick fence\n``\n",
      "a fence:\n```\n<invoke name=\"f\">\n```\ndone",
      "unclosed fence:\n```\n<invoke name=\"f\">\n",
      "~~~\nin a tilde fence\n~~~\n out",
      "   ```\n   indented fence\n   ```\n",
      "```json\n{\"a\": 1}\n```\n",
      "prose `a then a fence\n```\nnext line\n```\n",
      "a `b` c ```\nd\ne\n``` f",
      "text with a\nfence ``` opened mid-line\nclosed\n``` here",
      "explain `<|im_end|>` inline",
      "explain:\n```\n<|im_end|>\n```\n",
      "</think>\nafter the delimiter\n```\nstill fenced\n",
      "```\nopen\n</think>\nafterwards\n",
      "reasoning\n</think>\nanswer with `inline` code\n",
      "many >>>>> and < and <| markers <|not_a_vocab|>",
  };
  return corpus;
}

/// Every position of every shape, against the scan-per-call implementation the
/// tracker replaced: the cache must not change a single answer.
void TestMatchesReference() {
  std::size_t probes = 0;
  for (const std::string& text : Corpus()) {
    QuoteTracker tracker;
    for (std::size_t position = 0; position <= text.size(); ++position) {
      ++probes;
      const bool expected = NaiveQuotedAt(text, position);
      const bool actual = tracker.QuotedAt(text, position);
      Expect(actual == expected, "position " + std::to_string(position) +
                                     " of text: " + text + " (expected " +
                                     (expected ? "quoted" : "plain") + ")");
    }
  }
  std::cout << "  matches the per-call scan at " << probes << " positions\n";
}

/// Probes do not arrive in order: the marker search walks backwards and the
/// '<' scan walks forwards. The cache has to answer both orders.
void TestOrderIndependence() {
  std::mt19937 rng(20261003);
  for (const std::string& text : Corpus()) {
    std::vector<bool> sequential(text.size() + 1, false);
    {
      QuoteTracker tracker;
      for (std::size_t position = 0; position <= text.size(); ++position) {
        sequential[position] = tracker.QuotedAt(text, position);
      }
    }
    {
      QuoteTracker tracker;
      for (std::size_t i = text.size() + 1; i-- > 0;) {
        Expect(tracker.QuotedAt(text, i) == sequential[i],
               "backward probe at " + std::to_string(i) + " of text: " + text);
      }
    }
    std::vector<std::size_t> order(text.size() + 1);
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), rng);
    {
      QuoteTracker tracker;
      for (const std::size_t position : order) {
        Expect(tracker.QuotedAt(text, position) == sequential[position],
               "shuffled probe at " + std::to_string(position) +
                   " of text: " + text);
      }
    }
  }
  std::cout << "  same answers forwards, backwards and shuffled\n";
}

/// Callers hand in different views of one response (the raw text, a trimmed
/// one). Positions in another buffer mean something else, so switching buffers
/// must drop the cache rather than answer with the wrong line offsets.
void TestBufferSwitchesKeepAnswers() {
  constexpr std::size_t kShift = 3;
  for (const std::string& text : Corpus()) {
    if (text.size() <= kShift + 2) {
      continue;
    }
    const std::string_view whole(text);
    const std::string_view tail = whole.substr(kShift);
    std::vector<bool> expected_whole(whole.size() + 1, false);
    std::vector<bool> expected_tail(tail.size() + 1, false);
    {
      QuoteTracker reference;
      for (std::size_t p = 0; p <= whole.size(); ++p) {
        expected_whole[p] = reference.QuotedAt(whole, p);
      }
      QuoteTracker reference_tail;
      for (std::size_t p = 0; p <= tail.size(); ++p) {
        expected_tail[p] = reference_tail.QuotedAt(tail, p);
      }
    }
    QuoteTracker shared;
    for (std::size_t p = 0; p <= whole.size(); ++p) {
      Expect(
          shared.QuotedAt(whole, p) == expected_whole[p],
          "whole-buffer probe at " + std::to_string(p) + " of text: " + text);
      if (p <= tail.size()) {
        Expect(
            shared.QuotedAt(tail, p) == expected_tail[p],
            "tail-buffer probe at " + std::to_string(p) + " of text: " + text);
      }
    }
  }
  std::cout << "  interleaved buffers keep their own answers\n";
}

/// Reasoning is discarded at the phase delimiters: a fence the model left open
/// while thinking must not make the answer that follows read as quoted.
void TestPhaseDelimiterResets() {
  const std::string thinking = "```\n<invoke name=\"f\">\n";
  const std::string text = thinking + "</think>\nanswer <invoke name=\"f\">\n";
  QuoteTracker tracker;
  Expect(tracker.QuotedAt(text, thinking.size() - 12),
         "the fence in reasoning quotes what follows it there");
  const std::size_t answer = text.find("<invoke", thinking.size());
  Expect(answer != std::string::npos, "the answer marker is present");
  Expect(!tracker.QuotedAt(text, answer),
         "the answer after the phase delimiter starts unquoted");
  std::cout << "  the phase delimiter resets the reading\n";
}

/// The decode path feeds the tracker one token at a time; the parse path asks
/// it per marker.
/// Both have to read a multi-line fence the same way, or a marker would be
/// framing on one path and prose on the other.
void TestAgreesWithIncrementalReading() {
  const std::string text = "```\n<invoke name=\"f\">\n```\ndone\n";
  const std::size_t marker = text.find("<invoke");
  const std::size_t after = text.find("done");
  {
    NaiveQuoting incremental;
    incremental.Push(std::string_view(text).substr(0, marker));
    Expect(incremental.in_quote(), "the incremental reading opens the fence");
    incremental.Push(std::string_view(text).substr(marker, after - marker));
    Expect(!incremental.in_quote(), "the incremental reading closes the fence");
  }
  {
    QuoteTracker tracker;
    Expect(tracker.QuotedAt(text, marker), "the tracker reads the fence");
    Expect(!tracker.QuotedAt(text, after),
           "the tracker reads the closing fence");
  }
  std::cout << "  agrees with an incremental reading\n";
}

/// One tracker belongs to one response: two of them must not see each other,
/// and Reset() must be enough to hand one to a new response.
void TestIsolation() {
  const std::string fenced = "```\ninside\n";
  const std::string plain = "no quotes here";
  QuoteTracker first;
  QuoteTracker second;
  Expect(first.QuotedAt(fenced, fenced.size() - 1),
         "the first tracker is quoted");
  Expect(!second.QuotedAt(plain, plain.size() - 1),
         "the second tracker is not affected by the first");

  QuoteTracker reused;
  Expect(reused.QuotedAt(fenced, fenced.size() - 1), "quoted before the reset");
  reused.Reset();
  Expect(!reused.QuotedAt(plain, plain.size() - 1),
         "Reset() starts the next response clean");
  std::cout << "  trackers stay independent across responses\n";
}

/// The whole point: probing every '<' in a long line must walk the text once,
/// not once per probe. A regression here is the quadratic this replaced, so the
/// assertion compares against the text length rather than a tuned margin.
void TestLinearity() {
  std::string text;
  constexpr std::size_t kUnits = 4000;
  text.reserve(kUnits * 3);
  for (std::size_t i = 0; i < kUnits; ++i) {
    text += "<a>";
  }
  QuoteTracker tracker;
  std::size_t probes = 0;
  for (std::size_t position = 0; position < text.size(); ++position) {
    if (text[position] == '<') {
      ++probes;
      tracker.QuotedAt(text, position);
    }
  }
  const std::size_t forward = tracker.bytes_scanned();
  // A scan-per-probe implementation would read about probes * half the line.
  const std::size_t quadratic = probes * (text.size() / 2);
  Expect(forward <= 2 * text.size(),
         "forward probing scanned " + std::to_string(forward) + " bytes of a " +
             std::to_string(text.size()) + " byte line");
  for (std::size_t i = text.size(); i-- > 0;) {
    if (text[i] == '<') {
      tracker.QuotedAt(text, i);
    }
  }
  Expect(tracker.bytes_scanned() <= 2 * text.size(),
         "backward probing resumed the scan: " +
             std::to_string(tracker.bytes_scanned()) + " bytes");
  std::cout << "  " << probes << " probes on a " << text.size()
            << " byte line scanned " << tracker.bytes_scanned()
            << " bytes (scan-per-probe would read about " << quadratic << ")\n";
}

}  // namespace

/// Consecutive questions can sit on opposite sides of a phase delimiter, and
/// each side is read from its own origin. Both answers must stay right and the
/// scan must stay linear: a scope change is a lookup, not a rescan.
void TestAlternatingScopesStayLinear() {
  std::string text =
      "reasoning with a \"quote, a `tick` and an open fence:\n```\n";
  text += "</think>";
  const std::size_t answer = text.size();
  constexpr std::size_t kUnits = 4000;
  for (std::size_t i = 0; i < kUnits; ++i) {
    text += "<a>";
  }

  QuoteTracker tracker;
  for (std::size_t step = 0; step < kUnits; ++step) {
    // The three questions one pass asks: a marker inside the answer, a position
    // back in the reasoning phase, and the end-relative look for a held tail.
    const std::size_t inside = answer + step * 3;
    tracker.QuotedAt(text, inside);
    tracker.QuotedAt(text, 5);
    tracker.QuotedAt(text, text.size() - 1);
    if (step % 100 == 0) {
      Expect(tracker.QuotedAt(text, inside) == NaiveQuotedAt(text, inside),
             "a position inside the answer reads the same as the reference");
      Expect(
          tracker.QuotedAt(text, 5) == NaiveQuotedAt(text, 5),
          "a position in the reasoning phase reads the same as the reference");
    }
  }
  Expect(tracker.QuotedAt(text, text.size() - 1) ==
             NaiveQuotedAt(text, text.size() - 1),
         "the end-relative look agrees with the reference");
  std::cout << "  " << kUnits * 3
            << " probes alternating across a delimiter on " << text.size()
            << " bytes scanned " << tracker.bytes_scanned()
            << " bytes (a rescan per question would read about "
            << (kUnits * 3) * text.size() / 4 << ")\n";
  Expect(tracker.bytes_scanned() <= 2 * text.size(),
         std::to_string(text.size()) + " bytes of text were scanned " +
             std::to_string(tracker.bytes_scanned()) +
             " times: alternating scopes must not rescan, or a per-token "
             "question costs the whole response");
}

int main() {
  std::cout << "quote_tracker_test\n";
  TestMatchesReference();
  TestOrderIndependence();
  TestBufferSwitchesKeepAnswers();
  TestPhaseDelimiterResets();
  TestAgreesWithIncrementalReading();
  TestIsolation();
  TestLinearity();
  TestAlternatingScopesStayLinear();
  std::cout << "quote_tracker_test: passed\n";
  return 0;
}
