# Workshop conversation editor design

Proposed replacement for the single tagged-text editor. This is a design, not an implemented interface.

## What the game actually stores

Studied `src/talk.c`, `src/6000.c:ULTIMA_6f1e`, `src/vars.c:D_24ea` / `D_4aa8`, the audited Python editors, Workshop's native codec, and all four original TLK resources (135 conversations: 48 TOWNE, 15 DWELLING, 40 CASTLE, 32 KEEP).

A TLK header contains a count and (dialogue ID, byte offset) pairs. IDs identify conversations, not settlement NPC indices: settlement NPC records link to them. A conversation has:

1. Five positional NUL-terminated entries: name, description, greeting when already introduced, job/work, farewell. These are executable text, not necessarily plain strings. Name/description/greeting can start question branches or execute actions.
2. Ordered keyword/response pairs. A response consisting of `0x87` routes onward to the next pair's response, creating shared responses across aliases. Preserve the pairs and their order; present the common case as keyword chips on one response.
3. Question sections introduced by `0x90` followed by label `0x91..0x9F`: prompt, fallback response, then ordered reply keyword/response pairs. A label byte inside executable text enters that question; it is not an ordinary subroutine call that resumes the remaining text.
4. Structural ending bytes/padding. Every original conversation inspected ends with the conventional `90 9F C0` tail (sometimes followed by NUL padding). Do not show its `@` as a normal question or silently reuse its label 15. Keep the tail separately, and distinguish a genuine label-15 question in unusual/custom input.

Low-byte dictionary tokens expand to words; high-bit character bytes render text. Formatting, actions and operands share this stream. Decode instructions before recognizing structural markers: an operand that equals a label/control byte is not an instruction.

Important semantics:

- Name response receives “My name is …” from the engine. Description receives “You see …”. The editor should explain automatic wrappers rather than encouraging duplicate text.
- Greeting is not an unconditional opening speech. Unintroduced NPCs may randomly announce their name instead; already introduced NPCs use the greeting entry.
- Built-in NAME, JOB/WORK, BYE/THANK are dispatched before ordinary topics; in a question their behavior changes. Unknown ordinary topics receive a hard-coded engine reply, while questions have editable local fallback responses.
- Matching uses the engine's case-folded prefix routine, limited to its short comparison buffer. It is not exact whole-word matching. Player input is limited to 15 characters. The current helper also has a nine-character boundary quirk; simulation must mirror the checked-in engine, and warn about long/empty keywords instead of asserting a universal four-character rule.
- The existing `Set Flag` name for `0x8C` is wrong for the authoring UI. Its operand enters a question (or returns to ordinary topics for `FF`) if the NPC's introduced/name-known bit is set. `Ask Name` can set that bit when the reply matches a party member's name.
- `FE` has two operands: karma threshold and question destination. Expose it as “If karma is at least …, ask …”.
- Gold consumes three encoded digit bytes and attempts payment; insufficient gold returns to ordinary topics. Item/change uses the engine's actual inventory mapping, not an arbitrary item/flag editor.
- Rune toggles character rendering; quote state and automatic word spacing matter to previews.

The original files demonstrate why a text-length or capitalization heuristic is insufficient: Zachariah's `tele` / `star` pairs share one answer, while Malik's *name entry itself* contains conditional routing and Ask Name.

## Main layout

Use Workshop's native desktop styling, splitters and readable text. Keep the retro font for the game preview; it is too restrictive for authoring controls.

```text
Conversations    [Search names, topics and text …]       Undo  Redo
┌──────────────────┬──────────────────────┬───────────────────────────┐
│ NPCs             │ Conversation         │ Topic: avat / retu        │
│ Britain          │ Basics               │ Keywords [avat ×][retu ×] │
│  Zachariah       │   Name               │ [+ Add keyword]           │
│  Malifora        │   Description        │                           │
│  Malik           │   Greeting           │ Response                  │
│ ...              │   Job / Work         │ [Ask: Avatar question ▾]  │
│                  │   Farewell           │                           │
│ ID / locations   │ Topics               │ [+ Text] [+ Action]       │
│ [Open NPC/map]   │   tele / star        │                           │
│                  │   sign / plan        │ [Preview] [References]    │
│                  │   avat / retu ←      │                           │
│                  │ Questions            │ Diagnostics               │
│                  │   Avatar question    │ 617 / 1024 encoded bytes  │
│                  │   Resistance password│                           │
└──────────────────┴──────────────────────┴───────────────────────────┘
```

The NPC pane is the left pane *within* the conversation workspace; collapse Workshop's broader resource library while this workspace is active to avoid four competing navigation columns. Restore it on exit. On narrow windows, collapse NPC navigation to a searchable selector and let preview open in a dock/tab.

NPC rows show a safe name preview, dialogue ID, resource group, changed indicator and diagnostic count. Resolve locations and portraits through existing NPC/map data, including multiple NPC records sharing a dialogue ID. Never derive names by deleting instructions or assuming text before a quote is the complete identity. Unlinked conversations remain visible.

The middle pane is an outline, not a canvas full of arrows. Basics, Topics and Questions are stable categories. Authors can add/duplicate/delete topics, add aliases, and move whole topic groups. Show branch targets and incoming references. A read-only Flow tab can visualize cross-links later; the outline remains the default editing/navigation interface because questions can be shared or cyclic.

## Structured response editing

Represent a response as ordered text and action blocks, retaining their interleaving. Example:

- Text: “We live in the building …”
- Wait for key
- Text: “Go talk to her!”
- End conversation

