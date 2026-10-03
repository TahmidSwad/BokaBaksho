# Boka_Baksho Documentation

This directory contains the complete documentation for the Boka_Baksho
firmware. Start with the architecture document if you are new to the project;
use the API reference and conventions documents when writing code.

---

## Contents

### Understanding the system

| Document | Read it when |
|---|---|
| [architecture.md](architecture.md) | You need the big picture: layers, data flow, navigation stack, app lifecycle |
| [hardware.md](hardware.md) | You need pin numbers, I2C addresses, font metrics, or the BLE radio setup |
| [ble-protocol.md](ble-protocol.md) | You are writing or debugging the PC-side companion client — a reference implementation ships in `pc_client/` |
| [applications.md](applications.md) | You need to know exactly what each app does, screen by screen |

### Writing code

| Document | Read it when |
|---|---|
| [api-reference.md](api-reference.md) | You need the signature, ownership, or behaviour of a class or type |
| [conventions.md](conventions.md) | You are adding an app or service, or need the coding rules |
| [build-and-test.md](build-and-test.md) | You are compiling, flashing, monitoring, or testing |

### Project status

| Document | Read it when |
|---|---|
| [current-state.md](current-state.md) | You want to know what is implemented, what is partial, and what is broken |
| [roadmap.md](roadmap.md) | You want to know what is planned next |
| [decisions.md](decisions.md) | You want to know *why* the system is built this way |
| [change-log.md](change-log.md) | You want the dated history of changes |

---

## Conventions Used in These Documents

- **Boka_Baksho** is the project name; **`BokaBaksho`** (no underscore) is the
  BLE advertised device name, spelled exactly as it appears in
  `src/services/ble_service.h`.
- Code identifiers are rendered in `monospace`: `AppManager`, `AppId::Lyrics`,
  `DisplayRequestType::ShowSongList`.
- File paths are relative to the repository root, e.g.
  `src/core/app_manager.cpp`.
- Statements about the current implementation are verified against the source
  at the time of writing (2026-10-03) and against a successful `pio run`.

---

## Relationship to `AGENTS.md`

[`AGENTS.md`](../AGENTS.md) at the repository root holds **operating
instructions for AI agents** — how to bootstrap a session, what to do after a
change, and how to report status. It contains no project knowledge and links
here instead of restating it.

If you are an agent: start at this file, then follow the table above. Do not
duplicate documentation content into `AGENTS.md`.
