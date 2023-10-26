#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Python script to generate ebpf skeletons from bpf code.

This script compiles the C code with target bpf and then runs bpftool against
the resulting object file to generate bpf skeleton header files that can then be
used by userspace programs to load, attach and communicate with bpf functions.
"""

import argparse
import logging
from pathlib import Path
import re
import subprocess
import sys
from typing import List


_HACK_VAR_TO_DISABLE_ISORT = "hack"
# pylint: disable=wrong-import-position
import chromite_init  # pylint: disable=unused-import

from chromite.lib import commandline
from chromite.lib import cros_build_lib


_ARCH_TO_DEFINE = {
    "amd64": "__TARGET_ARCH_x86",
    "amd64-linux": "__TARGET_ARCH_x86",
    "arm": "__TARGET_ARCH_arm",
    "arm-linux": "__TARGET_ARCH_arm",
    "arm64": "__TARGET_ARCH_arm64",
    "mips": "__TARGET_ARCH_mips",
    "ppc": "__TARGET_ARCH_powerpc",
    "ppc64": "__TARGET_ARCH_powerpc",
    "ppc64-linux": "__TARGET_ARCH_powerpc",
    "x86": "__TARGET_ARCH_x86",
    "x86-linux": "__TARGET_ARCH_x86",
}


def _run_command(command: List[str]) -> subprocess.CompletedProcess:
    """Run a command with default options.

    Run a command using subprocess.run with default configuration.
    """
    return cros_build_lib.run(
        command,
        capture_output=True,
        encoding="utf-8",
    )


def do_compile_bpf(opts: argparse.Namespace) -> int:
    """Compile BPF program from C.

    Takes a BPF application written in C and generates the BPF object file.
    If args.out_header is specified, the BPF object file is also processed to
    generate BPF skeletons using bpftool.
    If args.out_min_btf is specified, the BPF object file is also processed to
    generate a min CO-RE BTF.
    """
    out_obj = opts.out_obj
    out_bpf_skeleton_header = opts.out_bpf_skeleton_header
    out_btf = opts.out_min_btf
    vmlinux_btf = opts.vmlinux_btf
    source = opts.source
    arch = _ARCH_TO_DEFINE[opts.arch]
    includes = opts.includes
    defines = opts.defines or []
    sysroot = opts.sysroot

    # Create the folder to hold the output if it does not exist.
    out_obj.parent.mkdir(parents=True, exist_ok=True)

    # Calling bpf-clang is equivalent to "clang --target bpf".
    # It may seem odd that the application needs to be compiled with -g but
    # then llvm-strip is ran against the resulting object.
    # The -g is needed for the bpf application to compile properly but we
    # want to reduce the file size by stripping it.
    call_bpf_clang = (
        ["/usr/bin/bpf-clang", "-g", "-O2", f"--sysroot={sysroot}"]
        + [f"-I{x}" for x in includes]
        + [f"-D{x}" for x in defines]
        + [f"-D{arch}", "-c", source, "-o", out_obj]
    )
    strip_dwarf = ["llvm-strip", "-g", out_obj]

    # Compile the BPF C application.
    _run_command(call_bpf_clang)
    # Strip useless dwarf information.
    _run_command(strip_dwarf)

    # Use bpftools to generate skeletons from the BPF object files.
    if out_bpf_skeleton_header:
        gen_skeleton = ["/usr/sbin/bpftool", "gen", "skeleton", out_obj]
        bpftool_proc = _run_command(gen_skeleton)

        # BPFtools will output the C formatted dump of kernel symbols to stdout.
        # Write the contents to file.
        out_bpf_skeleton_header.write_text(
            bpftool_proc.stdout, encoding="utf-8"
        )

    # Generate a detached min_core BTF.
    if out_btf:
        if not vmlinux_btf:
            print(
                "Need a full vmlinux BTF as input in order to generate a min "
                "BTF"
            )
            return 1
        gen_min_core_btf = [
            "/usr/sbin/bpftool",
            "gen",
            "min_core_btf",
            vmlinux_btf,
            out_btf,
            out_obj,
        ]
        _run_command(gen_min_core_btf)

    return 0


def do_gen_vmlinux(opts: argparse.Namespace) -> int:
    """Generate vmlinux.h for use in BPF programs.

    Invokes pahole and bpftool to generate vmlinux.h from vmlinux from the
    kernel build. Uses BTF as an intermediate format. The generated BTF is
    preserved for possible use in generation of min CO-RE BTFs.
    """
    sysroot = opts.sysroot
    vmlinux_out = opts.out_header
    btf_out = opts.out_btf
    vmlinux_in = f"{sysroot}/usr/lib/debug/boot/vmlinux"
    gen_detached_btf = [
        "/usr/bin/pahole",
        "--btf_encode_detached",
        btf_out,
        vmlinux_in,
    ]
    gen_vmlinux = [
        "/usr/sbin/bpftool",
        "btf",
        "dump",
        "file",
        btf_out,
        "format",
        "c",
    ]
    read_symbols = [
        "/usr/bin/readelf",
        "--symbols",
        "--wide",
        vmlinux_in,
    ]
    read_sections = [
        "/usr/bin/readelf",
        "--sections",
        "--wide",
        vmlinux_in,
    ]

    # First, run pahole to generate a detached vmlinux BTF. This step works
    # regardless of whether the vmlinux was built with CONFIG_DEBUG_BTF_INFO.
    btf_out.parent.mkdir(parents=True, exist_ok=True)
    _run_command(gen_detached_btf)

    symbols = _run_command(read_symbols)
    for line in symbols.stdout.splitlines():
        # pylint: disable=line-too-long
        # Example line to parse:
        # 145612: ffffffff823e1ca0   274 OBJECT  GLOBAL DEFAULT    2 linux_banner
        d = line.split()
        if len(d) == 8 and d[-1] == "linux_banner":
            linux_banner_addr = int(d[1], 16)
            linux_banner_len = int(d[2])
            linux_banner_sec = d[-2]
            break
    else:
        logging.error("Failed to find linux_banner from %s", vmlinux_in)
        return 1

    sections = _run_command(read_sections)
    target_section_str = f"[{linux_banner_sec:>2}]"
    for line in sections.stdout.splitlines():
        # pylint: disable=line-too-long
        # Example line to parse:
        # [ 2] .rodata           PROGBITS        ffffffff82200000 1400000 5e104e 00 WAMS  0   0 4096
        if line.lstrip().startswith(target_section_str):
            d = line.split("]", 1)[1].split()
            section_addr = int(d[2], 16)
            section_off = int(d[3], 16)
            break
    else:
        logging.error(
            "Failed to find section '%s' from %s",
            target_section_str,
            vmlinux_in,
        )
        return 1

    offset = section_off + linux_banner_addr - section_addr
    with open(vmlinux_in, "rb") as fp:
        fp.seek(offset)
        linux_banner = fp.read(linux_banner_len).decode("utf-8")

    m = re.match(r"Linux version (\d+).(\d+).(\d+)", linux_banner)
    if not m:
        logging.error("Failed to match Linux version: %s", linux_banner)
        return 1

    version = m.group(1)
    patch_level = m.group(2)
    sub_level = m.group(3)

    # Constructing LINUX_VERSION_CODE.
    linux_version_code = int(version) * 65536 + int(patch_level) * 256
    linux_version_code += min(int(sub_level), 255)

    # Then, use the generated BTF (and not vmlinux itself) to generate the
    # header.
    vmlinux_cmd = _run_command(gen_vmlinux)
    with vmlinux_out.open("w", encoding="utf-8") as vmlinux:
        written = False

        for line in vmlinux_cmd.stdout.splitlines():
            vmlinux.write(f"{line}\n")

            if not written and line == "#define __VMLINUX_H__":
                vmlinux.write(
                    f"\n#define LINUX_VERSION_CODE {linux_version_code}\n"
                )
                written = True

        if not written:
            logging.error("Failed to write LINUX_VERSION_CODE")
            return 1

    return 0


def get_parser() -> argparse.ArgumentParser:
    parser = commandline.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(help="sub-command help")

    compile_bpf = subparsers.add_parser("compile_bpf")
    compile_bpf.add_argument(
        "--out-obj",
        required=True,
        type=Path,
        help="The name of the output object file.",
    )
    compile_bpf.add_argument(
        "--out-bpf-skeleton-header",
        required=False,
        type=Path,
        help=(
            "The name of the eBPF skeleton output header file."
            " Specifying this argument will result in eBPF skeletons being"
            " generated in addition to the eBPF objects."
        ),
    )
    compile_bpf.add_argument(
        "--source", required=True, type=Path, help="The bpf source code."
    )
    compile_bpf.add_argument(
        "--arch",
        required=True,
        choices=_ARCH_TO_DEFINE.keys(),
        help="The target architecture.",
    )
    compile_bpf.add_argument(
        "--includes",
        required=True,
        nargs="+",
        help="Additional include directories.",
    )
    compile_bpf.add_argument(
        "--defines",
        required=False,
        nargs="*",
        help="Additional preprocessor defines.",
    )
    compile_bpf.add_argument(
        "--vmlinux-btf",
        required=False,
        type=Path,
        help="The detached full vmlinux BTF file.",
    )
    compile_bpf.add_argument(
        "--out-min-btf",
        required=False,
        type=Path,
        help="The name of the output min BTF file.",
    )
    # We require the board sysroot so that BPF compilations will use board
    # libbpf headers.
    compile_bpf.add_argument(
        "--sysroot",
        required=True,
        type="dir_exists",
        help="The path that should be treated as the root directory.",
    )

    compile_bpf.set_defaults(func=do_compile_bpf)

    gen_vmlinux = subparsers.add_parser("gen_vmlinux")
    gen_vmlinux.add_argument(
        "--sysroot",
        required=True,
        type="dir_exists",
        help="The path that should be treated as the root directory.",
    )
    gen_vmlinux.add_argument(
        "--out-header",
        required=True,
        type=Path,
        help="The name of the output vmlinux.h file.",
    )
    gen_vmlinux.add_argument(
        "--out-btf",
        required=True,
        type=Path,
        help="The name of the output vmlinux BTF file.",
    )
    gen_vmlinux.set_defaults(func=do_gen_vmlinux)

    return parser


def main(argv: List[str]) -> int:
    """A command line tool for all things BPF.

    A command line tool to help compile eBPF code to object file and generate C
    BPF skeletons, and to generate vmlinux.h from kernel build artifacts.
    """
    parser = get_parser()
    opts = parser.parse_args(argv)

    try:
        return opts.func(opts)
    except subprocess.CalledProcessError as e:
        logging.error(e)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
