#!/usr/bin/env python3
"""Build and run offline regressions. Never deploy or execute the instrument app."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "software/app"
BUILD = ROOT / "build"
CONFIG = json.loads((ROOT / "software/build_config.json").read_text())


def run(argv, cwd, timeout=180):
    print("+ " + " ".join(map(str, argv)), flush=True)
    subprocess.run(list(map(str, argv)), cwd=cwd, check=True, timeout=timeout)


def compile_c(cc, sources, output, extra=(), sanitize=False):
    flags = list(CONFIG["flags"])
    if sanitize:
        flags.remove("-O2")
        flags += ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    run([cc, *flags, *extra, *sources, "-lm", "-o", output], SRC)


def tests(cc, sanitize=False):
    if os.name != "posix":
        raise SystemExit("Offline C tests require Linux/POSIX. Use a Linux host or CI.")
    results = []
    with tempfile.TemporaryDirectory(prefix="offline-", dir=BUILD) as work:
        work = Path(work)
        (work / "results693").mkdir()
        (work / "hold").mkdir()
        for case in CONFIG["tests"]:
            binary = work / case["name"]
            definitions = []
            if case["name"] == "recovery_hold":
                definitions = ['-DTEST699_ROOT="' + (work / "hold").as_posix() + '"']
            compile_c(cc, case["sources"], binary, definitions, sanitize)
            run([binary], work, timeout=120)
            results.append({"name": case["name"], "passed": True})
    (BUILD / "test-results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(f"OFFLINE_TESTS_PASS count={len(results)} hardware_access=none")


def app(cc):
    definitions = ["-DREFLECT_RAW_PAGE=1",
                   '-DDIAG_STORAGE_DIR="/tmp/coax-link-analyzer"',
                   '-DREFLECT_STORAGE_DIR="/tmp/coax-link-analyzer"',
                   '-DSCOPE_VIEW_PATH="/tmp/coax-link-analyzer/scope.view"']
    compile_c(cc, CONFIG["sources"], BUILD / "adc_scope_display", definitions)
    print("APP_COMPILE_PASS: binary was NOT executed or installed")


def rtl_tests():
    iv = shutil.which("iverilog")
    vv = shutil.which("vvp")
    if not iv or not vv:
        raise SystemExit("Install Icarus Verilog (iverilog and vvp) for reference RTL tests.")
    adc = ROOT / "fpga/rtl/adc"
    modules = sorted(adc.glob("*.v"))
    for source in modules:
        run([iv, "-g2012", "-s", source.stem, "-o", BUILD / (source.stem + ".vvp"),
             *modules], adc)
    dac = ROOT / "fpga/reference/dac400"
    binary = BUILD / "dac400.vvp"
    run([iv, "-g2012", "-s", "tb_dac_wavegen", "-o", binary,
         "dac_wavegen.v", "tb_dac_wavegen.sv"], dac)
    run([vv, binary], dac, timeout=90)
    print("RTL_REFERENCE_TESTS_PASS: no board or analog output was tested")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["test", "app", "rtl-test", "all"])
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    BUILD.mkdir(exist_ok=True)
    if args.command in ("test", "all"):
        tests(args.cc, args.sanitize)
    if args.command in ("app", "all"):
        app(args.cc)
    if args.command in ("rtl-test", "all"):
        rtl_tests()


if __name__ == "__main__":
    main()
