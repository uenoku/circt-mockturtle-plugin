#!/usr/bin/env python3
"""Run each mockturtle pass on an MLIR file and LEC-check the result."""

import argparse
import dataclasses
import os
from pathlib import Path
import re
import shutil
import subprocess
from typing import Iterable


@dataclasses.dataclass(frozen=True)
class PassSpec:
    name: str
    anchor: str = "hw.module"

    @property
    def pipeline(self) -> str:
        if self.anchor == "module":
            return f"builtin.module({self.name})"
        return f"builtin.module(hw.module({self.name}))"


DEFAULT_PASSES = [
    PassSpec("synth-mockturtle-refactor"),
    PassSpec("synth-mockturtle-functional-reduction"),
    PassSpec("synth-mockturtle-sop-balancing"),
    PassSpec("synth-mockturtle-esop-balancing"),
    PassSpec("synth-mockturtle-aig-balancing"),
    PassSpec("synth-mockturtle-xag-balancing"),
    PassSpec("synth-mockturtle-aig-resubstitution"),
    PassSpec("synth-mockturtle-aig-resubstitution2"),
    PassSpec("synth-mockturtle-xag-resubstitution"),
    PassSpec("synth-mockturtle-mig-resubstitution"),
    PassSpec("synth-mockturtle-mig-resubstitution2"),
    PassSpec("synth-mockturtle-xmg-resubstitution"),
    PassSpec("synth-mockturtle-mig-algebraic-rewrite-depth"),
    PassSpec("synth-mockturtle-xag-algebraic-rewrite-depth"),
    PassSpec("synth-mockturtle-xmg-algebraic-rewrite-depth"),
    PassSpec("synth-mockturtle-mig-inv-propagation"),
    PassSpec("synth-mockturtle-mig-inv-optimization"),
]

OPTIONAL_PASSES = [
    # Requires a technology library in ordinary use, so keep it opt-in.
    PassSpec("synth-mockturtle-emap", "module"),
]


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def default_tool(*parts: str) -> Path | None:
    path = repo_root().joinpath(*parts)
    return path if path.exists() else None


def find_tool(user_value: str | None, env_var: str, default: Path | None,
              name: str, option: str) -> str:
    for candidate in (user_value, os.environ.get(env_var),
                      str(default) if default else None):
        if not candidate:
            continue
        path = shutil.which(candidate) if os.sep not in candidate else candidate
        if path and Path(path).exists():
            return path
    path = shutil.which(name)
    if path:
        return path
    raise SystemExit(f"could not find {name}; pass {option} or set {env_var}")


def find_path(user_value: str | None, env_var: str, defaults: Iterable[Path],
              description: str, option: str) -> str | None:
    candidates = [user_value, os.environ.get(env_var)]
    candidates.extend(str(path) for path in defaults)
    for candidate in candidates:
        if not candidate:
            continue
        path = shutil.which(candidate) if os.sep not in candidate else candidate
        if path and Path(path).exists():
            return path
    if user_value or os.environ.get(env_var):
        raise SystemExit(
            f"could not find {description}; pass {option} or set {env_var}")
    return None


def find_opt_command(args: argparse.Namespace) -> list[str]:
    root = repo_root()
    plugin = find_path(args.pass_plugin, "CIRCT_EXPERIMENT_PASS_PLUGIN",
                       [root / "build/lib/CIRCTExperimentPlugin.so"],
                       "mockturtle pass plugin", "--pass-plugin")

    if plugin and not args.opt:
        circt_opt = find_tool(args.circt_opt, "CIRCT_OPT",
                              root.parent / "build/bin/circt-opt", "circt-opt",
                              "--circt-opt")
        return [circt_opt, f"--load-pass-plugin={plugin}"]

    opt = find_tool(args.opt, "CIRCT_EXPERIMENT_OPT",
                    default_tool("build", "bin", "circt-experiment-opt"),
                    "circt-experiment-opt", "--circt-experiment-opt")
    return [opt]


def find_circt_lec(user_value: str | None) -> str:
    root = repo_root()
    candidates = [
        user_value,
        os.environ.get("CIRCT_LEC"),
        str(root / "build/bin/circt-lec"),
        str(root.parent / "build/bin/circt-lec"),
        str(root.parent / "build/tools/circt-lec/circt-lec"),
    ]
    for candidate in candidates:
        if not candidate:
            continue
        path = shutil.which(candidate) if os.sep not in candidate else candidate
        if path and Path(path).exists():
            return path
    path = shutil.which("circt-lec")
    if path:
        return path
    raise SystemExit("could not find circt-lec; pass --circt-lec or set CIRCT_LEC")


def discover_top(input_file: Path) -> str:
    text = input_file.read_text()
    match = re.search(r"\bhw\.module\s+(?:private\s+)?@([A-Za-z_.$-][\w.$-]*)",
                      text)
    if not match:
        raise SystemExit(f"could not infer top module from {input_file}; pass --top")
    return match.group(1)


