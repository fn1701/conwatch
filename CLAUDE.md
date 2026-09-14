# Code Conventions

> **Tooling:** `.clang-format` and `.clang-tidy` are the single source of truth for formatting and
> statically-checkable naming rules. The conventions below cover structure, patterns, and intent that
> tooling cannot enforce.

## Naming

> Casing rules (PascalCase/camelCase) are enforced by `.clang-tidy`. The prefix
> conventions (`m_`) and domain-specific patterns below are not checked by tooling.

- Types/classes/structs/enums: `PascalCase` (`PingMonitor`, `ProtoTrack`, `Severity`, `ProtoState`)
- Enum members: `PascalCase` (`Severity::Blue`, `ProtoState::Healthy`) — not `UPPER_SNAKE_CASE`
- Methods and free functions: `camelCase` (`openSocket4`, `sendPing4`, `severityOf`, `resolveGateway`)
- Local variables, parameters, and struct fields: `camelCase`, no prefix (`failStreak`, `gatewayIp`, `hasLocalAddress`)
- Private/protected class member variables: `m_` prefix (`m_v4`, `m_iface`, `m_tray`) — plain
  structs with no invariants to protect (e.g. `ProtoTrack`) skip the prefix
- `constexpr`/global constants: `PascalCase`, no prefix (`FailStreakForGatewayCheck`, `LocalAddressRecheckTicks`)
- Include guards: `#pragma once` (no manual guard macros)
- `.cpp`/`.hpp` filenames: `snake_case` (`gateway_resolve.cpp`, `target_resolve.hpp`)

## Class Structure

- All helper functions must be **private static member functions** — never anonymous namespace free functions
- Code that operates exclusively on one object should live as a method on that object's class, not as a free function or helper elsewhere
- Default to attaching everything to a class — functions, enums, and constants alike — rather than leaving them free/global. If a set of free functions shares implicit state (parameters threaded through every call), wrap it in a class and make that state members instead
- Free functions/enums are only acceptable with a genuinely good reason (e.g. measurable performance need, or a public API contract that must stay a plain function) — never just convenience. When one is created, add a `//` comment directly on it justifying why it isn't a class member
- Avoid duplicate code — check whether a helper for that purpose already exists before creating one
- Split large classes across multiple `.cpp` files by concern; declare all methods in the single `.h`
- Comment in the `.h` which `.cpp` file implements a method group when the split is non-obvious
- Forward-declare classes in headers instead of including their full headers where possible
- Each class lives in its own `.hpp`/`.cpp` pair. No inline method implementations in the `.hpp`,
  no exceptions (including trivial one-line accessors) — every method body lives in the `.cpp`.
  The `.hpp` holds only the class declaration: method signatures, member variables, and nested
  types/enums that are part of its public interface

## Size Limits

The goal is modular, reusable units — size limits are a signal, not the objective.

- Methods must not exceed **20 lines of code** — split by business logic into multiple well-named methods, or extract named helpers
- Source files (`.cpp`) must not exceed **150 lines** — split by concern into multiple files
- The 150-line guideline extends to `.py`/`.sh` files as far as sensible (test modules, GitLab API scripts) — a signal to split by concern, not a hard gate; data/fixture-heavy scripts may reasonably exceed it

## Comments and Readability (Clean Code)

Code must read like well-written prose (Robert C. Martin, *Clean Code*):

- **Self-documenting code first** — names reveal intent; functions do one thing; no comment should be needed to explain *what* the code does
- **Meaningful names** — prefer a longer descriptive name over a short name plus a comment
- **Small, focused functions** — if a function needs a block comment to explain a section, extract that section into a named function instead
- **`/** */` doc comments are allowed** on public API (classes, public methods) to describe intent, parameters, or non-obvious contracts — not to restate the signature
- **`//` inline comments** only for *why*: hidden constraints, workarounds, non-obvious invariants, trade-off rationale
- `// TODO #<issue> short description` for known open issues — always include the issue number
- One-line comment above a `.cpp` method group is acceptable when it lives in a split file

## Git

- Commit messages may not contain a `Co-Authored-By` line

## Agent Workflow

- Do not start background agents (the `Agent` tool) without explicit user permission or instruction to do so
- Do all work serially in the foreground
