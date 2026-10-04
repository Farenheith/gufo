"""Stop tokens the model spells out inside reasoning or a quoted span.

Flash-Next only. The model ends its turn with a control token that the runner
refuses while the reply is inside reasoning or inside an open code span or
fence; those turns commit the token's literal text instead and keep writing.
That text reaches the client only once the reply continues: a reply that asks
to stop again immediately ends with its answer and without the text.
The fixtures below force the model to name that token, so the boundary is
exercised with the turn's real end still ahead of it: a turn cut by the token
stops as soon as it appears and never reaches the DONE sentinel. Both stop
tokens are covered, a fence the model leaves open counts as a quote, and
nothing written for a refused stop survives into the next request.
"""

import sys

END_OF_TURN = ("<|im_end|>", "<|endoftext|>")

REASONING_PROMPT = (
    "Start your private reasoning by quoting the exact text of the control token "
    "that ends your turn, inside backticks, on its own line and nothing else on "
    "that line. Then explain in one sentence what a client that trims it gets "
    "wrong. In the visible answer, write DONE on its own line."
)

QUOTE_PROMPT = (
    "Answer in two lines. Line one: the exact text of the control token that ends "
    "your turn, inside backticks. Line two: DONE."
)

FENCE_PROMPT = (
    "Show the exact text of the control token that ends your turn inside a fenced "
    "code block. After the block, write DONE on its own line."
)

UNRELATED_PROMPT = (
    "Compute 17 * 23. Reason briefly, then write ANSWER: <value> on its own line."
)

END_OF_TEXT_PROMPT = (
    "Answer in two lines. Line one: the exact text of the token that ends a "
    "pretraining document, inside backticks. Line two: DONE."
)

UNCLOSED_FENCE_PROMPT = (
    "Write a fenced code block containing the exact text of the token that ends "
    "your turn. End your reply right after that token, without closing the fence."
)


def named_marker(result):
    combined = result["reasoning"] + result["text"]
    return next((marker for marker in END_OF_TURN if marker in combined), None)


def assert_named_marker(result, where):
    """Guard the fixture: a turn that never spells the token proves nothing.

    A refused token is written out, so its text must be present. Its absence
    means either that the model avoided naming it or that the turn was consumed
    at the token instead of being written, which is the pre-fix behaviour.
    """
    marker = named_marker(result)
    assert marker is not None, (
        f"no control token was written {where}: the fixture did not spell it out, "
        f"or the turn was consumed at the token", result)
    return marker


def assert_not_cut(result, where):
    assert result["finish"] == "stop", result
    assert "DONE" in result["text"], (
        f"turn was cut before its visible answer {where}", result)


def assert_no_trailing_marker(result, where):
    """A refused stop that ended the turn leaves no text of its own behind.

    The reply spells the token out only when the model keeps writing; the reply
    that stops right after asking shows its answer without that last marker.
    A failure means either that the held text reached the client (the hold is
    gone) or that the model wrote the token out itself — check which before
    touching the runner.
    """
    tail = result["text"].rstrip()
    assert not tail.endswith(END_OF_TURN), (
        f"the visible answer ends with control text written for a refused stop {where}",
        result)


def check_control_text(client, model, checks, chat_result):
    """Stop tokens inside reasoning or a quote become text; the turn continues."""

    def run(name, prompt, thinking, streaming=False, previous=None, cache_prompt=False):
        messages = list(previous or []) + [{"role": "user", "content": prompt}]
        request = dict(
            model=model, messages=messages,
            temperature=0, seed=42, max_completion_tokens=1024,
            extra_body={"chat_template_kwargs": {"enable_thinking": thinking},
                        "cache_prompt": cache_prompt},
        )
        result = chat_result(client, request, streaming)
        checks[name] = result
        print(f"CHECK {name}", file=sys.stderr, flush=True)
        return result

    reasoning = run("control_text_reasoning", REASONING_PROMPT, True)
    assert_named_marker(reasoning, "in reasoning")
    assert_not_cut(reasoning, "in reasoning")

    streamed = run("control_text_reasoning_stream", REASONING_PROMPT, True, True)
    assert_named_marker(streamed, "in streamed reasoning")
    assert_not_cut(streamed, "in streamed reasoning")
    assert streamed["text"] == reasoning["text"], (streamed, reasoning)
    # Streamed deltas retain the whitespace that buffered reasoning trims.
    assert streamed["reasoning"].strip() == reasoning["reasoning"].strip(), \
        (streamed, reasoning)

    quote = run("control_text_quote", QUOTE_PROMPT, False)
    assert_named_marker(quote, "in a backtick span")
    assert_not_cut(quote, "in a backtick span")
    assert_no_trailing_marker(quote, "inside the open backtick span")
    assert quote["text"].rstrip().endswith("DONE"), (
        "the answer must end with its last line, not with the refused stop", quote)

    fence = run("control_text_fence", FENCE_PROMPT, False)
    assert_named_marker(fence, "in a fenced block")
    assert_not_cut(fence, "in a fenced block")
    body = fence["text"].split("DONE")[0]
    assert body.count("```") >= 2, ("the answer left its fenced block unclosed", fence)

    # The other stop token is refused and dropped the same way.
    end_of_text = run("control_text_endoftext", END_OF_TEXT_PROMPT, False)
    assert "<|endoftext|>" in end_of_text["reasoning"] + end_of_text["text"], (
        "the fixture did not spell out the pretraining end token", end_of_text)
    assert_not_cut(end_of_text, "naming the pretraining end token")
    assert_no_trailing_marker(end_of_text, "naming the pretraining end token")

    # A turn that ends inside a fence it left open: the ending ask is refused
    # there, so no text of its own may reach the client.
    unclosed = run("control_text_unclosed_fence", UNCLOSED_FENCE_PROMPT, False)
    assert unclosed["finish"] == "stop", unclosed
    assert unclosed["text"].count("```") % 2 == 1, (
        "the fixture must end with its fence still open", unclosed)
    assert_no_trailing_marker(unclosed, "inside the unclosed fence")

    # Nothing written for a refused stop carries into the next request: the
    # follow-up continues the conversation and answers on its own.
    follow_up = run("control_text_follow_up", UNRELATED_PROMPT, False,
                    previous=[{"role": "user", "content": QUOTE_PROMPT},
                              {"role": "assistant", "content": quote["text"]}],
                    cache_prompt=True)
    assert follow_up["finish"] == "stop", follow_up
    assert "391" in follow_up["text"], follow_up
    assert not follow_up["text"].lstrip().startswith(END_OF_TURN), (
        "text held for a refused stop leaked into the next request", follow_up)

    # The reasoning-end marker keeps its framing meaning: a turn that cannot
    # close its reasoning block never reaches its answer.
    unrelated = run("control_text_reasoning_end", UNRELATED_PROMPT, True)
    assert len(unrelated["reasoning"]) > 12, unrelated
    assert "ANSWER" in unrelated["text"] and "391" in unrelated["text"], unrelated
    assert unrelated["finish"] == "stop", unrelated
