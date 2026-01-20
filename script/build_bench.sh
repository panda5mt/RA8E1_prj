#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  ./script/build_bench.sh [options]

Options:
  -t, --type <Release|Debug|RelWithDebInfo|MinSizeRel>   Build type (default: Release)
  -B, --build-dir <dir>                                  Build dir (default: build/<type>)
  -G, --generator <name>                                 CMake generator (default: Ninja if available else Unix Makefiles)
  -j, --parallel <n>                                     Parallel jobs (default: nproc)
  --clean                                                Remove build dir before configuring
  --twice                                                Run build twice (2nd run measures incremental speed)
  --suite                                                Run 3 benchmark cases in one shot: (1) normal -j N, (2) -j 1, (3) /tmp build -j N
  --no-configure                                         Skip configure step (assumes build dir already configured)
  --toolchain <path>                                     Toolchain file (default: cmake/llvm.cmake)
  --log <path>                                           Log output path (default: build_bench_<host>_<type>.log)
  -h, --help                                             Show this help

Examples:
  ./script/build_bench.sh --clean --twice
  ./script/build_bench.sh -t Debug --clean -j 4
  ./script/build_bench.sh -G Ninja --clean --twice
EOF
}

# Defaults
build_type="Release"
build_dir=""
generator=""
parallel=""
clean=0
twice=0
suite=0
no_configure=0
toolchain="cmake/llvm.cmake"
log_path=""

# Parse args
while [[ $# -gt 0 ]]; do
  case "$1" in
    -t|--type)
      build_type="${2:-}"; shift 2;;
    -B|--build-dir)
      build_dir="${2:-}"; shift 2;;
    -G|--generator)
      generator="${2:-}"; shift 2;;
    -j|--parallel)
      parallel="${2:-}"; shift 2;;
    --clean)
      clean=1; shift;;
    --twice)
      twice=1; shift;;
    --suite)
      suite=1; shift;;
    --no-configure)
      no_configure=1; shift;;
    --toolchain)
      toolchain="${2:-}"; shift 2;;
    --log)
      log_path="${2:-}"; shift 2;;
    -h|--help)
      usage; exit 0;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

host="$(hostname 2>/dev/null || echo unknown)"
arch="$(uname -m 2>/dev/null || echo unknown)"

if [[ -z "$parallel" ]]; then
  if command -v nproc >/dev/null 2>&1; then
    parallel="$(nproc)"
  else
    parallel="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
  fi
fi

if [[ -z "$generator" ]]; then
  if command -v ninja >/dev/null 2>&1; then
    generator="Ninja"
  else
    generator="Unix Makefiles"
  fi
fi

if [[ -z "$build_dir" ]]; then
  build_dir="build/${build_type}"
fi

if [[ -z "$log_path" ]]; then
  log_path="build_bench_${host}_${arch}_${build_type}.log"
fi

# Logging helpers
log() {
  # shellcheck disable=SC2001
  echo "[$(date -Is)] $*" | tee -a "$log_path"
}

section() {
  echo | tee -a "$log_path"
  echo "==== $* ====" | tee -a "$log_path"
}

read_temps() {
  # Print a compact list of thermal zones if present
  for z in /sys/class/thermal/thermal_zone*/temp; do
    [[ -r "$z" ]] || continue
    local v
    v="$(cat "$z" 2>/dev/null || true)"
    [[ -n "$v" ]] || continue
    echo "$(basename "$(dirname "$z")"): $v"  # usually milli-C
  done
}

