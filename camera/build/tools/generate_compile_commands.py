#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A tool to generate compile_commands file."""

import argparse
import json
import logging
import os
import re
import subprocess
import sys
from typing import List


PLATFORM2_CAMERA_CORE_PACKAGES = [
    'chromeos-base/cros-camera',
    'chromeos-base/cros-camera-android-deps',
    'chromeos-base/cros-camera-libs',
    'chromeos-base/cros-camera-tool',
    'media-libs/cros-camera-connector-client',
    'media-libs/cros-camera-hal-usb',
]

PLATFORM2_CAMERA_TEST_PACKAGES = [
    # 'media-libs/cros-camera-libjda_test',  # not buildable at the moment
    'media-libs/cros-camera-libjea_test',
    'media-libs/cros-camera-test',
    'media-libs/cros-camera-usb-tests',
]

SYSROOT_COMPILEDB_PATH = '/build/{board}/build/compilation_database/{pkg}'
COMPILE_COMMANDS_CHROOT = 'compile_commands_chroot.json'
COMPILE_COMMANDS_NO_CHROOT = 'compile_commands_no_chroot.json'


def get_canonical_package_name(board: str, pkg: str) -> str:
    cmd = ['equery-%s' % (board), 'which', pkg]
    try:
        output = subprocess.check_output(' '.join(cmd), shell=True)
    except subprocess.CalledProcessError:
        raise ValueError('Unknown package: %s' % (pkg))
    ebuild_package_path = os.path.dirname(output.decode('utf-8').strip())
    pkg_name = os.path.basename(ebuild_package_path)
    pkg_category = os.path.basename(os.path.dirname(ebuild_package_path))
    return '/'.join((pkg_category, pkg_name))


def fix_source_file_path(compdb: List[dict], chroot: bool):
    """Fix file paths.

    This is required for platform2 packages that are not being cros-workon
    started, or for packages in platform/camera.
    """

    patterns = {
        # For src/platform2/camera
        r'(camera/.*)': 'src/platform2',

        # For src/platform/camera
        r'platform2/camera_hal/(.*)': 'src/platform/camera',
    }

    KEY_FILE = 'file'
    for cmd in compdb:
        if KEY_FILE not in cmd:
            continue
        filepath = cmd[KEY_FILE]
        if filepath.startswith('gen/'):
            continue

        match_obj = None
        repo_path = None
        for k, v in patterns.items():
            match_obj = re.search(k, filepath)
            if match_obj:
                repo_path = v
                break
        if not match_obj or not repo_path:
            logging.debug('Unrecognized source file %s', filepath)
            continue

        if chroot:
            src_root = '/mnt/host/source'
        else:
            src_root = os.environ.get('EXTERNAL_TRUNK_PATH')
            assert src_root is not None
        cmd[KEY_FILE] = os.path.join(
            src_root, repo_path, match_obj.group(1))


def fix_include_path(compdb: List[dict], chroot: bool):
    """Fix the -I include paths in cflags.

    This is required for packages in platform/camera, but nice-to-have for
    platform2 packages.
    """

    patterns = {
        # For src/platform2/camera
        r'-I[^ ]*/(camera/[^ ]*)': 'src/platform2',

        # For src/platform/camera
        r'-I[^ ]*/platform2/camera_hal/([^ ]*)': 'src/platform/camera',
    }

    KEY_COMMAND = 'command'
    for cmd in compdb:
        if KEY_COMMAND not in cmd:
            continue

        if chroot:
            src_root = '/mnt/host/source'
        else:
            src_root = os.environ.get('EXTERNAL_TRUNK_PATH')
            assert src_root is not None

        for k, v in patterns.items():
            cmd[KEY_COMMAND] = re.sub(
                k, '-I%s/%s' % (os.path.join(src_root, v), r'\1'),
                cmd[KEY_COMMAND])


