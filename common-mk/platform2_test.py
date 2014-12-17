#!/usr/bin/env python
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Wrapper for running platform2 tests.

This handles the fun details like running against the right sysroot, via
qemu, bind mounts, etc...
"""

from __future__ import print_function

import argparse
import array
import contextlib
import errno
import os
import sys
import tempfile

from platform2 import Platform2

from chromite.lib import namespaces
from chromite.lib import osutils
from chromite.lib import retry_util
from chromite.lib import signals


# Env vars to let through to the test env when we do sudo.
ENV_PASSTHRU = (
    # Used by various sanitizers.
    'ASAN_OPTIONS',
    'LSAN_OPTIONS',
)


class Qemu(object):
  """Framework for running tests via qemu"""

  # The binfmt register format looks like:
  # :name:type:offset:magic:mask:interpreter:flags
  _REGISTER_FORMAT = r':%(name)s:M::%(magic)s:%(mask)s:%(interp)s:%(flags)s'

  # Require enough data to read the Ehdr of the ELF.
  _MIN_ELF_LEN = 64

  # Tuples of (magic, mask) for an arch.  Most only need to identify by the Ehdr
  # fields: e_ident (16 bytes), e_type (2 bytes), e_machine (2 bytes).
  _MAGIC_MASK = {
      'aarch64':
          (r'\x7f\x45\x4c\x46\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x02\x00\xb7\x00',
           r'\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff'
           r'\xfe\xff\xff\xff'),
      'alpha':
          (r'\x7f\x45\x4c\x46\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x02\x00\x26\x90',
           r'\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff'
           r'\xfe\xff\xff\xff'),
      'arm':
          (r'\x7f\x45\x4c\x46\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x02\x00\x28\x00',
           r'\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff'
           r'\xfe\xff\xff\xff'),
      'armeb':
          (r'\x7f\x45\x4c\x46\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x02\x00\x28',
           r'\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff'
           r'\xff\xfe\xff\xff'),
      'm68k':
          (r'\x7f\x45\x4c\x46\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x02\x00\x04',
           r'\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff'
           r'\xff\xfe\xff\xff'),
      # For mips targets, we need to scan e_flags.  But first we have to skip:
      # e_version (4 bytes), e_entry/e_phoff/e_shoff (4 or 8 bytes).
      'mips':
          (r'\x7f\x45\x4c\x46\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x02\x00\x08\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x00\x00\x00\x00\x00\x10\x00',
           r'\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff'
           r'\xff\xfe\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x00\x00\x00\x00\x00\xf0\x20'),
      'mipsel':
          (r'\x7f\x45\x4c\x46\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x02\x00\x08\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x00\x00\x00\x00\x10\x00\x00',
           r'\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff'
           r'\xfe\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x00\x00\x00\x20\xf0\x00\x00'),
      'mipsn32':
          (r'\x7f\x45\x4c\x46\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x02\x00\x08\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x00\x00\x00\x00\x00\x00\x20',
           r'\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff'
           r'\xff\xfe\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x00\x00\x00\x00\x00\xf0\x20'),
      'mipsn32el':
          (r'\x7f\x45\x4c\x46\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x02\x00\x08\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x00\x00\x00\x20\x00\x00\x00',
           r'\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff'
           r'\xfe\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x00\x00\x00\x20\xf0\x00\x00'),
      'mips64':
          (r'\x7f\x45\x4c\x46\x02\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x02\x00\x08',
           r'\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff'
           r'\xff\xfe\xff\xff'),
      'mips64el':
          (r'\x7f\x45\x4c\x46\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x02\x00\x08\x00',
           r'\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff'
           r'\xfe\xff\xff\xff'),
      'ppc':
          (r'\x7f\x45\x4c\x46\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x02\x00\x14',
           r'\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff'
           r'\xff\xfe\xff\xff'),
      'sparc':
          (r'\x7f\x45\x4c\x46\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x02\x00\x12',
           r'\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff'
           r'\xff\xfe\xff\xff'),
      'sparc64':
          (r'\x7f\x45\x4c\x46\x02\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x02\x00\x2b',
           r'\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff'
           r'\xff\xfe\xff\xff'),
      's390x':
          (r'\x7f\x45\x4c\x46\x02\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x02\x00\x16',
           r'\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff'
           r'\xff\xfe\xff\xff'),
      'sh4':
          (r'\x7f\x45\x4c\x46\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x02\x00\x2a\x00',
           r'\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff'
           r'\xfe\xff\xff\xff'),
      'sh4eb':
          (r'\x7f\x45\x4c\x46\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
           r'\x00\x02\x00\x2a',
           r'\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff'
           r'\xff\xfe\xff\xff'),
  }

  _BINFMT_PATH = '/proc/sys/fs/binfmt_misc'
  _BINFMT_REGISTER_PATH = os.path.join(_BINFMT_PATH, 'register')

  def __init__(self, sysroot, arch=None):
    if arch is None:
      arch = self.DetectArch(None, sysroot)
    self.arch = arch

    self.sysroot = sysroot

    self.name = 'qemu-%s' % self.arch
    self.build_path = os.path.join('/build', 'bin', self.name)
    self.binfmt_path = os.path.join(self._BINFMT_PATH, self.name)

  @classmethod
  def DetectArch(cls, prog, sysroot):
    """Figure out which qemu wrapper is best for this target"""
    def MaskMatches(bheader, bmagic, bmask):
      """Apply |bmask| to |bheader| and see if it matches |bmagic|

      The |bheader| array may be longer than the |bmask|; in which case we
      will only compare the number of bytes that |bmask| takes up.
      """
      # This algo is what the kernel uses.
      return all(((header_byte ^ magic_byte) & mask_byte) == 0x00
                 for header_byte, magic_byte, mask_byte in
                 zip(bheader[0:len(bmask)], bmagic, bmask))

    if prog is None:
      # Common when doing a global setup.
      prog = '/'

    for path in (prog, '/sbin/ldconfig', '/bin/sh', '/bin/dash', '/bin/bash'):
      path = os.path.join(sysroot, path.lstrip('/'))
      if os.path.islink(path) or not os.path.isfile(path):
        continue

      # Read the header of the ELF first.
      matched_arch = None
      with open(path, 'rb') as f:
        header = f.read(cls._MIN_ELF_LEN)
        if len(header) == cls._MIN_ELF_LEN:
          bheader = array.array('B', header)

          # Walk all the magics and see if any of them match this ELF.
          for arch, magic_mask in cls._MAGIC_MASK.items():
            magic = magic_mask[0].decode('string_escape')
            bmagic = array.array('B', magic)
            mask = magic_mask[1].decode('string_escape')
            bmask = array.array('B', mask)

            if MaskMatches(bheader, bmagic, bmask):
              # Make sure we do not have ambiguous magics as this will
              # also confuse the kernel when it tries to find a match.
              if not matched_arch is None:
                raise ValueError('internal error: multiple masks matched '
                                 '(%s & %s)' % (matched_arch, arch))
              matched_arch = arch

      if not matched_arch is None:
        return matched_arch

  @staticmethod
  def inode(path):
    """Return the inode for |path| (or -1 if it doesn't exist)"""
    try:
      return os.stat(path).st_ino
    except OSError as e:
      if e.errno == errno.ENOENT:
        return -1
      raise

  def Install(self, sysroot=None):
    """Install qemu into |sysroot| safely"""
    if sysroot is None:
      sysroot = self.sysroot

    # Copying strategy:
    # Compare /usr/bin/qemu inode to /build/$board/build/bin/qemu; if
    # different, hard link to a temporary file, then rename temp to target.
    # This should ensure that once $QEMU_SYSROOT_PATH exists it will always
    # exist, regardless of simultaneous test setups.
    paths = (
        ('/usr/bin/%s' % self.name,
         sysroot + self.build_path),
        ('/usr/bin/qemu-binfmt-wrapper',
         sysroot + self.build_path + '-binfmt-wrapper'),
    )

    for src_path, sysroot_path in paths:
      src_path = os.path.normpath(src_path)
      sysroot_path = os.path.normpath(sysroot_path)
      if self.inode(sysroot_path) != self.inode(src_path):
        # Use hardlinks so that the process is atomic.
        temp_path = '%s.%s' % (sysroot_path, os.getpid())
        os.link(src_path, temp_path)
        os.rename(temp_path, sysroot_path)
        # Clear out the temp path in case it exists (another process already
        # swooped in and created the target link for us).
        try:
          os.unlink(temp_path)
        except OSError as e:
          if e.errno != errno.ENOENT:
            raise

  @classmethod
  def GetRegisterBinfmtStr(cls, arch, name, interp):
    """Get the string used to pass to the kernel for registering the format

    Args:
      arch: The architecture to get the register string
      name: The name to use for registering
      interp: The name for the interpreter

    Returns:
      A string ready to pass to the register file
    """
    magic, mask = cls._MAGIC_MASK[arch]

    # We need to decode the escape sequences as the kernel has a limit on
    # the register string (256 bytes!).  However, we can't decode two chars:
    # NUL bytes (since the kernel uses strchr and friends) and colon bytes
    # (since we use that as the field separator).
    # TODO: Once this lands, and we drop support for older kernels, we can
    # probably drop this workaround too.  https://lkml.org/lkml/2014/9/1/181
    magic = magic.decode('string_escape')
    mask = mask.decode('string_escape')

    # Further way of data packing: if the mask and magic use 0x00 for the same
    # byte, then turn the magic into something else.  This way the magic can
    # be written in raw form, but the mask will still cancel it out.
    magic = ''.join([
        '!' if (magic_byte == '\x00' and mask_byte == '\x00') else magic_byte
        for magic_byte, mask_byte in zip(magic, mask)
    ])

    # New repack the bytes.
    def _SemiEncode(s):
      return s.replace('\x00', r'\x00').replace(':', '\x3a')
    magic = _SemiEncode(magic)
    mask = _SemiEncode(mask)

    return cls._REGISTER_FORMAT % {
        'name': name,
        'magic': magic,
        'mask': mask,
        'interp': '%s-binfmt-wrapper' % interp,
        'flags': 'POC',
    }

  def RegisterBinfmt(self):
    """Make sure qemu has been registered as a format handler

    Prep the binfmt handler. First mount if needed, then unregister any bad
    mappings, and then register our mapping.

    There may still be some race conditions here where one script
    de-registers and another script starts executing before it gets
    re-registered, however it should be rare.
    """
    if not os.path.exists(self._BINFMT_REGISTER_PATH):
      osutils.Mount('binfmt_misc', self._BINFMT_PATH, 'binfmt_misc', 0)

    if os.path.exists(self.binfmt_path):
      interp = 'interpreter %s\n' % self.build_path
      for line in osutils.ReadFile(self.binfmt_path):
        if line == interp:
          break
      else:
        osutils.WriteFile(self.binfmt_path, '-1')

    if not os.path.exists(self.binfmt_path):
      register = self.GetRegisterBinfmtStr(self.arch, self.name,
                                           self.build_path)
      try:
        osutils.WriteFile(self._BINFMT_REGISTER_PATH, register)
      except IOError:
        print('error: attempted to register: (len:%i) %s' %
              (len(register), register))
        raise


class Platform2Test(object):
  """Framework for running platform2 tests"""

  _BIND_MOUNT_PATHS = ('dev', 'dev/pts', 'proc', 'mnt/host/source', 'sys')

  def __init__(self, test_bin, board, host, use_flags, framework,
               run_as_root, gtest_filter, user_gtest_filter, cache_dir,
               sysroot, test_bin_args):
    if not test_bin_args:
      test_bin_args = [test_bin]
    if not test_bin:
      test_bin = test_bin_args[0]
    self.bin = test_bin
    self.args = test_bin_args
    self.board = board
    self.host = host
    self.use_flags = use_flags
    self.run_as_root = run_as_root
    (self.gtest_filter, self.user_gtest_filter) = \
        self.generateGtestFilter(gtest_filter, user_gtest_filter)

    p2 = Platform2(self.use_flags, self.board, self.host, cache_dir=cache_dir)
    if sysroot:
      self.sysroot = sysroot
    else:
      self.sysroot = p2.sysroot
    self.lib_dir = os.path.join(p2.get_products_path(), 'lib')

    self.framework = framework
    if self.framework == 'auto':
      qemu_arch = Qemu.DetectArch(self.bin, self.sysroot)
      if qemu_arch is None:
        self.framework = 'ldso'
      else:
        self.framework = 'qemu'

    if self.framework == 'qemu':
      self.qemu = Qemu(self.sysroot, arch=qemu_arch)

  @classmethod
  def generateGtestSubfilter(cls, gtest_filter):
    """Split a gtest_filter down into positive and negative filters.

    Args:
      gtest_filter: A filter string as normally passed to --gtest_filter.

    Returns:
      A tuple of format (positive_filters, negative_filters).
    """

    filters = gtest_filter.split('-', 1)
    positive_filters = [x for x in filters[0].split(':') if x]
    if len(filters) > 1:
      negative_filters = [x for x in filters[1].split(':') if x]
    else:
      negative_filters = []

    return (positive_filters, negative_filters)

  @classmethod
  def generateGtestFilter(cls, filters, user_filters):
    """Merge internal gtest filters and user-supplied gtest filters.

    Returns:
      A string that can be passed to --gtest_filter.
    """

    gtest_filter = cls.generateGtestSubfilter(filters)
    user_gtest_filter = []

    if user_filters:
      filters = user_filters.split('::')[-1]
      user_gtest_filter = cls.generateGtestSubfilter(filters)

    return (gtest_filter, user_gtest_filter)

  def removeSysrootPrefix(self, path):
    """Returns the given path with any sysroot prefix removed."""
    # If the sysroot is /, then the paths are already normalized.
    if self.sysroot != '/' and path.startswith(self.sysroot):
      path = path.replace(self.sysroot, '', 1)

    return path

  @staticmethod
  def GetNonRootAccount():
    """Return details about the non-root account we want to use.

    Returns:
      A tuple of (username, uid, gid, home).
    """
    return (
        os.environ.get('SUDO_USER', 'nobody'),
        int(os.environ.get('SUDO_UID', '65534')),
        int(os.environ.get('SUDO_GID', '65534')),
        # Should we find a better home?
        '/tmp/portage',
    )

  @staticmethod
  @contextlib.contextmanager
  def LockDb(db):
    """Lock an account database.

    We use the same algorithm as shadow/user.eclass.  This way we don't race
    and corrupt things in parallel.
    """
    lock = '%s.lock' % db
    _, tmplock = tempfile.mkstemp(prefix='%s.platform.' % lock)

    # First try forever to grab the lock.
    retry = lambda e: e.errno == errno.EEXIST
    # Retry quickly at first, but slow down over time.
    try:
      retry_util.GenericRetry(retry, 60, os.link, tmplock, lock, sleep=0.1)
    except Exception:
      print('error: could not grab lock %s' % lock)
      raise

    # Yield while holding the lock, but try to clean it no matter what.
    try:
      os.unlink(tmplock)
      yield lock
    finally:
      os.unlink(lock)

  def SetupUser(self):
    """Propogate the user name<->id mapping from outside the chroot.

    Some unittests use getpwnam($USER), as does bash.  If the account
    is not registered in the sysroot, they get back errors.
    """
    MAGIC_GECOS = 'Added by your friendly platform test helper; do not modify'
    # This is kept in sync with what sdk_lib/make_chroot.sh generates.
    SDK_GECOS = 'ChromeOS Developer'

    user, uid, gid, home = self.GetNonRootAccount()
    if user == 'nobody':
      return

    passwd_db = os.path.join(self.sysroot, 'etc', 'passwd')
    with self.LockDb(passwd_db):
      data = osutils.ReadFile(passwd_db)
      accts = data.splitlines()
      for acct in accts:
        passwd = acct.split(':')
        if passwd[0] == user:
          # Did the sdk make this account?
          if passwd[4] == SDK_GECOS:
            # Don't modify it (see below) since we didn't create it.
            return

          # Did we make this account?
          if passwd[4] != MAGIC_GECOS:
            raise RuntimeError('your passwd db (%s) has unmanaged acct %s' %
                               (passwd_db, user))

          # Maybe we should see if it needs to be updated?  Like if they
          # changed UIDs?  But we don't really check that elsewhere ...
          return

      acct = '%(name)s:x:%(uid)s:%(gid)s:%(gecos)s:%(homedir)s:%(shell)s' % {
          'name': user,
          'uid': uid,
          'gid': gid,
          'gecos': MAGIC_GECOS,
          'homedir': home,
          'shell': '/bin/bash',
      }
      with open(passwd_db, 'a') as f:
        if data[-1] != '\n':
          f.write('\n')
        f.write('%s\n' % acct)

  def pre_test(self):
    """Runs pre-test environment setup.

    Sets up any required mounts and copying any required files to run tests
    (not those specific to tests) into the sysroot.
    """
    if not self.run_as_root:
      self.SetupUser()

    if self.framework == 'qemu':
      self.qemu.Install()
      self.qemu.RegisterBinfmt()

  def post_test(self):
    """Runs post-test teardown, removes mounts/files copied during pre-test."""

  def use(self, use_flag):
    return use_flag in self.use_flags

  def run(self):
    """Runs the test in a proper environment (e.g. qemu)."""

    # We know these pre-tests are fast (especially if they've already been run
    # once), so run them automatically for the user if they test by hand.
    self.pre_test()

    if not self.use('cros_host'):
      for mount in self._BIND_MOUNT_PATHS:
        path = os.path.join(self.sysroot, mount)
        osutils.SafeMakedirs(path)
        osutils.Mount('/' + mount, path, 'none', osutils.MS_BIND)

    positive_filters = self.gtest_filter[0]
    negative_filters = self.gtest_filter[1]

    if self.user_gtest_filter:
      positive_filters += self.user_gtest_filter[0]
      negative_filters += self.user_gtest_filter[1]

    filters = (':'.join(positive_filters), ':'.join(negative_filters))
    gtest_filter = '%s-%s' % filters

    cmd = self.removeSysrootPrefix(self.bin)
    argv = self.args[:]
    argv[0] = self.removeSysrootPrefix(argv[0])
    if gtest_filter != '-':
      argv.append('--gtest_filter=' + gtest_filter)

    # Some programs expect to find data files via $CWD, so doing a chroot
    # and dropping them into / would make them fail.
    cwd = self.removeSysrootPrefix(os.getcwd())

    # Fork off a child to run the test.  This way we can make tweaks to the
    # env that only affect the child (gid/uid/chroot/cwd/etc...).  We have
    # to fork anyways to run the test, so might as well do it all ourselves
    # to avoid (slow) chaining through programs like:
    #   sudo -u $SUDO_UID -g $SUDO_GID chroot $SYSROOT bash -c 'cd $CWD; $BIN'
    child = os.fork()
    if child == 0:
      print('chroot: %s' % self.sysroot)
      print('cwd: %s' % cwd)
      print('cmd: {%s} %s' % (cmd, ' '.join(map(repr, argv))))
      os.chroot(self.sysroot)
      os.chdir(cwd)
      # The TERM the user is leveraging might not exist in the sysroot.
      # Force a sane default that supports standard color sequences.
      os.environ['TERM'] = 'ansi'
      # Some progs want this like bash else they get super confused.
      os.environ['PWD'] = cwd
      if not self.run_as_root:
        _, uid, gid, home = self.GetNonRootAccount()
        os.setgid(gid)
        os.setuid(uid)
        os.environ['HOME'] = home
      sys.exit(os.execv(cmd, argv))

    _, status = os.waitpid(child, 0)
    if status:
      exit_status, sig = status >> 8, status & 0xff
      raise AssertionError('Error running test binary %s: exit:%i signal:%s(%i)'
                           % (cmd, exit_status,
                              signals.StrSignal(sig & 0x7f), sig))


def _SudoCommand():
  """Get the 'sudo' command, along with all needed environment variables."""
  cmd = ['sudo']
  for key in ENV_PASSTHRU:
    value = os.environ.get(key)
    if value is not None:
      cmd += ['%s=%s' % (key, value)]
  return cmd


def _ReExecuteIfNeeded(argv):
  """Re-execute tests as root.

  We often need to do things as root, so make sure we're that.  Like chroot
  for proper library environment or do bind mounts.

  Also unshare the mount namespace so as to ensure that doing bind mounts for
  tests don't leak out to the normal chroot.  Also unshare the UTS namespace
  so changes to `hostname` do not impact the host.
  """
  # Disable the Gentoo sandbox if it's active to avoid warnings/errors.
  if os.environ.get('SANDBOX_ON') == '1':
    os.environ['SANDBOX_ON'] = '0'
    os.execvp(argv[0], argv)
  elif os.geteuid() != 0:
    # Clear the LD_PRELOAD var since it won't be usable w/sudo (and the Gentoo
    # sandbox normally sets it for us).
    os.environ.pop('LD_PRELOAD', None)
    cmd = _SudoCommand() + ['--'] + argv
    os.execvp(cmd[0], cmd)
  else:
    namespaces.SimpleUnshare(net=True, pid=True)


class _ParseStringSetAction(argparse.Action):
  """Support flags that store into a set (vs a list)."""

  def __call__(self, parser, namespace, values, option_string=None):
    setattr(namespace, self.dest, set(values.split()))


def main(argv):
  actions = ['pre_test', 'post_test', 'run']

  parser = argparse.ArgumentParser()
  parser.add_argument('--action', default='run',
                      choices=actions, help='action to perform')
  parser.add_argument('--bin',
                      help='test binary to run')
  parser.add_argument('--board', default=None,
                      help='board to build for')
  parser.add_argument('--sysroot', default=None,
                      help='sysroot to run tests inside')
  parser.add_argument('--cache_dir',
                      default='var/cache/portage/chromeos-base/platform2',
                      help='directory to use as cache for incremental build')
  parser.add_argument('--framework', default='auto',
                      choices=('auto', 'ldso', 'qemu'),
                      help='framework to be used to run tests')
  parser.add_argument('--gtest_filter', default='',
                      help='args to pass to gtest/test binary')
  parser.add_argument('--host', action='store_true', default=False,
                      help='specify that we\'re testing for the host')
  parser.add_argument('--run_as_root', action='store_true',
                      help='should the test be run as root')
  parser.add_argument('--use_flags', default=set(),
                      action=_ParseStringSetAction,
                      help='USE flags to enable')
  parser.add_argument('--user_gtest_filter', default='',
                      help=argparse.SUPPRESS)
  parser.add_argument('cmdline', nargs='*')

  options = parser.parse_args(argv)

  if options.action == 'run' and ((not options.bin or len(options.bin) == 0)
                                  and not options.cmdline):
    raise AssertionError('You must specify a binary for the "run" action')

  if options.host and options.board:
    raise AssertionError('You must provide only one of --board or --host')
  elif not options.host and not options.board and not options.sysroot:
    raise AssertionError('You must provide --board or --host or --sysroot')

  if options.sysroot:
    # Normalize the value so we can assume certain aspects.
    options.sysroot = osutils.ExpandPath(options.sysroot)
    if not os.path.isdir(options.sysroot):
      raise AssertionError('Sysroot does not exist: %s' % options.sysroot)

  # Once we've finished sanity checking args, make sure we're root.
  _ReExecuteIfNeeded([sys.argv[0]] + argv)

  p2test = Platform2Test(options.bin, options.board, options.host,
                         options.use_flags, options.framework,
                         options.run_as_root, options.gtest_filter,
                         options.user_gtest_filter, options.cache_dir,
                         options.sysroot, options.cmdline)
  getattr(p2test, options.action)()


if __name__ == '__main__':
  main(sys.argv[1:])
