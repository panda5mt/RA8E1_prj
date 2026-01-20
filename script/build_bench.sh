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
  echo "CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL-}" 
  echo
  echo "Temperatures (raw):"
  read_temps || true
} | tee -a "$log_path"

if [[ $clean -eq 1 ]]; then
  section "Clean"
  log "Removing build dir: $build_dir"
  rm -rf "$build_dir"
fi

if [[ $no_configure -eq 0 ]]; then
  section "Configure"
  log "Configuring..."
  cmake -S . -B "$build_dir" -G "$generator" \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DCMAKE_BUILD_TYPE="$build_type" \
    2>&1 | tee -a "$log_path"
else
  section "Configure"
  log "Skipping configure (--no-configure)"
fi

build_once() {
  local tag="$1"
  section "Build ${tag}"
  log "Build start (parallel=$parallel)"

  echo "Temperatures before (raw):" | tee -a "$log_path"
  read_temps | tee -a "$log_path" || true

  # Measure elapsed time. Use /usr/bin/time when available; otherwise fall back to wall clock.
  # Note: bash's `time` is a keyword and cannot be invoked via a variable.
  local start_s end_s elapsed_s
  start_s="$(date +%s)"

  set +e
  if [[ -x /usr/bin/time ]]; then
    # -p prints: real/user/sys
    /usr/bin/time -p cmake --build "$build_dir" --parallel "$parallel" 2>&1 | tee -a "$log_path"
    local rc=${PIPESTATUS[0]}
  else
    echo "NOTE: /usr/bin/time not found; logging wall-clock elapsed time only" | tee -a "$log_path"
    cmake --build "$build_dir" --parallel "$parallel" 2>&1 | tee -a "$log_path"
    local rc=${PIPESTATUS[0]}
  fi
  set -e

  end_s="$(date +%s)"
  elapsed_s=$(( end_s - start_s ))
  echo "elapsed_seconds: ${elapsed_s}" | tee -a "$log_path"

  echo "Temperatures after (raw):" | tee -a "$log_path"
  read_temps | tee -a "$log_path" || true

  if [[ $rc -ne 0 ]]; then
    log "Build failed with exit code $rc"
    exit "$rc"
  fi
  log "Build finished OK"
}

build_once "#1"

if [[ $twice -eq 1 ]]; then
  build_once "#2 (incremental)"
fi

section "Done"
log "Log saved to: $log_path"