def shell_quote_cmd(cmd: Iterable[str]) -> str:
    return " ".join(shlex_quote(arg) for arg in cmd)


def shlex_quote(arg: str) -> str:
    if re.fullmatch(r"[A-Za-z0-9_@%+=:,./-]+", arg):
        return arg
    return "'" + arg.replace("'", "'\"'\"'") + "'"


def run_command(cmd: list[str], log_path: Path, timeout: float | None) -> int:
    with log_path.open("w") as log:
        log.write("$ " + shell_quote_cmd(cmd) + "\n\n")
        log.flush()
        try:
            proc = subprocess.run(
                cmd,
                stdout=log,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=timeout,
            )
            return proc.returncode
        except subprocess.TimeoutExpired as exc:
            log.write(f"\nTIMEOUT after {exc.timeout} seconds\n")
            return 124


def parse_pass_list(values: list[str] | None,
                    include_emap: bool) -> list[PassSpec]:
    known = {spec.name: spec for spec in DEFAULT_PASSES + OPTIONAL_PASSES}
    if not values:
        passes = list(DEFAULT_PASSES)
        if include_emap:
            passes += OPTIONAL_PASSES
        return passes

    result = []
    for value in values:
        for name in value.split(","):
            name = name.strip()
            if not name:
                continue
            if name not in known:
                raise SystemExit(f"unknown pass '{name}'")
            result.append(known[name])
    return result


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("input", nargs="?", default="a.mlir", type=Path)
    parser.add_argument("--top", help="Top hw.module name for both designs")
    parser.add_argument("--c1",
                        help="Original-design top name; defaults to --top")
    parser.add_argument("--c2",
                        help="Transformed-design top name; defaults to --top")
    parser.add_argument("--out-dir", default="mockturtle-lec-fuzz-out", type=Path)
    parser.add_argument("--circt-opt", dest="circt_opt")
    parser.add_argument("--pass-plugin",
                        help="Mockturtle pass plugin for use with circt-opt")
    parser.add_argument("--circt-experiment-opt", dest="opt")
    parser.add_argument("--circt-lec", dest="lec")
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--pass", dest="passes", action="append",
                        help="Pass name or comma-separated pass names to run")
    parser.add_argument("--include-emap", action="store_true",
                        help="Also try synth-mockturtle-emap")
    parser.add_argument("--mlir-disable-threading", action="store_true",
                        help="Pass --mlir-disable-threading to the opt driver")
    parser.add_argument("--lec-extra", action="append", default=[],
                        help="Extra argument for circt-lec; repeat as needed")
    parser.add_argument("--keep-going", action=argparse.BooleanOptionalAction,
                        default=True, help="Continue after pass or LEC failures")
    args = parser.parse_args()

    input_file = args.input.resolve()
    if not input_file.exists():
        raise SystemExit(f"input file does not exist: {input_file}")

    opt_cmd_prefix = find_opt_command(args)
    lec = find_circt_lec(args.lec)
    top = args.top or discover_top(input_file)
    c1 = args.c1 or top
    c2 = args.c2 or top
    passes = parse_pass_list(args.passes, args.include_emap)

    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    summary_path = out_dir / "summary.tsv"

    rows = [("pass", "opt", "lec", "transformed", "opt_log", "lec_log")]
    print(f"input: {input_file}")
    print(f"top: c1={c1} c2={c2}")
    print(f"out: {out_dir}")

    for spec in passes:
        safe_name = spec.name.removeprefix("synth-mockturtle-")
        transformed = out_dir / f"{safe_name}.mlir"
        opt_log = out_dir / f"{safe_name}.opt.log"
        lec_log = out_dir / f"{safe_name}.lec.log"

        opt_cmd = opt_cmd_prefix + [
            str(input_file), "--pass-pipeline", spec.pipeline, "-o",
            str(transformed)
        ]
        if args.mlir_disable_threading:
            opt_cmd.insert(2, "--mlir-disable-threading")

        opt_rc = run_command(opt_cmd, opt_log, args.timeout)
        lec_rc: int | str = "skip"
        if opt_rc == 0:
            lec_cmd = [lec, str(input_file), str(transformed), "--c1", c1, "--c2",
                       c2] + args.lec_extra
            lec_rc = run_command(lec_cmd, lec_log, args.timeout)

        rows.append((spec.name, str(opt_rc), str(lec_rc), str(transformed),
                     str(opt_log), str(lec_log)))
        print(f"{spec.name}: opt={opt_rc} lec={lec_rc}")

        if not args.keep_going and (opt_rc != 0 or lec_rc != 0):
            break

    with summary_path.open("w") as summary:
        for row in rows:
            summary.write("\t".join(row) + "\n")

    print(f"summary: {summary_path}")
    return 0 if all(row[1] == "0" and row[2] == "0" for row in rows[1:]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
