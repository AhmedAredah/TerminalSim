# Contributing to TerminalSim

Thank you for your interest in contributing to TerminalSim. This document outlines
the process for proposing changes, reporting issues, and submitting pull requests.

## Code of Conduct

This project and everyone participating in it is governed by the
[TerminalSim Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are
expected to uphold this code. Please report unacceptable behavior to
[AhmedAredah@vt.edu](mailto:AhmedAredah@vt.edu).

## Contributor License Agreement (CLA)

TerminalSim requires all contributors to sign a Contributor License Agreement
before any pull request can be merged. The CLA is administered by
[CLA Assistant](https://cla-assistant.io/AhmedAredah/TerminalSim) and the bot
will comment on your first pull request with instructions.

## How to Contribute

### Reporting Bugs

Before opening a bug report, please:

1. Search [existing issues](https://github.com/AhmedAredah/TerminalSim/issues)
   to confirm the bug has not already been reported.
2. Build from the latest `main` to confirm the bug still reproduces.
3. Open a new issue using the **Bug report** template and provide:
   - A minimal reproduction (commands, configuration, input data).
   - Expected vs. actual behavior.
   - Environment details (OS, compiler, Qt version, RabbitMQ version).
   - Relevant logs or stack traces.

### Suggesting Enhancements

Open an issue using the **Feature request** template. Describe the use case,
the proposed behavior, and any alternatives you considered.

### Submitting Pull Requests

1. Fork the repository and create a topic branch from `main`:
   ```
   git checkout -b feature/short-description
   ```
2. Make your changes in focused, logically grouped commits.
3. Ensure the project builds cleanly and all tests pass:
   ```
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j
   ctest --test-dir build --output-on-failure
   ```
4. Update or add tests covering your change.
5. Update documentation when behavior or public APIs change.
6. Open a pull request against `main` and fill in the PR template.

## Coding Standards

- **Language:** C++17. Avoid compiler-specific extensions.
- **Style:** the repository ships a `.clang-format`. Run it before committing:
  ```
  clang-format -i path/to/changed/files
  ```
- **Naming:** match the surrounding code. Prefer descriptive names over
  abbreviations.
- **Headers:** keep includes minimal and ordered (project, third-party, system).
- **Concurrency:** TerminalSim is multi-threaded. Document thread-safety
  expectations on any new public API.
- **Memory & performance:** profile before optimizing. Avoid premature
  micro-optimizations, but do consider algorithmic complexity on hot paths
  (graph operations, pathfinding, simulation loops).

## Commit Messages

Use the [Conventional Commits](https://www.conventionalcommits.org/) style:

```
<type>(<scope>): <short summary>

<body explaining the why, not the what>
```

Common types: `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`,
`ci`, `chore`.

## Review Process

A maintainer will review your PR within a reasonable timeframe. Reviews focus on
correctness, performance, API design, and test coverage. Please respond to
review comments promptly and force-push fixups to keep history readable.

## License

By contributing, you agree that your contributions will be licensed under the
[GNU General Public License v3.0](LICENSE).