def emerge_packages(board: str, packages: List[str], use_flags: str):
    """Run emerge command to build the packages."""

    use_flags = ' '.join([use_flags, 'compilation_database'])
    logging.info('Emerging the following packages with USE flags (%s):\n\t%s',
                 use_flags, '\n\t'.join(packages))
    emerge_cmd = ['USE="%s"' % (use_flags),
                  'emerge-%s' % (board), '-j']
    emerge_cmd.extend(packages)
    logging.debug('Running emerge cmd: %s', ' '.join(emerge_cmd))
    subprocess.check_call(' '.join(emerge_cmd), shell=True)


def aggregate_compile_db(
        board: str, packages: List[str], chroot: bool) -> List[dict]:
    """Aggregate the compilation database of the given packages into a list."""

    compdb_files = []
    for p in packages:
        compdb_path = os.path.join(
            SYSROOT_COMPILEDB_PATH.format(board=board, pkg=p),
            COMPILE_COMMANDS_CHROOT if chroot else
            COMPILE_COMMANDS_NO_CHROOT)
        if os.path.exists(compdb_path):
            compdb_files.append(compdb_path)

    logging.info('Combining the following compilation database:\n\t%s',
                 '\n\t'.join(compdb_files))
    result = []
    for path in compdb_files:
        with open(path, 'r') as f:
            result.extend(json.loads(f.read()))
    return result


def main(argv: list):
    parser = argparse.ArgumentParser()
    parser.add_argument('-b', '--board', type=str, required=True, help=(
                            'The board to emerge the camera packages for and'
                            'copy the compilation database from'))
    parser.add_argument('-o', '--output_file', type=str,
                        default='compile_commands.json', help=(
                            'Output compilation database file name '
                            '(default=%(default)s)'))
    parser.add_argument('--noemerge', dest='emerge', action='store_false',
                        default=True, help=(
                            'Do not emerge the packages and combine the '
                            'available existing compilation database in '
                            'the sysroot'))
    parser.add_argument('--nochroot', dest='chroot', action='store_false',
                        default=True, help=(
                            'Generate the no chroot version of compilation '
                            'database'))
    parser.add_argument('--append', action='store_true', help=(
                            'Append the compilation database of the specified '
                            'package to the existing compdb file instead of '
                            'overwritting it'))
    parser.add_argument('--debug', action='store_true', help=(
                            'Enable debug logs'))
    parser.add_argument('--use', type=str, default='', help=(
                            'Additional USE flag(s) to enable when emerging '
                            'the packages, e.g. "test -asan"'))
    parser.add_argument('packages', type=str, nargs='*', help=(
                            'Package(s) to emerge and/or copy compilation '
                            'database from, in addition to the default set '
                            'of packages: %s' %
                            ' '.join(PLATFORM2_CAMERA_CORE_PACKAGES +
                                     PLATFORM2_CAMERA_TEST_PACKAGES)))
    args = parser.parse_args(argv)

    log_level = logging.INFO
    if args.debug:
        log_level = logging.DEBUG
    logging.basicConfig(level=log_level)

    if not os.path.exists('/etc/cros_chroot_version'):
        raise RuntimeError('The script needs to run inside the CrOS SDK chroot')

    all_packages = (
        PLATFORM2_CAMERA_CORE_PACKAGES +
        PLATFORM2_CAMERA_TEST_PACKAGES +
        [get_canonical_package_name(args.board, p) for p in args.packages])

    if args.emerge:
        emerge_packages(args.board, all_packages, args.use)

    compdb_aggregated = []
    if args.append and os.path.exists(args.output_file):
        logging.info('Append to the existing compdb file %s', args.output_file)
        with open(args.output_file, 'r') as f:
            compdb_aggregated.extend(json.loads(f.read()))

    compdb_aggregated.extend(
        aggregate_compile_db(args.board, all_packages, args.chroot))

    fix_source_file_path(compdb_aggregated, args.chroot)
    fix_include_path(compdb_aggregated, args.chroot)

    with open(args.output_file, 'w+') as f:
        f.write(json.dumps(compdb_aggregated, indent=2))


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
