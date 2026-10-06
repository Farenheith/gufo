# Qwen control tokens

Every Qwen control token is spelled in exactly one file:
[`control_tokens.hpp`](control_tokens.hpp). Code, tests, tools and docs
reference the `gufo::tokenization` names instead of writing the token out.

Why: these tokens are special to the inference engine as much as to the model.
An agent that reads a raw spelling in the tree, or writes one into a patch, can
have its own generation cut short — the engine sees the token rather than the
text. Making inference robust wherever a token might surface in an agent's
context (prompts, file contents, tool output, diffs) is an open-ended and likely
impossible mission, and doing it on the inference path carries a real risk to
the product's stability. Isolating the spellings in one small file is the
cheaper, safer lever: the tokens stay out of the code, diffs and context an
agent works with, so a name is all it ever needs — and a name cannot cut
generation short the way the literal can.

## Never read `control_tokens.hpp`

Do not open that file. It is the one place that carries the spellings, and
reading them is exactly what this document exists to prevent — the values would
enter your context and can cut your generation short. The catalogue below is the
reference: pick the constant from here, never from the header.

The single exception is a task whose whole purpose is to add or change a token.

## Rules

- Never write a control-token literal in C++. Include
  `src/models/qwen/control_tokens.hpp` and use the named constant.
- Bring the name into scope (`using gufo::tokenization::kImStart;`) in tests and
  other non-model code, or qualify it (`tokenization::kImStart`) inside a model
  namespace.
- When a token sits next to literal text, append the parts separately, e.g.
  `output.append(kImStart).append("assistant\n")`.
- Do not paste a token spelling into a commit message, pull request, issue or
  any Markdown file.
- Treat the set as closed. If a required token is missing, add it to
  `control_tokens.hpp` rather than inlining the spelling.

## Catalogue

The table names each constant and what it delimits. It deliberately omits the
spellings, which live only in `control_tokens.hpp`.

| Constant | Meaning |
|---|---|
| `kEndOfText` | End-of-text entry; fallback eos and pad when GGUF metadata does not carry them. |
| `kImStart` | Opens a chat turn, emitted immediately before the role name; fallback BOS. |
| `kImEnd` | Closes a chat turn; the primary end-of-generation stop token. |
| `kObjectRefStart`, `kObjectRefEnd` | Delimit an object reference in grounding output. |
| `kBoxStart`, `kBoxEnd` | Delimit a bounding box in grounding output. |
| `kQuadStart`, `kQuadEnd` | Delimit a quadrilateral in grounding output. |
| `kVisionStart`, `kVisionEnd` | Delimit the image-placeholder run inside a user turn. |
| `kVisionPad` | Padding entry in the pinned MiniMax H3 vocabulary. |
| `kImagePad` | One image-placeholder token, repeated once per visual token. |
| `kVideoPad` | One video-placeholder token. |

## Python

`tools/gufo/control_tokens.py` mirrors this catalogue for the Python tools and
tests, imported as `gufo.control_tokens`. The same rule applies — import the
name, never write the literal — and the same prohibition holds: do not open the
C++ header to read values, use this catalogue.

Open `control_tokens.hpp` only when the task is to add or change a token.
