#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A tool to manage the fingerprint study."""

from __future__ import print_function

import argparse
import glob
import logging
import os
import shutil
import stat
import sys
import tempfile

import gnupg


# Study Directory/File Structure:
# <participant_id>/<group>/<finger_id>/<finger_id>_<capture_num>.<raw|fmi>.gpg
FILE_GLOB_GPG = '*/*/*/*.gpg'


def decrypt(private_key: str, private_key_pass: str, files: list) -> bool:
    """Decrypt the given file."""

    # Enable basic stdout logging for gnupg.
    h = logging.StreamHandler()
    l = logging.getLogger('gnupg')
    l.setLevel(logging.INFO)
    l.addHandler(h)

    with tempfile.TemporaryDirectory() as gnupghome:
        os.chmod(gnupghome, stat.S_IRWXU)
        # Creating this directory makes old gnupg versions happy.
        os.makedirs(f'{gnupghome}/private-keys-v1.d', mode=stat.S_IRWXU)

        gpg = gnupg.GPG(gnupghome=gnupghome,
                        verbose=False,
                        options=[
                            '--no-options',
                            '--no-default-recipient',
                            '--trust-model', 'always',
                        ])

        with open(private_key, mode='rb') as key_file:
            key_data = key_file.read()
            if gpg.import_keys(key_data).count != 1:
                raise Exception(f'Failed to import key {private_key}.')

        for file in files:
            file_parts = os.path.splitext(file)
            assert file_parts[1] == '.gpg'
            file_output = file_parts[0]
            print(f'Decrypting file {file} to {file_output}.')
            with open(file, mode='rb') as file_input_stream:
                ret = gpg.decrypt_file(file_input_stream,
                                       always_trust=True,
                                       passphrase=private_key_pass,
                                       output=file_output)
                if not ret.ok:
                    raise Exception(f'Failed to decrypt file {file}.')

                if not os.path.exists(file_output):
                    raise Exception(
                        f'Output file {file_output} was not created.'
                    )

        # Shred all remnants GPG keys in the temp directory.
        os.system(f'find {gnupghome} -type f | xargs shred -v')
    return True


def cmd_decrypt(args: argparse.Namespace) -> int:
    """Handle the subcommand decrypt."""
    if not os.path.isfile(args.key):
        print(f'Error - The given key file {args.key} does not exist.')
        return 1

    if not os.path.isdir(args.dir):
        print(f'Error - The given dir path {args.dir} is not a directory.')
        return 1

    files = glob.glob(args.dir + '/' + FILE_GLOB_GPG)
    if not files:
        print('Error - The given dir path does not contain encrypted files.')
        return 1

    if not shutil.which('shred'):
        print('Error - The shred utility does not exist.')
        return 1

    if not decrypt(args.key, args.password, files):
        return 1


def main(argv: list) -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(
        dest='subcommand', required=True, title='subcommands')

    # Parser for "decrypt" subcommand.
    parser_decrypt = subparsers.add_parser('decrypt')
    parser_decrypt.add_argument('key', help='Path to the GPG private key')
    parser_decrypt.add_argument(
        'dir', help='Path to directory of encrypted captures')
    parser_decrypt.add_argument('--password', default=None,
                                help='Password for private key')
    parser_decrypt.set_defaults(func=cmd_decrypt)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
