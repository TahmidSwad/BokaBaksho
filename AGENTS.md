# AGENTS.md — Working Instructions for AI Agents

This file contains **operational instructions for agents working on this
repository**. It deliberately holds no project knowledge.

> **Project knowledge lives in [`docs/`](docs/index.md).** Architecture, API,
> hardware, protocol, conventions, and status are documented there and are
> authoritative. Do not restate them here — duplicating them is what caused
> the previous documentation to drift out of date.

---

## 1. Before Writing Code

1. Read [`docs/index.md`](docs/index.md) and open only the documents relevant
   to the current task.
2. Inspect the source files that task touches. Do **not** read the whole
   repository — the documentation is the index into it.
3. Re-read [`docs/conventions.md`](docs/conventions.md) before adding or
   changing a component; it contains the rules that are easy to violate.

---

## 2. Non-Negotiable Rules

These are summaries. The authoritative statement is linked in each line.

| Rule | Authoritative source |
|---|---|
| **Static allocation only** — no `new`, `malloc`, `std::vector`, `std::string`, Arduino `String`. Every buffer is compile-time sized. | [conventions.md §4.1](docs/conventions.md#41-static-allocation-only) |
| **Member storage for queued event payloads** — any pointer posted through the `EventBus` must outlive `Dispatch()`. Never a stack local. | [conventions.md §4.3](docs/conventions.md#43-member-storage-for-queued-event-payloads) |
| **Foreground-only drawing** — only the app at the top of the nav stack may post a `DisplayRequest`, and only with `sender = GetAppId()`. Never draw from `Oled` inside an app. | [conventions.md §4.2](docs/conventions.md#42-single-foreground-owner) |
| **Never block the main loop** — no `delay()` outside a fatal boot halt; waits are `millis()` checks inside `Update()`. | [conventions.md §8](docs/conventions.md#8-error-handling-style) |
| **Bounded structures** — registry 6, nav stack 8, event queue 16, subscribers/type 4, BLE line 256, lyric words 1000, song window 5. Exceeding one is a silent truncation or a dropped event. | [conventions.md §4.1](docs/conventions.md#41-static-allocation-only) |

---

## 3. After Every Change

### 3.1 Build

```sh
pio run
```

A change is not complete until the build succeeds. If PlatformIO is
unavailable in the environment, say so explicitly rather than implying the
change was verified. See [build-and-test.md §2](docs/build-and-test.md#2-build-commands).

### 3.2 Update documentation

Update the affected documents **in the same change**. The mapping from change
type to document is in
[conventions.md §10](docs/conventions.md#10-documentation-maintenance). In
short:

| Changed | Update |
|---|---|
| Module / class / method | `docs/api-reference.md` |
| App behaviour, screens, controls | `docs/applications.md` |
| BLE command | `docs/ble-protocol.md` |
| Pins / peripherals | `docs/hardware.md` |
| Subsystem or data flow | `docs/architecture.md` |
| Constraint or pattern | `docs/conventions.md` |
| Status, bug, limitation | `docs/current-state.md` |
| Deliberate design trade-off | `docs/decisions.md` |
| User-visible change | `docs/change-log.md` (dated entry) |
| Build step | `docs/build-and-test.md` |
| Persistent project-wide rule | `README.md` |

`AGENTS.md` is **not** in this table. Update it only if the working
instructions themselves change.

---

## 4. Reporting

- **Never describe a feature as implemented unless it is implemented and you
  have verified it.** Distinguish explicitly between *verified* (you built it
  or observed it), *partial* (works only under some conditions), and
  *unverified* (you read the code but did not run it).
- If you change behaviour without running the build, say so.
- If the documentation and the code disagree, the code wins — report the
  discrepancy rather than silently trusting either one.
- Known issues and current status are tracked in
  [`docs/current-state.md`](docs/current-state.md); add new findings there
  instead of surfacing them only in chat.

---

## 5. Out of Scope for This File

Do not put the following here — they belong in `docs/`:

- Architecture, data flow, or module descriptions
- Coding conventions, naming rules, or include conventions
- Hardware pin maps, UUIDs, or protocol command lists
- Build instructions, serial debug keys, or troubleshooting
- Feature status, known issues, or the roadmap
- Architectural decision records
