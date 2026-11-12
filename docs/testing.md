# Testing and CI

How the driver is checked: host tests against a model of the pins, a fuzzer that controls what the driver
reads, compiled documentation, an install test, ESP32 and Cortex-M builds, static analysis and measured quality
gates, on every pull request.

## Host tests

```bash
cmake -S . -B build -DCANOPEN_CLIENT_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

| File | Covers |
| --- | --- |
| [`tests/test_protocol.cpp`](../tests/test_protocol.cpp) | The frame codecs: NMT, SDO, heartbeat, EMCY, SYNC, PDO, statusword, and their rejects |
| [`tests/test_sdo.cpp`](../tests/test_sdo.cpp) | Expedited and segmented transfers, abort codes, replies that lie, timeouts |
| [`tests/test_rx.cpp`](../tests/test_rx.cpp) | NMT, heartbeat, guarding, EMCY, SYNC and TPDO on the receive path |
| [`tests/test_pdo.cpp`](../tests/test_pdo.cpp) | PDO configuration in the profile's order, failure leaves the PDO invalid |
| [`tests/test_drive.cpp`](../tests/test_drive.cpp) | The CiA 402 state machine, safety retries, remapped objects |
| [`tests/test_lifecycle.cpp`](../tests/test_lifecycle.cpp) | `NotInitialized`, `InvalidArgument` sends nothing, the send-only port, every `Error` returned |
| [`tests/test_faults.cpp`](../tests/test_faults.cpp) | A send or receive failure injected at every step of every call |
| [`tests/test_boundaries.cpp`](../tests/test_boundaries.cpp) | Both sides of every numeric limit |
| [`tests/test_hardening.cpp`](../tests/test_hardening.cpp) | The review fixes: raw access, switch-off with a failing receive side, time-bound drive waits, toggle and size checks, node range limits, PDO bounds |
| [`tests/fuzz/`](../tests/fuzz/fuzz_canopen_client.cpp) | The fuzz target; CTest replays its corpus (`canopen_client_fuzz_canopen_client_corpus`), see below |
| [`tests/mock_canopen_client.hpp`](../tests/mock_canopen_client.hpp) | The bus, the node and drive models and the mock ports, written from the protocol and not from the driver's codecs |
| [`tests/compile_fail/`](../tests/compile_fail/not_a_port.cpp) | Ports and calls that must not compile |
| [`tests/check_install.cmake`](../tests/check_install.cmake) | `find_package(canopen_client)` from an installed package |

The tests build with `-fno-exceptions -fno-rtti` and warnings as errors, on GCC and Clang.

## Fuzz tests

Everything the driver reads is what arrives on the bus, so the frames, their timing and the port's failures are what
a hostile or broken node controls. [`tests/fuzz/fuzz_canopen_client.cpp`](../tests/fuzz/fuzz_canopen_client.cpp) lets
libFuzzer choose a configuration, then up to 400 driver calls, and for each frame the port sends or receives whether it
fails, times out, is injected or is answered by the node model. After every call it checks the driver's promises and
aborts, naming the line, when one breaks:

- a failed call returns an `Error` this driver returns (never `Crc`, `Busy` or `DeviceIdMismatch`) and `T{}`;
- before `EnsureInitialized()` every call returns `NotInitialized`, and `InvalidArgument` means no frame was sent;
- an upload never reports more bytes than the caller's buffer holds, and the EMCY queue never holds more than its
  capacity;
- every log line is a non-empty plain-text message;
- `DriveDisable()`, `DriveQuickStop()` and `SendNmt(Stop)` attempt a send once started, even when the port's receive
  side
  fails.

```bash
# libFuzzer needs Clang (on macOS, Homebrew's LLVM)
cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DCANOPEN_CLIENT_BUILD_TESTS=ON -DCANOPEN_CLIENT_BUILD_FUZZERS=ON
cmake --build build-fuzz --target fuzz_canopen_client
./build-fuzz/tests/fuzz/fuzz_canopen_client -max_total_time=120 tests/fuzz/corpus/fuzz_canopen_client
```

The same file also builds without libFuzzer as `fuzz_replay_fuzz_canopen_client`, and CTest replays every input in
[`tests/fuzz/corpus/fuzz_canopen_client/`](../tests/fuzz/corpus/fuzz_canopen_client) on GCC and Clang
(`canopen_client_fuzz_canopen_client_corpus`): the seeds, and every input that ever broke a promise (name it
`regress-*`), so a fixed bug stays fixed. CI fuzzes for 120 s on every pull request, from the seeds plus a corpus it
keeps between runs (job `Host quality / fuzz`).

## Quality gates

The `Host quality` job measures the library code, `inc/` and `src/` only (tests and examples are left out), and
fails when a value is over the limits of this repository's assurance level, `high` (set in
[`.kokeb/handbook.yml`](../.kokeb/handbook.yml), defined by the handbook's ADR-0020):

| Gate | Tool and how it is measured | Limit at `high` |
| --- | --- | --- |
| Complexity | lizard 1.24.1, modified cyclomatic complexity (`-m`: a `switch` counts once) per function | CCN at most 15 |
| Function length | lizard, lines of code per function | at most 60 |
| Parameters | lizard, per function | at most 6 |
| Line coverage | GCC 14 `--coverage -O0`, every CTest, gcovr 8.7 `--merge-lines` (a template's lines count once across its instances), `--exclude-unreachable-branches --exclude-throw-branches` | at least 85 % |
| Branch coverage | same run | at least 75 % |
| MC/DC | GCC `-fcondition-coverage`, same run | measured, not gated at `high` (100 % at `safety`) |
| Sanitizers | Clang, `-fsanitize=address,undefined -fno-sanitize-recover=all`, every CTest | no report |
| Fuzzing | libFuzzer with ASan and UBSan, 120 s per target | no broken promise |

The reports are in the run's `host-quality-reports` artifact and its step summary. To run the gates locally:

```bash
# Coverage (GCC 14 or later), then the complexity limits
cmake -S . -B build-cov -DCMAKE_CXX_COMPILER=g++-14 -DCANOPEN_CLIENT_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="--coverage -O0 -fcondition-coverage"
cmake --build build-cov && ctest --test-dir build-cov
gcovr --root . --filter inc/ --filter src/ --merge-lines --exclude-unreachable-branches --txt build-cov
lizard -m -C 15 -L 60 -a 6 inc src

