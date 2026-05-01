# Project Overview

## Hard Constraints: Guest Emulation

- TCTI is the required AArch64 guest emulation engine.
- Use TCTI to its fullest for Linux guest execution on iOS under Apple App Store constraints against JIT.
- Do not add, restore, or route guest AArch64 execution through a separate interpreter, fallback interpreter, or ad hoc instruction execution path when that behavior can be implemented in TCTI.
- Missing instruction semantics, fault handling, syscall exits, signals, dispatch, state flush, or guest architectural behavior must be advanced through TCTI and its documented contracts.
- Host-side scaffolding may support loading, scheduling, PTY/session wiring, tracing, and tests, but must not become an alternate guest CPU execution engine.
- Linux mechanisms such as ELF `PT_INTERP`, dynamic linker execution, shebang handling, and shell commands are allowed only as guest code executing through TCTI.
- Terminal/runtime work must preserve guest-backed PTY semantics and must not revert to host-side fake PTY behavior.
- Keep Linux guest startup bounded to one active guest emulator/session at a time.

## Hard Constraints: Project Integration

- `project.yml` is the source of truth for generated Xcode project structure and package integration.
- Use iPhone 17 as the default simulator destination for local verification.
- Use `/Volumes/1TB/Xcode/DerivedData` for Xcode DerivedData and build artifacts.
- Use `/Volumes/1TB/Xcode/Caches` for Xcode-related caches.
- Preserve `libarchive` support for root archive import/export operations.
- Use `libarchive-for-swift` exclusively as an upstream Swift Package dependency. Do not replace it with local vendored copies, wrappers, checkouts, forks, or local edits.
- Use `libghostty-spm` exclusively as an upstream Swift Package dependency. Do not replace it with local vendored copies, wrappers, checkouts, forks, or local edits.

## Tooling Constraints

- If using XcodeBuildMCP, use the installed XcodeBuildMCP skill before calling XcodeBuildMCP tools.
- Use raw `git` for status, commits, and pushes.

## General Guidelines

- Use TypeScript for all new code
- Follow consistent naming conventions
- Write self-documenting code with clear variable and function names
- Prefer composition over inheritance
- Use meaningful comments for complex business logic

## Code Style

- Use 2 spaces for indentation
- Use semicolons
- Use double quotes for strings
- Use trailing commas in multi-line objects and arrays

## Architecture Principles

- Organize code by feature, not by file type
- Keep related files close together
- Use dependency injection for better testability
- Implement proper error handling
- Follow single responsibility principle
