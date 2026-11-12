#!/usr/bin/env bash
# The MISRA scan of CI (kokeb-labs/ci-tools ru-misra.yml) on this checkout, inside the MISRA image
# (tools/ci-container/Dockerfile.misra). It reads the inputs from the caller workflow of this repository
# (.github/workflows/ci-misra.yml), then runs what the workflow runs, with the same scripts of the pinned ci-tools
# commit: the licence check, the clang-tidy configurations, one scan per configuration (host, then Cortex-M unless
# the caller turns it off: traced build, CodeQL database, the coding-standards pack and the profile pack, the
# deviation records) and the report. The pass rule is the report's: it exits 1 when a finding of a mandatory or
# required guideline that no record deviates, a scan problem or a suppression error exists. Advisory findings are
# warnings and do not fail, as in CI.
#
#   tools/ci-container/misra.sh [--host-only] [DIR]    DIR: the checkout (default: the current directory)
#
# --host-only skips the Cortex-M configuration (about half the time; it does not cover the preprocessor paths of the
# cross build). Output: each blocking finding as "file:line failure Rule message", then the verdict. Everything the
# scan wrote (SARIF, JSON, CSV, the full log, timings) is in $PREFLIGHT_OUT/misra (default /tmp/misra-out).
set -uo pipefail
host_only=0; [ "${1:-}" = "--host-only" ] && { host_only=1; shift; }
cd "${1:-.}" || exit 2
root="$(pwd)"
RU="${RU_MISRA:-/opt/ru-misra}"
: "${CODEQL:?run this in the MISRA image}" "${MISRA_PACK:?}" "${CS_SCRIPT:?}" "${ARM_BIN:?}"
out="${PREFLIGHT_OUT:-/tmp/misra-out}/misra"; rm -rf "$out"; mkdir -p "$out"
log="$out/misra.log"; : >"$log"
py="$RU/venv/bin/python"
caller=.github/workflows/ci-misra.yml
[ -f "$caller" ] || { echo "no $caller: nothing to scan"; exit 2; }

# The caller's inputs of ru-misra.yml, with the workflow's defaults.
eval "$("$py" - "$caller" <<'EOF'
import shlex, sys, yaml
wf = yaml.safe_load(open(sys.argv[1], encoding="utf-8"))
call = next((j for j in wf["jobs"].values() if "ru-misra.yml" in str(j.get("uses", ""))), None)
if call is None:
    sys.exit("echo 'no job of " + sys.argv[1] + " calls ru-misra.yml'; exit 2")
d = {"level": "high", "paths": "inc src", "cmake_source_dir": ".", "cmake_args": "", "build_target": "",
     "cross": True, "cross_cmake_args": "", "cross_build_target": "", "profile_pack": True,
     "repo_deviations": "docs/deviations.yml", "handbook_ref": "db2cdd0f75cdf998797d535565966b60e97f8c8d",
     "advanced_security": False, "open_source_prerelease": False, "enforce": True}
d.update(call.get("with") or {})
for k, v in d.items():
    print(f"in_{k}={shlex.quote(str(v).lower() if isinstance(v, bool) else str(v))}")
print(f"in_uses={shlex.quote(str(call['uses']))}")
EOF
)" || exit 2
echo "caller: $in_uses level=$in_level paths='$in_paths' cmake_args='$in_cmake_args'" | tee -a "$log"
case "$in_uses" in *@v1) ;; *) echo "note: the image carries the ci-tools commit that '@v1' pointed to; $in_uses may differ" ;; esac

# The workflow's checkouts: the scripts at the pinned commit and the handbook's compliance folder at handbook_ref.
rm -rf .ru-misra; mkdir -p .ru-misra/ci-tools .ru-misra/handbook .ru-misra/out
for d in tools/misra codeql tests/misra-validation; do mkdir -p ".ru-misra/ci-tools/$(dirname "$d")"; cp -a "$RU/ci-tools/$d" ".ru-misra/ci-tools/$d"; done
if [ "$in_handbook_ref" = "$(cat "$RU/handbook.ref" 2>/dev/null)" ]; then
  cp -a "$RU/handbook/compliance" .ru-misra/handbook/compliance
else
  echo "the caller pins handbook $in_handbook_ref, not the image's: fetching it" | tee -a "$log"
  git init -q .ru-misra/hb && git -C .ru-misra/hb fetch -q --depth 1 https://github.com/kokeb-labs/handbook "$in_handbook_ref" &&
    git -C .ru-misra/hb checkout -q FETCH_HEAD && cp -a .ru-misra/hb/compliance .ru-misra/handbook/compliance || exit 2
fi
tools=.ru-misra/ci-tools/tools/misra
t0=$SECONDS; verdict=0
lap() { echo "$1: $((SECONDS - t0))s" | tee -a "$out/timing.txt"; t0=$SECONDS; }

# Job "plan": may CodeQL run on this repository, and which configurations.
"$py" "$tools/misra.py" licence --private true --advanced-security "$in_advanced_security" \
  --open-source-prerelease "$in_open_source_prerelease" --repo . >>"$log" 2>&1 || { echo "licence check failed (see $log)"; grep '^::error' "$log"; exit 1; }
configs=(host); [ "$in_cross" = "true" ] && [ "$host_only" = 0 ] && configs+=(cortex-m)
[ "$in_cross" = "true" ] && [ "$host_only" = 1 ] && echo "NOT COVERED (--host-only): the Cortex-M configuration CI also scans"