# Sanitizers (Clang)
cmake -S . -B build-san -DCMAKE_CXX_COMPILER=clang++ -DCANOPEN_CLIENT_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -O1 -g"
cmake --build build-san && ctest --test-dir build-san --output-on-failure
```

The fuzz gate runs locally as shown in [Fuzz tests](#fuzz-tests).

clang-tidy 20 ([`_config/.clang-tidy`](../_config/.clang-tidy): bugprone, CERT, clang-analyzer,
cppcoreguidelines, modernize, performance, portability and readability, each disabled check with its reason) and
cppcheck (strict) run in the `C++ lint` and `Repository checks` jobs and fail on any finding.

The `MISRA` job ([`ci-misra.yml`](../.github/workflows/ci-misra.yml)) runs the shared `ru-misra` workflow of
ci-tools at level `high`: it traces a host build and the Cortex-M build (`canopen_client_optimised_build`) of every
translation unit, checks them against MISRA C++:2023 and the handbook's C++20 profile (the CodeQL coding-standards
and profile packs, plus the validated clang-tidy and cppcheck checks), and reports each finding against the
handbook's compliance matrix and deviation records. A finding is fixed in the code; a deliberate exception names
its deviation on the line before it (`// codeql::misra_deviation_next_line(MD-CPP-009)` above the `.ipp` include
of `inc/canopen_client.hpp`). Until the repository is public the job runs with `open_source_prerelease: true`, the
declaration that the code is released under the licence in `LICENSE` and is to be published.

## CI workflows

Everything runs from [`ci.yml`](../.github/workflows/ci.yml), which calls one workflow file per check and ends in
the `CI gate` job, the single required check (`CI gate / gate`).

| Job | What it checks |
| --- | --- |
| Host tests | GCC, then Clang (one job), build and run every test above, then build and run `examples/host` on its own |
| Host quality | The gates above: complexity, coverage, sanitizers, fuzzing |
| Cross-compile | `arm-none-eabi-g++` for Cortex-M0+, M4F and M7F with the strict flags, warnings as errors (nothing linked) |
| C++ lint | clang-format and clang-tidy over the sources |
| MISRA | MISRA C++:2023 and the C++20 profile on traced host and Cortex-M builds, at level `high` (see [Quality gates](#quality-gates)) |
| Docs | Strict site build (`_config/mkdocs.yml`), generated-region drift, Doxygen (`_config/Doxyfile`), offline link check |
| Repository checks | One job: DCO on pull requests, the org standard, Markdown and YAML lint, the [Kokeb Labs handbook](https://github.com/kokeb-labs/handbook) rules for `.kokeb/handbook.yml` (by rule ID), template drift (warns only) and cppcheck over the driver sources |
| CI gate | Passes only if every job above passed: the one required check |

The **ESP32 examples** job builds every app in `examples/esp32/app_config.yml` with the repository root as the
driver's component. The gate needs it too.
