#!/usr/bin/env python3
# # -*- coding: utf-8 -*-
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Create tarballs with modem FW, and upload them to OS Archive Mirrors."""

import argparse
import logging
from distutils.dir_util import copy_tree
from enum import Enum
import os
import shutil
import subprocess
import sys
import tempfile


class PackageType(Enum):
    """Packaging options for different firmwares or cust packs."""

    L850_MAIN_FW = 'l850-main-fw'
    L850_OEM_FW = 'l850-oem-fw'
    L850_OEM_DIR_ONLY = 'l850-oem-dir'
    NL668_MAIN_FW = 'nl668-main-fw'
    FM101_MAIN_FW = 'fm101-main-fw'

    def __str__(self):
        return str(self.value)


MIRROR_PATH = 'gs://chromeos-localmirror/distfiles/'
FIBOCOM_TARBALL_PREFIX = 'cellular-firmware-fibocom-'
L850_TARBALL_PREFIX = FIBOCOM_TARBALL_PREFIX + 'l850-'
NL668_TARBALL_PREFIX = FIBOCOM_TARBALL_PREFIX + 'nl668-'
FM101_TARBALL_PREFIX = FIBOCOM_TARBALL_PREFIX + 'fm101-'

OEM_FW_PREFIX = 'OEM_cust.'
OEM_FW_POSTFIX = '_signed.fls3.xz'


class FwUploader(object):
    """Class to verify the files and upload the tarball to a gs bucket."""

    def __init__(self, path, upload, tarball_dir_name):
        self.path = os.path.abspath(path)
        self.upload = upload
        self.basename = os.path.basename(self.path)
        self.tarball_dir_name = tarball_dir_name

    def process_fw_and_upload(self, keep_tmp_files):
        if not self.validate():
            return os.EX_USAGE

        tempdir = tempfile.mkdtemp()
        path_to_package = os.path.join(tempdir, self.tarball_dir_name)
        os.mkdir(path_to_package)

        if not self.prepare_files(self.path, path_to_package):
            logging.error('Failed to prepare files for packaging')
            return os.EX_OSFILE

        os.chdir(tempdir)
        tarball_name = f'{self.tarball_dir_name}.tar.xz'
        subprocess.run(['tar', '-Ipixz', '-cf', f'{tarball_name}',
                        f'{self.tarball_dir_name}/'],
                       stderr=subprocess.DEVNULL, check=False)
        tarball_path = os.path.join(tempdir, tarball_name)
        logging.info('Tarball created: %s', tarball_path)
        if self.upload:
            gs_bucket_path = os.path.join(MIRROR_PATH, tarball_name)
            logging.info('Uploading file %s to %s', tarball_path,
                         gs_bucket_path)
            subprocess.run(['gsutil', 'cp', '-n', '-a', 'public-read',
                            f'{tarball_path}', f'{gs_bucket_path}'],
                           stderr=subprocess.DEVNULL, check=False)
        if not keep_tmp_files:
            logging.info('Removing temporary files')
            shutil.rmtree(tempdir)

        return os.EX_OK


class L850MainFw(FwUploader):
    """Uploader class for L850GL main FW."""

    def __init__(self, path, upload):
        super().__init__(path, upload, None)
        self.tarball_dir_name = L850_TARBALL_PREFIX + self.basename.replace(
            '.fls3.xz', '')

    def validate(self):
        main_fw_postfix = 'Secureboot.fls3.xz'
        if not self.path.endswith(main_fw_postfix):
            logging.error('The main FW file `%s` name does not match `*%s`',
                          self.path, main_fw_postfix)
            return False
        return True

    @staticmethod
    def prepare_files(fw_path, target_path):
        logging.info('Copying %s into %s', fw_path, target_path)
        shutil.copy(fw_path, target_path)
        return True


class L850OemFw(FwUploader):
    """Uploader class for L850GL OEM FW."""

    def __init__(self, path, upload):
        super().__init__(path, upload, None)
        self.tarball_dir_name = (
            f'{L850_TARBALL_PREFIX}' +
            f'[{self.basename.replace(OEM_FW_POSTFIX, "")}]'
            )


    def validate(self):
        if not (self.basename.startswith(OEM_FW_PREFIX)
                and self.basename.endswith(OEM_FW_POSTFIX)):
            logging.error('The OEM FW file `%s` name does not match `%s*%s`',
                          self.basename, OEM_FW_PREFIX, OEM_FW_POSTFIX)
            return False
        return True

    @staticmethod
    def prepare_files(fw_path, target_path):
        logging.info('Copying %s into %s', fw_path, target_path)
        shutil.copy(fw_path, target_path)
        return True


class L850OemDir(FwUploader):
    """Uploader class for L850GL cust packs directory."""

    def __init__(self, path, upload, revision, board):
        super().__init__(path, upload, None)
        self.tarball_dir_name = (
            f'{L850_TARBALL_PREFIX}{board}' +
            f'-carriers_OEM_{self.basename}-{revision}')
        self.revision = revision

    def validate(self):
        if not self.revision.startswith(
                'r') or not self.revision[1:].isdigit():
            logging.error('The revision should be in the form of r##')
            return False
        if len(self.basename) != 4 or not self.basename.isdigit():
            logging.error('The OEM carrier directory name is expected to '
                          'consist of 4 digits')
            return False
        return True

    def prepare_files(self, dir_path, target_path):
        logging.info('Copying %s into %s', dir_path, target_path)
        os.mkdir(os.path.join(target_path, self.basename))
        copy_tree(dir_path, os.path.join(target_path, self.basename))

        return True


class NL668MainFw(FwUploader):
    """Uploader class for NL668 main FW."""

    def __init__(self, path, upload):
        super().__init__(path, upload, None)
        self.tarball_dir_name = NL668_TARBALL_PREFIX + self.basename

    def validate(self):
        if not os.path.isdir(self.path):
            logging.error('The NL668 FW should be a directory')
            return False
        return True

    def prepare_files(self, dir_path, target_path):
        logging.info('Copying %s into %s', dir_path, target_path)
        os.mkdir(os.path.join(target_path, self.basename))
        copy_tree(dir_path, os.path.join(target_path, self.basename))
        return True

class FM101MainFw(FwUploader):
    """Uploader class for FM101 main FW."""

    def __init__(self, path, upload):
        super().__init__(path, upload, None)
        self.tarball_dir_name = FM101_TARBALL_PREFIX + self.basename

    def validate(self):
        if not os.path.isdir(self.path):
            logging.error('The FM101 FW should be a directory')
            return False
        return True

    def prepare_files(self, dir_path, target_path):
        logging.info('Copying %s into %s', dir_path, target_path)
        os.mkdir(os.path.join(target_path, self.basename))
        copy_tree(dir_path, os.path.join(target_path, self.basename))
        return True


def parse_arguments(argv):
    """Parses command line arguments.

    Args:
        argv: List of commandline arguments.

    Returns:
        Namespace object containing parsed arguments.
    """

    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawTextHelpFormatter)

    parser.add_argument('type',
                        type=PackageType,
                        choices=list(PackageType),
                        help='The type of package to create')

    parser.add_argument(
        'path', help='The path to the FW file or directory to be packaged.')

    parser.add_argument(
        '--board',
        help='The ChromeOS board in which this cust pack will be used.')

    parser.add_argument(
        '--revision',
        help='The next ebuild number for that board. If the current ebuild '
        'revision is r12, enter r13.')

    parser.add_argument('--upload',
                        default=False,
                        action='store_true',
                        help='upload file to GS bucket.')

    parser.add_argument('--keep-files',
                        default=False,
                        action='store_true',
                        help="Don't delete the tarball files in /tmp. Useful "
                        'for Partners. Googlers should not upload files '
                        'manually.')

    return parser.parse_args(argv[1:])


def main(argv):
    """Main function."""

    logging.basicConfig(level=logging.DEBUG)
    opts = parse_arguments(argv)
    if opts.type == PackageType.L850_MAIN_FW:
        fw_uploader = L850MainFw(opts.path, opts.upload)
    elif opts.type == PackageType.L850_OEM_FW:
        fw_uploader = L850OemFw(opts.path, opts.upload)
    elif opts.type == PackageType.L850_OEM_DIR_ONLY:
        if not opts.revision:
            logging.error('The ebuild revision is needed to pack it, since '
                          'the tarballs need to be unique.')
            return os.EX_USAGE
        if not opts.board:
            logging.error('Please enter the board name.')
            return os.EX_USAGE
        fw_uploader = L850OemDir(opts.path, opts.upload, opts.revision,
                                 opts.board)
    elif opts.type == PackageType.NL668_MAIN_FW:
        fw_uploader = NL668MainFw(opts.path, opts.upload)
    elif opts.type == PackageType.FM101_MAIN_FW:
        fw_uploader = FM101MainFw(opts.path, opts.upload)

    return fw_uploader.process_fw_and_upload(opts.keep_files)


if __name__ == '__main__':
    sys.exit(main(sys.argv))
