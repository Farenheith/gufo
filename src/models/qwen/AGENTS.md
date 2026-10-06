# Qwen control tokens

Every Qwen control token is spelled in exactly one file:
[`control_tokens.hpp`](control_tokens.hpp). Code, tests, tools and docs
reference the `gufo::tokenization` names instead of writing the token out.

Why: one spelling means a producer and a consumer cannot disagree, a mistyped
token cannot be invented at a call site, and the raw spellings stay out of an
agent's context — an assistant that only ever reads the names cannot corrupt a
framing marker by copying it.

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

Open `control_tokens.hpp` only to add or change a token.