Normal text displays expanded words without `<the>` tags. Avatar name appears as an inline variable chip; rune spans and explicit line breaks are visible. An action menu provides named forms: pause, wait for key, ask player's name, enter a question, conditional branch on introduction, conditional branch on karma, attempt gold payment, give an inventory item, adjust karma, attempt party joining, call guards, return to ordinary topics, and end conversation.

Action forms use validated amounts, named destinations and the actual inventory mappings. Show side effects and failure behavior next to the relevant action. Preserve rune toggle positioning; do not treat every control as an independent line-ending action.

Give questions project-only descriptive names, e.g. “Resistance password”; keep the numeric label as secondary technical information. The compiler allocates unused labels, honors existing definitions/tail, and updates all operands/references together. Names live in `.imperaproject`, not invented fields in TLK.

For a selected question, show:

```text
Question: Resistance password                  Label 2
Prompt:   What is the password of our Resistance?

Replies (matched in order)
  dawn                  → Text + wait + end conversation
  [+ Add reply]

Otherwise
  I do not yet believe thee! → End conversation

Used by: Avatar question / reply “y” / conditional branch
```

Fallback is always visible. Do not invent a universal “Any” reply or a generic condition language; only expose behaviors the engine supports. Make the ordinary-topic unknown reply visibly engine-controlled.

For Zachariah specifically: `avat` / `retu` opens “Art thou the Avatar of legend?”; `y` contains Ask Name and conditional routing to the password question. Do not simplify it to an unconditional yes→password edge. For Malik, preserve the literal three-digit payment value (004) even though his spoken text says “3 gold coins”; show the discrepancy as a review note, not an automatic correction.

## Preview and testing

A dock has Readable / DOS Preview / Test Conversation tabs.

Readable preview expands words and variable substitutions. DOS preview uses the existing game font, the engine's 18-character text-column wrapping, quotation/rune behavior, pauses and pagination. Mark approximations explicitly until exact rendering is shared or verified; a generic wrapped QLabel is not an accurate DOS preview.

Test Conversation accepts typed input, shows the matched rule and executed actions, and links each transcript line back to its source block. Provide a sandbox starting state: Avatar/party names, introduction bit, karma, gold, party capacity, and deterministic first-encounter name announcement choice. Reset restores that state; it must never mutate the project, game files or saved games. Joining/guard/world effects need reported simulation results rather than claims of full game simulation. Gold/beggar karma behavior depends on more state than gold alone.

Implement a bounded native dialogue interpreter using the same opcode/matching semantics; verify it against small actual-engine harness cases before treating its results as authoritative. Bound instruction steps and label scans, and report loops/missing destinations rather than hanging.

## Safety, editing and storage

Replace explicit Apply prompts on every normal navigation with validated document edits and grouped undo. Keep incomplete drafts visibly invalid; navigation may retain them, but project export must require their resolution. Typing edits are debounced/grouped; actions/topic changes are one undo step. Do not normalize unrelated entries when committing a field.

Preserve original bytes and spans for all untouched entries, tokens, operands, padding, tails and header order. Edited plain text uses deterministic safe compression against the existing dictionary, retaining case and punctuation. Reopening/exporting without edits must be byte-identical. Structured and Advanced views share one model and undo stack.

Advanced tabs show tagged source and byte offsets for the selected section, with whole-conversation source as an explicit advanced option. Unknown/ambiguous structures stay intact as Raw blocks. Structured operations touching them are disabled where boundaries are not proven, with an explanation. Source edits reparse transactionally; never silently discard unknown bytes or malformed content.

Live diagnostics distinguish:

- Errors blocking export: missing/truncated operands, missing branch targets, duplicate definitions, invalid structure, missing fixed entries, invalid TLK IDs/offsets, over-1024-byte conversations, or destinations colliding with a structural tail.
- Warnings: reserved built-in topic prefixes, ambiguous/overlapping keyword matches, long or empty keywords, unreachable branches, terminal actions followed by unreachable text, cycles, and incomplete simulation support. Intentional cycles and ordered-prefix behavior remain permitted.

Warnings link to the exact rule/block. Show encoded bytes—not displayed characters—plus remaining question labels and file-offset budget. Delete a referenced question opens a list of callers and offers retargeting; it must not create dangling jumps. “Repair tail” is a diagnosed, selected-conversation operation with preview and undo, not a blanket normalizing edit.

Export continues to use Workshop's existing `.imperamod` package writer and shared validation. No new engine dialogue format is required.

## Implementation order and acceptance

1. Build lossless instruction/section model with source spans and explicit unknown blocks; parse and no-op round-trip all 135 original conversations. Test alias chains, name-field actions, fallback ordering, operand/control collisions, structural label-15 tails, genuine label-15 definitions and malformed input.
2. Add NPC navigation, outline, basic entries, topic/alias editing and live byte budgets. No-op editing/export must preserve originals.
3. Add ordered action forms, question/reply/fallback editing, reference-safe deletion/renumbering and Advanced view. Undo/redo must restore both behavior and raw bytes.
4. Add bounded simulator and DOS preview, with matching/opcode traces checked against the engine. Use Zachariah and Malik as real regression cases.
5. Add optional flow visualization after the primary editing flow is usable.

The primary editor, ordered actions, reference-safe deletion, Advanced source,
project annotations, bounded sandbox and approximate DOS font preview are now
implemented. Tests preserve all 135 original conversations and compare keyword
matching against the engine's actual matcher. A graphical flow diagram and full
world-effect/recruitment simulation remain future work; the outline is the main
navigation interface.