read_cpufreq() {
  # Prefer cpufreq policies (works better on big.LITTLE / shared policies).
  local any=0
  for p in /sys/devices/system/cpu/cpufreq/policy*; do
    [[ -d "$p" ]] || continue
    any=1
    local gov drv cur min max
    gov="$(cat "$p/scaling_governor" 2>/dev/null || true)"
    drv="$(cat "$p/scaling_driver" 2>/dev/null || true)"
    cur="$(cat "$p/scaling_cur_freq" 2>/dev/null || true)"
    min="$(cat "$p/cpuinfo_min_freq" 2>/dev/null || true)"
    max="$(cat "$p/cpuinfo_max_freq" 2>/dev/null || true)"
    echo "$(basename "$p"): governor=${gov:-?} driver=${drv:-?} cur_khz=${cur:-?} min_khz=${min:-?} max_khz=${max:-?}"
  done

  if [[ $any -eq 0 ]]; then
    # Fallback: some kernels expose per-cpu cpufreq without policy*.
    for c in /sys/devices/system/cpu/cpu[0-9]*/cpufreq; do
      [[ -d "$c" ]] || continue
      any=1
      local gov cur max
      gov="$(cat "$c/scaling_governor" 2>/dev/null || true)"
      cur="$(cat "$c/scaling_cur_freq" 2>/dev/null || true)"
      max="$(cat "$c/cpuinfo_max_freq" 2>/dev/null || true)"
      echo "$(basename "$(dirname "$c")"): governor=${gov:-?} cur_khz=${cur:-?} max_khz=${max:-?}"
    done
  fi

  if [[ $any -eq 0 ]]; then
    echo "(cpufreq sysfs not available)"
  fi
}

read_tmpfs_info() {
  echo "df -T /tmp:" 
  df -T /tmp 2>/dev/null || true
  if command -v findmnt >/dev/null 2>&1; then
    echo
    echo "findmnt /tmp:"
    findmnt -no TARGET,SOURCE,FSTYPE,OPTIONS /tmp 2>/dev/null || true
  fi
}

section "System"
{
  echo "repo_root: $repo_root"
  echo "host: $host"
  echo "arch: $arch"
  echo "build_type: $build_type"
  echo "build_dir: $build_dir"
  echo "generator: $generator"
  echo "parallel: $parallel"
  echo "toolchain: $toolchain"
  echo
  uname -a || true
  echo
  lscpu || true
  echo
  echo "Uptime/load:"
  uptime || true
  echo
  echo "Memory:"
  free -h || true
  echo
  echo "/tmp filesystem:"
  read_tmpfs_info || true
  echo
  echo "CPU freq/governor (current):"
  read_cpufreq || true
  echo
  echo "CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL-}" 
  echo
  echo "Temperatures (raw):"
  read_temps || true
} | tee -a "$log_path"

configure_build_dir() {
  local dir="$1"
  if [[ $no_configure -eq 1 ]]; then
    section "Configure"
    log "Skipping configure (--no-configure)"
    return 0
  fi

  section "Configure"
  log "Configuring..."
  cmake -S . -B "$dir" -G "$generator" \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DCMAKE_BUILD_TYPE="$build_type" \
    2>&1 | tee -a "$log_path"
}

clean_build_dir() {
  local dir="$1"
  section "Clean"
  log "Removing build dir: $dir"
  rm -rf "$dir"
}

BUILD_ELAPSED_SECONDS=""

build_once() {
  local tag="$1"
  local dir="$2"
  local jobs="$3"
  section "Build ${tag}"
  log "Build start (parallel=$jobs, build_dir=$dir)"

  echo "CPU freq/governor before:" | tee -a "$log_path"
  read_cpufreq | tee -a "$log_path" || true
  echo "Loadavg before:" | tee -a "$log_path"
  cat /proc/loadavg 2>/dev/null | tee -a "$log_path" || true

  echo "Temperatures before (raw):" | tee -a "$log_path"
  read_temps | tee -a "$log_path" || true

  # Measure elapsed time. Use /usr/bin/time when available; otherwise fall back to wall clock.
  # Note: bash's `time` is a keyword and cannot be invoked via a variable.
  local start_s end_s elapsed_s
  start_s="$(date +%s)"

  set +e
  if [[ -x /usr/bin/time ]]; then
    # -p prints: real/user/sys
    /usr/bin/time -p cmake --build "$dir" --parallel "$jobs" 2>&1 | tee -a "$log_path"
    local rc=${PIPESTATUS[0]}
  else
    echo "NOTE: /usr/bin/time not found; logging wall-clock elapsed time only" | tee -a "$log_path"
    cmake --build "$dir" --parallel "$jobs" 2>&1 | tee -a "$log_path"
    local rc=${PIPESTATUS[0]}
  fi
  set -e

  end_s="$(date +%s)"
  elapsed_s=$(( end_s - start_s ))
  echo "elapsed_seconds: ${elapsed_s}" | tee -a "$log_path"
  BUILD_ELAPSED_SECONDS="$elapsed_s"

  echo "CPU freq/governor after:" | tee -a "$log_path"
  read_cpufreq | tee -a "$log_path" || true
  echo "Loadavg after:" | tee -a "$log_path"
  cat /proc/loadavg 2>/dev/null | tee -a "$log_path" || true

  echo "Temperatures after (raw):" | tee -a "$log_path"
  read_temps | tee -a "$log_path" || true

  if [[ $rc -ne 0 ]]; then
    log "Build failed with exit code $rc"
    exit "$rc"
  fi
  log "Build finished OK"
}

