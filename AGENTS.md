# AGENTS.md

driver-canopen-client: A generic, product-agnostic CANopen client: NMT master, SDO client, heartbeat, EMCY, SYNC, PDO
helpers and a CiA 402 drive state machine over a classic CAN frame port. It follows the [Kokeb Labs engineering standard](https://github.com/kokeb-labs/.github/blob/main/standards/README.md)
(agents: `standards/agents.md`, drivers: `standards/driver.md`).

## Layout

- `inc/`, `src/`: the driver. `inc/canopen_client.hpp` is the umbrella header, `src/canopen_client.ipp` the
  implementation,
  `inc/canopen_client_can_interface.hpp` the port, `inc/canopen_client_types.hpp` errors and config,
  `inc/canopen_client_protocol.hpp` the frame codecs, `inc/canopen_client_detail.hpp`
  the compile-time helpers (typed integer literals, exact hook probes, typed register fields, enumeration checks).
- `tests/`: host tests over the register model `tests/mock_canopen_client.hpp`, `tests/compile_fail/`, `tests/docs/`
  (every code block on the site), `tests/install_consumer/`.
- `docs/`: the site pages (`docs/.nav.yml` orders them). `docs/datasheets.yml` records the datasheet the driver
  follows (revision, SHA-256) and every topic it takes from it; `kokeb-docs gen` builds the README card, the
  Datasheet map (`docs/datasheet.md`) and each page's box from it (`tools/docs/generators/datasheet.py`).
- `examples/<platform>/`: the only platform code, one self-contained folder per platform with its own port.
  `examples/host/` (simulated chip, any PC) always exists and runs in the host tests.
  `examples/esp32/`: the ESP-IDF example; the root `CMakeLists.txt` is the ESP-IDF component (`ESP_PLATFORM`).
- `_config/`: all tool configuration (clang-format, clang-tidy, `Doxyfile`, `mkdocs.yml`, `Kconfig`, linters).
  Pass the path (`--style=file:_config/.clang-format`, `--config-file=_config/.clang-tidy`,
  `mkdocs build -f _config/mkdocs.yml`); never add copies or symlinks at the root.
- `cmake/canopen_client_build_settings.cmake`: the only place the version is set; `driver.yml` repeats name and version,
  and CI checks they agree.
- `.kokeb/handbook.yml`: the handbook release this repository follows, its kind and assurance level, and any
  waivers (rule, reason, approver, expiry). CI checks the handbook rules against it and reports each finding by
  rule ID (`KB-CPP-003`, ...); fix the code, or add a dated waiver only with the maintainer's approval.
- The root holds only what GitHub or a tool needs there and what a reader looks for first: `README.md`, `LICENSE`,
  `CHANGELOG.md`, `CMakeLists.txt`, `AGENTS.md`, `CLAUDE.md`, `.gitignore`, `.gitmodules`, `driver.yml`.

## Reading and citing the datasheet

- The chip's datasheet is the only source of the driver's behaviour. `docs/datasheets.yml` names the exact
  document (vendor, number, revision, date, `url`, `pdf`, the `sha256` of the PDF) and lists every topic the
  driver takes from it: the datasheet's section name, the printed page, the driver API that implements it and the
  docs pages that explain it. Link the PDF; keep the file itself out of the repository.
- Cite the section next to the code that depends on it (`// t_RST, section 8.2`), in the Doxygen comment of the
  API, and in the topic row, so code, docs and the Datasheet map agree. Run `kokeb-docs gen` after editing
  `docs/datasheets.yml`.
- Waits use the datasheet maximum, pulses its minimum; `Config{}` holds the reset values; `Validate()` the
  documented ranges.
- Behaviour learned on a bench is written as the driver's design, justified by the datasheet section that
  explains it. Where the datasheet is silent, write the design decision and its reasoning, and give the bench
  step that confirms it (**not verified on hardware** until someone runs it). The repository describes this
  chip only: no other boards, products, projects or drivers, and no dated history (`tools/check_standalone.py`
  checks this).

## Architecture and layer rules

The driver is three layers: the public API (`inc/canopen_client.hpp`, implemented in `src/canopen_client.ipp`), the chip's
definitions and compile-time helpers (`inc/canopen_client_types.hpp`, `inc/canopen_client_detail.hpp`), and the port the
application supplies (`inc/canopen_client_can_interface.hpp`). `docs/architecture.md` draws them.

- A driver is MCU-agnostic and platform-free: no SDK, no RTOS, no lock, no heap, exceptions, RTTI, `std::function`,
  `std::string`, `printf` or static mutable state. It reaches the bus only through the port hooks.
- Everything is in `namespace canopen_client`; internals in `canopen_client::detail`. The class is `canopen_client::Client<Port>`.
- The port base declares no hook; the driver is constrained by the `CanPort` and `CanRxPort` concept, which checks each hook's
  exact signature and `noexcept`. Optional hooks are detected by named `Declares*`/`Has*` concepts in
  `canopen_client::detail` only, never by an inline `requires` expression.
- Code follows the handbook's C++20 profile (KB-CPP20): ISO C++20 with no extensions, nothing from C++23, spans
  for buffers, masked shifts, `uint32_t` bit work, bounded loops, `[[nodiscard]]` error types.
- `Error` keeps the base set at values 0 to 9; chip-specific values start at 16 and each is returned somewhere.
- Every fallible call returns `Result<T>`; every port failure propagates. The constructor stores the configuration
  with no I/O; `EnsureInitialized()` is the only start-up call, and every I/O call returns `NotInitialized` before it
  has succeeded.
- A call sends only the traffic the datasheet needs. Any extra read-back or compare is asked for per call with a
  trailing `Verify verify = Verify::None` argument, its cost in the doc comment (handbook KB-ARCH-041). Start-up
  checks in `EnsureInitialized()` and datasheet-required steps stay unconditional.
- Public and protected methods PascalCase, private methods camelBack, members `snake_case_`, constants
  `kPascalCase`, units as suffixes. 100 columns (`_config/.clang-format`).
- Kconfig values are read only in `inc/canopen_client_kconfig.hpp`.

## Standards

- **Handbook rules** (`kokeb-labs/handbook`, rule IDs such as `KB-CPP-003`): CI checks them against
  `.kokeb/handbook.yml` and reports each finding by rule ID. Fix the code; a waiver (rule, reason, approver,
  expiry) needs the maintainer's approval.
- **C++20 profile** (KB-CPP20-001 to 058): the `cpp20-profile` skill lists which features to use, use with
  rules, or leave out, and runs the automated checks first.
- **MISRA C++:2023 matrix**: the CI job `MISRA` (`.github/workflows/ci-misra.yml`) analyses every translation
  unit, and `tools/preflight.sh --container` runs the same scan locally (step `misra`); the `misra-review` skill
  reviews against the handbook's compliance matrix, lists the patterns the code follows
  (`default: break;` in every enumeration switch, `else` after `else if`, typed constant arguments with
  `_u8`/`_u16`/`_u32`, ...) and cites guideline numbers only.
- **Hardware lessons** (reply validity, stuck and floating lines, resynchronisation, retries, ready waits, bus
  requirements, pin idle levels): the `hardware-lessons-review` skill checks each rule has a code path and a
  model test.

## Build, test and verify

```bash
cmake -S . -B build -DCANOPEN_CLIENT_BUILD_TESTS=ON
cmake --build build --parallel && ctest --test-dir build --output-on-failure
```

The tests build as ISO C++20 (`CMAKE_CXX_EXTENSIONS OFF`, `-pedantic-errors`) with `-fno-exceptions -fno-rtti
-Werror`, and the target `canopen_client_header_checks` compiles each public header alone. CI runs GCC and Clang; run
the compilers you have. The host example runs as the CTest `canopen_client_host_example`. ESP32 builds run in CI
(`esp32-examples-build-ci.yml`) and locally in the container (`tools/preflight.sh --container`, step `esp32`).

## Docs checks

```bash
python3 -m venv .venv && .venv/bin/pip install \
  "git+https://github.com/kokeb-labs/ci-tools@v1#subdirectory=packages/kokeb-mkdocs-theme"
.venv/bin/kokeb-docs figures && .venv/bin/kokeb-docs gen    # after changing headers or snippets
.venv/bin/kokeb-docs check && .venv/bin/mkdocs build -f _config/mkdocs.yml --strict -d /tmp/site
```

Code shown on the site lives in `tests/docs/` between `// --8<-- [start:x]` / `[end:x]` markers. Doxygen runs as
the CTest `canopen_client_doxygen` when `doxygen` is installed; every public symbol in `inc/` is documented.

The `verify` skill (`.claude/skills/verify/SKILL.md`) runs every check CI runs, in order: GCC and Clang host
tests, the strict-dialect header checks, the handbook source checks, `tools/check_standalone.py`, clang-format
20, clang-tidy 20, cppcheck, lizard, markdownlint and the docs build. `tools/preflight.sh` runs all of them in one
command; push only when it ends with `0 failure(s)`, and push the fixes together. `--container` adds the MISRA
scan (`misra`) and the ESP32 builds (`esp32`) as CI runs them: a driver's first push requires both green.

## Branches, commits and pull requests

- Branch from `main` as `feat/`, `fix/`, `docs/`, `ci/` or `chore/<topic>`; one logical change per branch.
- Commit with a sign-off (`git commit -s`) and an imperative subject.
- Open a pull request with the template filled in, including **Bench status**. When every check is green,
  including `CI gate / gate`, the change is merged with a squash merge; the PR title becomes the commit subject
  on `main`. `main` changes only through merged pull requests and keeps its history (no force-push).
- A red check is read before it is re-run. Re-run only an infrastructure failure (a failed install, upload or
  job set-up, a job that never got a runner, a full artifact quota, an API 502), at most twice; a re-run replays the
  `kokeb-labs/ci-tools` version the run started with, so a fix released since needs a new push. The `release`
  skill, section 5, has the list.

## Definition of done

A change is done when it is merged: one change; the standards followed or each deviation named; every check
passes locally (the `verify` skill); host tests for new behaviour including failure paths; docs, Doxygen and
`docs/datasheets.yml` updated with the datasheet sections; `CHANGELOG.md` under `## [Unreleased]`; CI green
including `CI gate / gate` (the handbook check, the Cortex-M build and the coverage, complexity and sanitizer
gates); anything hardware-dependent marked **not verified on hardware** with bench steps; squash-merged.

## Safety rules

- Every commit is signed off (DCO).
- No attribution lines in commits, pull requests or files: no `Co-Authored-By:` trailers, no "Generated with"
  footers, no tool banners.
- No secrets, credentials or vendor PDFs in the repository.
- No force-push to `main` and no history rewrites on it.
- Repository settings (rulesets, merge options, Pages) are the maintainers'; `docs/repo-setup.md` lists them.

## Skills

In `.claude/skills/`:

- `verify`: every check CI runs, before each push.
- `datasheet-conformance`: register maps, timing, reset values and errors from the datasheet, test first.
- `add-driver-feature`, `add-port`: a new driver capability; a new platform under `examples/`.
- `cpp20-profile`, `misra-review`, `hardware-lessons-review`: the reviews of the standards above.
- `release`, `bump-pin`: a version release; a submodule or workflow pin update.
- `new-driver-from-template`: turning a repository made from the template into a real driver.
- `first-push`: the gate before a new driver's first push: container rehearsal, independent review, evidence.

`.claude/agents/safety-reviewer.md` runs the review skills with `datasheet-conformance` and reports findings by
severity for the author to act on.
