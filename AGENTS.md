# Repository Guidelines

## Project Structure & Module Organization
This repository is currently empty and has no established source tree. When adding code, keep a simple, conventional layout so contributors can orient quickly. Suggested defaults:
- `src/` for application or library code.
- `tests/` for automated tests.
- `assets/` for static files (images, data, firmware blobs).
- `docs/` for project documentation.

If you introduce a build system, include a short `README.md` with setup steps and update this guide accordingly.

## Build, Test, and Development Commands
No build or test commands are defined yet. When you add tooling, document the exact commands here. Examples to adopt if relevant:
- `make build` for compiling native code.
- `make test` or `pytest` for running tests.
- `npm run dev` for a local dev server.

## Coding Style & Naming Conventions
No style rules exist yet. When you add code, define the language-specific conventions and formatter/linter tools (for example, `clang-format`, `black`, `ruff`, or `eslint`). Suggested baseline:
- Indentation: 2 spaces for JSON/YAML, 4 spaces for Python/C.
- Naming: `snake_case` for functions in Python/C, `PascalCase` for types, `SCREAMING_SNAKE_CASE` for constants.

## Testing Guidelines
There are no tests or frameworks configured yet. If you introduce tests:
- Place unit tests under `tests/`.
- Use clear naming such as `test_*.py` or `*_test.c` based on the framework.
- Document how to run tests and any coverage expectations.

## Commit & Pull Request Guidelines
No historical commit conventions are available. Use clear, descriptive messages:
- Example: `add vita wifi driver stub` or `fix build warnings on macOS`.

For pull requests:
- Include a concise summary and list of changes.
- Link related issues if they exist.
- Add screenshots or logs when the change affects behavior or output.

## Security & Configuration Tips
If you add configuration or secrets, do not commit private keys or tokens. Prefer `.env` files for local use and document required environment variables in `README.md`.
