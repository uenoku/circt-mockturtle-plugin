#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"

tool="$repo_dir/build/bin/circt-fraig-lec"
root="${WORD_LEVEL_HWMC_ROOT:-$repo_dir/../../word-level-hwmc-benchmarks}"
category="all"
set_path=""
depth=4
precheck_depth=0
divmod_bits=16
pdr_blocked_cube_limit=0
conflict_limit=-1
timeout_s=15
limit=0
order="name"
summary_table=0
detail_table=0
log_dir=""
tool_args=()

usage() {
  cat <<EOF
usage: $0 [options]

Run a bounded smoke test over word-level-hwmc-benchmarks BTOR2 files.

Options:
  --tool PATH       circt-fraig-lec executable (default: $tool)
  --root PATH       word-level-hwmc-benchmarks checkout (default: $root)
  --category NAME   all, bv, or array (default: all)
  --set PATH        benchmark subdirectory or single .btor2 file
  --depth N         --btor2-pdr-depth value (default: $depth)
  --precheck-depth N
                   --btor2-pdr-precheck-depth value (default: $precheck_depth)
  --divmod-bits N   --btor2-pdr-divmod-unknown-bits value (default: $divmod_bits)
  --pdr-blocked-cube-limit N
                   --btor2-pdr-blocked-cube-limit value (default: unlimited)
  --conflict-limit N
                   --conflict-limit value passed to circt-fraig-lec
                   (default: unlimited)
  --timeout SEC     per-file timeout in seconds (default: $timeout_s)
  --limit N         maximum number of sorted files to run (default: all)
  --order NAME      name or size; size runs smaller files first (default: $order)
  --tool-arg ARG    extra argument passed to circt-fraig-lec; may be repeated
                   (for example: --tool-arg --mlir-print-ir-after-all)
  --log-dir DIR     write full stdout/stderr from each benchmark to DIR
  --summary-table   print a markdown table row for timeout/success/fail counts
  --detail-table    print a markdown table row for each benchmark result
  -h, --help        show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
  --tool)
    tool="$2"
    shift 2
    ;;
  --root)
    root="$2"
    shift 2
    ;;
  --category)
    category="$2"
    shift 2
    ;;
  --set)
    set_path="$2"
    shift 2
    ;;
  --depth)
    depth="$2"
    shift 2
    ;;
  --precheck-depth)
    precheck_depth="$2"
    shift 2
    ;;
  --divmod-bits)
    divmod_bits="$2"
    shift 2
    ;;
  --pdr-blocked-cube-limit)
    pdr_blocked_cube_limit="$2"
    shift 2
    ;;
  --conflict-limit)
    conflict_limit="$2"
    shift 2
    ;;
  --timeout)
    timeout_s="$2"
    shift 2
    ;;
  --limit)
    limit="$2"
    shift 2
    ;;
  --order)
    order="$2"
    shift 2
    ;;
  --tool-arg)
    tool_args+=("$2")
    shift 2
    ;;
  --log-dir)
    log_dir="$2"
    shift 2
    ;;
  --summary-table)
    summary_table=1
    shift
    ;;
  --detail-table)
    detail_table=1
    shift
    ;;
  -h | --help)
    usage
    exit 0
    ;;
  *)
    echo "unknown option: $1" >&2
    usage >&2
    exit 2
    ;;
  esac
done

if [[ ! -x "$tool" ]]; then
  echo "tool is not executable: $tool" >&2
  exit 2
fi

if [[ -n "$set_path" ]]; then
  search_root="$set_path"
elif [[ "$category" == "all" ]]; then
  search_root="$root"
elif [[ "$category" == "bv" || "$category" == "array" ]]; then
  search_root="$root/$category/btor2"
else
  echo "unknown category: $category" >&2
  exit 2
fi

if [[ ! -e "$search_root" ]]; then
  echo "benchmark path does not exist: $search_root" >&2
  exit 2
fi

if [[ "$order" != "name" && "$order" != "size" ]]; then
  echo "unknown order: $order" >&2
  exit 2
fi

if [[ -n "$log_dir" ]]; then
  mkdir -p "$log_dir"
fi

files="$(mktemp "${TMPDIR:-/tmp}/circt-fraig-lec-bench.XXXXXX")"
trap 'rm -f "$files" "${details:-}"' EXIT

if [[ -f "$search_root" ]]; then
  printf "%s\n" "$search_root" >"$files"
elif [[ "$order" == "size" ]]; then
  find "$search_root" -type f -name '*.btor2' -printf '%s\t%p\n' |
    sort -n -k1,1 -k2,2 | cut -f2- >"$files"
else
  find "$search_root" -type f -name '*.btor2' | sort >"$files"
fi

if [[ "$limit" -gt 0 ]]; then
  limited="$(mktemp "${TMPDIR:-/tmp}/circt-fraig-lec-bench-limit.XXXXXX")"
  head -n "$limit" "$files" >"$limited"
  mv "$limited" "$files"
fi

declare -A counts=()
total=0
details="$(mktemp "${TMPDIR:-/tmp}/circt-fraig-lec-bench-details.XXXXXX")"
detail_sep=$'\034'

summary_label() {
  if [[ -n "$set_path" ]]; then
    if [[ "$set_path" == "$root" ]]; then
      echo "$(basename "$root")"
    elif [[ "$set_path" == "$root/"* ]]; then
      echo "${set_path#$root/}"
    else
      echo "$set_path"
    fi
  elif [[ "$category" == "all" ]]; then
    echo "$(basename "$root")"
  else
    echo "$category"
  fi
}