run_case() {
  local name="$1"
  local dir="$2"
  local jobs="$3"

  section "Case: $name"
  log "case_name=$name"
  log "case_build_dir=$dir"
  log "case_parallel=$jobs"

  if [[ $clean -eq 1 ]]; then
    clean_build_dir "$dir"
  fi
  configure_build_dir "$dir"

  local e1 e2
  build_once "${name} #1" "$dir" "$jobs"
  e1="$BUILD_ELAPSED_SECONDS"

  if [[ $twice -eq 1 ]]; then
    build_once "${name} #2 (incremental)" "$dir" "$jobs"
    e2="$BUILD_ELAPSED_SECONDS"
  else
    e2=""
  fi

  # stdout-friendly summary line (also captured into log)
  echo "summary: case=${name} build_dir=${dir} parallel=${jobs} elapsed1=${e1} elapsed2=${e2}" | tee -a "$log_path"

  # return values via globals
  CASE_ELAPSED1="$e1"
  CASE_ELAPSED2="$e2"
}

CASE_ELAPSED1=""
CASE_ELAPSED2=""

if [[ $suite -eq 1 ]]; then
  # Suite always runs 3 cases. We keep the user-provided -j as the 'normal' parallelism.
  # For /tmp build, pick a unique directory per host/build_type to avoid collisions.
  normal_parallel="$parallel"

  tmp_dir="/tmp/RA8E1_build_${host}_${build_type}"

  # In suite mode, it's almost always intended to be reproducible; default to clean unless user asked otherwise.
  # (If user explicitly passed --no-configure, we still honor it.)
  if [[ $clean -eq 0 ]]; then
    clean=1
  fi

  section "Suite"
  log "Suite start"

  run_case "normal" "$build_dir" "$normal_parallel"
  suite_normal_e1="$CASE_ELAPSED1"; suite_normal_e2="$CASE_ELAPSED2"

  run_case "single" "$build_dir" "1"
  suite_single_e1="$CASE_ELAPSED1"; suite_single_e2="$CASE_ELAPSED2"

  run_case "tmp" "$tmp_dir" "$normal_parallel"
  suite_tmp_e1="$CASE_ELAPSED1"; suite_tmp_e2="$CASE_ELAPSED2"

  section "Summary"
  printf '%-10s %-34s %-8s %-12s %-12s\n' "case" "build_dir" "-j" "elapsed1(s)" "elapsed2(s)" | tee -a "$log_path"
  printf '%-10s %-34s %-8s %-12s %-12s\n' "normal" "$build_dir" "$normal_parallel" "$suite_normal_e1" "${suite_normal_e2:-}" | tee -a "$log_path"
  printf '%-10s %-34s %-8s %-12s %-12s\n' "single" "$build_dir" "1" "$suite_single_e1" "${suite_single_e2:-}" | tee -a "$log_path"
  printf '%-10s %-34s %-8s %-12s %-12s\n' "tmp" "$tmp_dir" "$normal_parallel" "$suite_tmp_e1" "${suite_tmp_e2:-}" | tee -a "$log_path"
else
  # Legacy single-case behavior
  if [[ $clean -eq 1 ]]; then
    clean_build_dir "$build_dir"
  fi
  configure_build_dir "$build_dir"
  build_once "#1" "$build_dir" "$parallel"
  if [[ $twice -eq 1 ]]; then
    build_once "#2 (incremental)" "$build_dir" "$parallel"
  fi
fi

section "Done"
log "Log saved to: $log_path"
