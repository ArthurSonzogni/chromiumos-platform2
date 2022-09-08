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
import os
import subprocess
import sys
import typing


def _run_command(command: typing.List[str]) -> subprocess.CompletedProcess:
    """Run a command with default options.

    Run a command using subprocess.run with default configuration.
    """
    return subprocess.run(
        command,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
    )


def do_gen_bpf_skeleton(args):
    """Generate BPF skeletons from C.

    Takes a BPF application written in C and generates the BPF object file and
    then uses that to generate BPF skeletons using bpftool.
    """
    out = args.out
    source = args.source
    arch = args.arch
    includes = args.includes
    clang = args.clang
    sysroot = args.sysroot

    obj = "_".join(os.path.basename(source).split(".")[:-1]) + ".o"

    arch_to_define = {
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
    arch = arch_to_define.get(arch, None)
    if arch is None:
        print(
            f"Unable to map arch={arch} to a sensible"
            "__TARGET_ARCH_XXX for bpf compilation."
        )
        return -1

    # It may seem odd that the application needs to be compiled with -g but
    # then llvm-strip is ran against the resulting object.
    # The -g is needed for the bpf application to compile properly but we
    # want to reduce the file size by stripping it.
    call_clang = (
        [clang, "-g", "-O2", "-target", "bpf", f"--sysroot={sysroot}"]
        + [f"-I{x}" for x in includes]
        + [f"-D{arch}", "-c", source, "-o", obj]
    )
    gen_skeleton = ["/usr/sbin/bpftool", "gen", "skeleton", obj]
    strip_dwarf = ["llvm-strip", "-g", obj]

    # Compile the BPF C application.
    _run_command(call_clang)
    # Strip useless dwarf information.
    _run_command(strip_dwarf)
    # Use bpftools to generate skeletons from the BPF object files.
    bpftool_proc = _run_command(gen_skeleton)

    # BPFtools will output the C formatted dump of kernel symbols to stdout.
    # Write the contents to file.
    with open(out, "w", encoding="utf-8") as bpf_skeleton:
        bpf_skeleton.write(bpftool_proc.stdout)

    return 0


def do_gen_vmlinux(args):
    """Generate vmlinux.h for use in BPF programs.

    Invokes bpftool to generate vmlinux.h from vmlinux from the kernel build.
    """
    sysroot = args.sysroot
    vmlinux_out = args.out
    gen_vmlinux = [
        "/usr/sbin/bpftool",
        "btf",
        "dump",
        "file",
        f"{sysroot}/usr/lib/debug/boot/vmlinux",
        "format",
        "c",
    ]
    vmlinux_cmd = _run_command(gen_vmlinux)
    with open(f"{vmlinux_out}", "w", encoding="utf-8") as vmlinux:
        vmlinux.write(vmlinux_cmd.stdout)
    return 0


def main(argv: typing.List[str]) -> int:
    """A command line tool for all things BPF.

    A command line tool to help generate C BPF skeletons and to generate
    vmlinux.h from kernel build artifacts.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(help="sub-command help")

    gen_skel = subparsers.add_parser("gen_skel")
    gen_skel.add_argument(
        "--out", required=True, help="The name of the output file."
    )
    gen_skel.add_argument(
        "--source", required=True, help="The bpf source code."
    )
    gen_skel.add_argument(
        "--clang", required=True, help="The clang compiler to use."
    )
    gen_skel.add_argument(
        "--arch", required=True, help="The target architecture."
    )
    gen_skel.add_argument(
        "--includes",
        required=True,
        nargs="+",
        help="Additional include directories.",
    )
    # We require the board sysroot so that BPF compilations will use board
    # libbpf headers.
    gen_skel.add_argument(
        "--sysroot",
        required=True,
        help="The path that should be treated as the root directory.",
    )

    gen_skel.set_defaults(func=do_gen_bpf_skeleton)

    gen_vmlinux = subparsers.add_parser("gen_vmlinux")
    gen_vmlinux.add_argument(
        "--sysroot",
        required=True,
        help="The path that should be treated as the root directory.",
    )
    gen_vmlinux.add_argument(
        "--out", required=True, help="The name of the output file."
    )
    gen_vmlinux.set_defaults(func=do_gen_vmlinux)
    args = parser.parse_args(argv)

    try:
        return args.func(args)
    except subprocess.CalledProcessError as error:
        print(
            f'cmd={" ".join(error.cmd)}\nstderr={error.stderr}\n'
            f"stdout={error.stdout}\nretcode={error.returncode}\n"
        )
        return -1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