classify() {
  local rc="$1"
  local output="$2"
  if [[ "$rc" -eq 124 ]]; then
    echo "timeout"
  elif grep -q "pdr: proven safe" <<<"$output"; then
    echo "proven"
  elif grep -q "counterexample" <<<"$output"; then
    echo "counterexample"
  elif grep -q "unknown within depth" <<<"$output"; then
    echo "unknown"
  elif grep -q "large arrays are not supported" <<<"$output"; then
    echo "unsupported-large-array"
  elif grep -q "can only emulate 'comb\\." <<<"$output"; then
    echo "unsupported-divmod"
  elif grep -q "failed to legalize operation 'hw\\.array" <<<"$output"; then
    echo "unsupported-hw-array"
  else
    echo "error"
  fi
}

result_bucket() {
  case "$1" in
  proven | counterexample | unknown)
    echo "success"
    ;;
  timeout)
    echo "timeout"
    ;;
  *)
    echo "fail"
    ;;
  esac
}

markdown_escape() {
  sed -e 's/\\/\\\\/g' -e 's/|/\\|/g'
}

log_path_for() {
  local rel="$1"
  local safe
  safe="$(printf "%s" "$rel" |
    sed -e 's@^/*@@' -e 's@/@__@g' -e 's@[^A-Za-z0-9._=-]@_@g')"
  if [[ -z "$safe" ]]; then
    safe="benchmark"
  fi
  printf "%s/%s.log" "$log_dir" "$safe"
}

while IFS= read -r file; do
  [[ -n "$file" ]] || continue
  ((++total))
  output="$(timeout "$timeout_s" "$tool" --btor2-pdr-depth="$depth" \
    --btor2-pdr-precheck-depth="$precheck_depth" \
    --btor2-pdr-divmod-unknown-bits="$divmod_bits" \
    --btor2-pdr-blocked-cube-limit="$pdr_blocked_cube_limit" \
    --conflict-limit="$conflict_limit" \
    "${tool_args[@]}" "$file" 2>&1)" || rc=$?
  rc="${rc:-0}"
  status="$(classify "$rc" "$output")"
  counts["$status"]="$(( ${counts["$status"]:-0} + 1 ))"
  rel="${file#$root/}"
  log_file=""
  if [[ -n "$log_dir" ]]; then
    log_file="$(log_path_for "$rel")"
    printf "%s\n" "$output" >"$log_file"
  fi
  detail="$(sed '/^$/d' <<<"$output" | tail -n 1)"
  if [[ ${#detail} -gt 180 ]]; then
    detail="${detail:0:177}..."
  fi
  printf "%s: %s" "$rel" "$status"
  if [[ -n "$detail" ]]; then
    printf " -- %s" "$detail"
  fi
  if [[ -n "$log_file" ]]; then
    printf " -- log: %s" "$log_file"
  fi
  printf "\n"
  printf "%s%s%s%s%s%s%s%s%s\n" \
    "$rel" "$detail_sep" "$(result_bucket "$status")" "$detail_sep" \
    "$status" "$detail_sep" "$detail" "$detail_sep" "$log_file" \
    >>"$details"
  unset rc
done <"$files"

echo
echo "summary:"
echo "  total: $total"
for status in proven counterexample unknown timeout unsupported-large-array \
  unsupported-divmod unsupported-hw-array error; do
  echo "  $status: ${counts["$status"]:-0}"
done

if [[ "$summary_table" -eq 1 ]]; then
  proven="${counts["proven"]:-0}"
  counterexample="${counts["counterexample"]:-0}"
  unknown="${counts["unknown"]:-0}"
  timeout_count="${counts["timeout"]:-0}"
  unsupported_large_array="${counts["unsupported-large-array"]:-0}"
  unsupported_divmod="${counts["unsupported-divmod"]:-0}"
  unsupported_hw_array="${counts["unsupported-hw-array"]:-0}"
  error_count="${counts["error"]:-0}"
  success_count="$((proven + counterexample + unknown))"
  fail_count="$((unsupported_large_array + unsupported_divmod + unsupported_hw_array + error_count))"

  echo
  echo "| set | order | total | success | timeout | fail | proven | counterexample | unknown | unsupported | error |"
  echo "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"
  printf "| %s | %s | %d | %d | %d | %d | %d | %d | %d | %d | %d |\n" \
    "$(summary_label)" "$order" "$total" "$success_count" "$timeout_count" \
    "$fail_count" "$proven" "$counterexample" "$unknown" \
    "$((unsupported_large_array + unsupported_divmod + unsupported_hw_array))" \
    "$error_count"
fi

if [[ "$detail_table" -eq 1 ]]; then
  echo
  if [[ -n "$log_dir" ]]; then
    echo "| file | result | status | detail | log |"
    echo "|---|---|---|---|---|"
  else
    echo "| file | result | status | detail |"
    echo "|---|---|---|---|"
  fi
  while IFS="$detail_sep" read -r rel bucket status detail log_file; do
    if [[ -n "$log_dir" ]]; then
      printf "| %s | %s | %s | %s | %s |\n" \
        "$(printf "%s" "$rel" | markdown_escape)" "$bucket" "$status" \
        "$(printf "%s" "$detail" | markdown_escape)" \
        "$(printf "%s" "$log_file" | markdown_escape)"
    else
      printf "| %s | %s | %s | %s |\n" \
        "$(printf "%s" "$rel" | markdown_escape)" "$bucket" "$status" \
        "$(printf "%s" "$detail" | markdown_escape)"
    fi
  done <"$details"
fi