# Job "scan", once per configuration.
"$py" "$tools/install.py" versions --out .ru-misra/out/versions.json --codeql "$CODEQL" \
  --clang-tidy "$RU/venv/bin/clang-tidy" --arm-bin "$ARM_BIN" >>"$log" 2>&1
"$py" "$tools/misra.py" tidy-configs --cases .ru-misra/ci-tools/tests/misra-validation/cases \
  --out .ru-misra/out/tidy-configs.json >>"$log" 2>&1 || { echo "tidy-configs failed"; exit 1; }
own=(); [ "$in_profile_pack" = "true" ] && own=(--own-pack .ru-misra/ci-tools/codeql/kokeb-cpp20)
repo_records=(); [ -f "$in_repo_deviations" ] && repo_records=(--repo-records "$in_repo_deviations")
lap "setup"
for c in "${configs[@]}"; do
  cm="$in_cmake_args"; bt="$in_build_target"
  if [ "$c" = cortex-m ]; then [ -n "$in_cross_cmake_args" ] && cm="$in_cross_cmake_args"; bt="$in_cross_build_target"; fi
  echo "scan $c ..."
  "$py" "$tools/scan.py" --config "$c" --out ".ru-misra/out/$c" --codeql "$CODEQL" --misra-pack "$MISRA_PACK" \
    --cs-script "$CS_SCRIPT" --records .ru-misra/handbook/compliance/misra-cpp-2023-deviations.yml "${repo_records[@]}" \
    --matrix .ru-misra/handbook/compliance/misra-cpp-2023.yml --source-dir "$in_cmake_source_dir" \
    --cmake-args="$cm" --build-target "$bt" --paths="$in_paths" "${own[@]}" \
    --clang-tidy "$RU/venv/bin/clang-tidy" --tidy-configs .ru-misra/out/tidy-configs.json --arm-bin "${ARM_BIN:-}" \
    >"$out/scan-$c.log" 2>&1
  rc=$?
  cp .ru-misra/out/versions.json ".ru-misra/out/$c/" 2>/dev/null
  if [ "$rc" != 0 ]; then
    verdict=1; echo "scan $c FAILED (exit $rc); log $out/scan-$c.log"
    grep -E '^::error|Error|error:' "$out/scan-$c.log" | head -20 | sed 's/^/     /'
  fi
  lap "scan $c"
done

# Job "report": the same arguments, the same level policy.
args=(); for c in "${configs[@]}"; do args+=(--config "$c=.ru-misra/out/$c"); done
expect="cpp/misra/unused-parameter-misra-cpp,cpp/misra/loop-body-compound-condition,cpp/misra/global-namespace-declarations"
[ "$in_profile_pack" = "true" ] && expect="$expect,kokeb/cpp20/result-type-not-nodiscard"
if [ ! -f .ru-misra/out/host/markers.json ]; then
  echo "the host scan produced no results"; verdict=1
else
  mkdir -p .ru-misra/report
  "$py" "$tools/misra.py" report --matrix .ru-misra/handbook/compliance/misra-cpp-2023.yml \
    --supported .ru-misra/ci-tools/tests/misra-validation/codeql-supported.json \
    --anomalies .ru-misra/ci-tools/tests/misra-validation/anomalies.yml \
    --markers .ru-misra/out/host/markers.json "${args[@]}" --paths "$in_paths" --level "$in_level" \
    --canary-expect "$expect" --enforce "$in_enforce" --out-dir .ru-misra/report >"$out/report.log" 2>&1
  [ $? = 0 ] || verdict=1
  lap "report"
  # Annotations become lines: "failure" (blocks) and "warning" (listed in the file, not here), as the CI check lists them.
  "$py" - "$out/report.log" "$out/findings.txt" "$out/blocking.txt" <<'EOF'
import re, sys
rx = re.compile(r"^::(error|warning)(?: file=([^,:]*)(?:,line=(\d+))?)?::(.*)$")
allf, block = [], []
for ln in open(sys.argv[1], encoding="utf-8", errors="replace"):
    m = rx.match(ln.rstrip("\n"))
    if not m:
        continue
    kind, f, line, msg = m.groups()
    loc = f"{f}:{line}" if f and line else (f or "-")
    row = f"{loc} {'failure' if kind == 'error' else 'warning'} {msg}"
    allf.append(row)
    if kind == "error":
        block.append(row)
open(sys.argv[2], "w").write("\n".join(allf) + "\n")
open(sys.argv[3], "w").write("\n".join(block) + "\n")
EOF
  cp .ru-misra/report/* "$out/" 2>/dev/null
  for c in "${configs[@]}"; do cp ".ru-misra/out/$c/results.sarif" "$out/$c.sarif" 2>/dev/null; done
fi
for c in "${configs[@]}"; do grep -h '^::error' "$out/scan-$c.log" 2>/dev/null; done | sed 's/^::error::/- /' >>"$out/blocking.txt"
echo
if [ -s "$out/blocking.txt" ]; then
  echo "blocking findings (these fail CI):"; sed '/^$/d' "$out/blocking.txt" | sort -u | sed 's/^/  /'
fi
nw=$(grep -c ' warning ' "$out/findings.txt" 2>/dev/null); echo "${nw:-0} warning(s) that do not block: $out/findings.txt"
grep -E '^level ' "$out/report.log" 2>/dev/null
[ -s "$out/blocking.txt" ] && verdict=1
echo "timing: $(tr '\n' ' ' <"$out/timing.txt")"
echo "full log and artifacts: $out"
exit "$verdict"
