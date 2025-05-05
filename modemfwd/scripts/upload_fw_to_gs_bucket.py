#!/usr/bin/env python3
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Create tarballs with modem FW, and upload them to OS Archive Mirrors."""

import argparse
import distutils
import enum
import logging
import os
import re
import shutil
import subprocess
import sys
import tempfile


class PackageType(enum.Enum):
  """Packaging options for different firmwares or cust packs."""

  L850_MAIN_FW = "l850-main-fw"
  L850_OEM_FW = "l850-oem-fw"
  L850_OEM_DIR_ONLY = "l850-oem-dir"
  # NL668 firmware payloads
  NL668_MAIN_FW = "nl668-main-fw"
  # FM101 firmware payloads
  FM101_MAIN_FW = "fm101-main-fw"
  FM101_AP_FW = "fm101-ap-fw"
  FM101_DEV_FW = "fm101-dev-fw"
  FM101_OEM_FW = "fm101-oem-fw"
  FM101_OP_FW = "fm101-op-fw"
  FM101_RECOVERY_FW = "fm101-recovery-fw"
  # RW101 firmware payloads
  RW101_MAIN_FW = "rw101-main-fw"
  RW101_AP_FW = "rw101-ap-fw"
  RW101_DEV_FW = "rw101-dev-fw"
  RW101_OEM_FW = "rw101-oem-fw"
  RW101_OP_FW = "rw101-op-fw"
  RW101_RECOVERY_FW = "rw101-recovery-fw"
  # RW135 firmware payloads
  RW135_MAIN_FW = "rw135-main-fw"
  RW135_AP_FW = "rw135-ap-fw"
  RW135_DEV_FW = "rw135-dev-fw"
  RW135_OEM_FW = "rw135-oem-fw"
  RW135_OP_FW = "rw135-op-fw"
  RW135_RECOVERY_FW = "rw135-recovery-fw"
  # EM060 firmware payloads
  EM060_FW = "em060-fw"
  # LCUK54 firmware payloads
  LCUK54_FW = "lcuk54-fw"

  # FM350 firmware payloads
  FM350_MAIN_FW = "fm350-main-fw"  # 81600... directory
  FM350_AP_FW = "fm350-ap-fw"  # FM350... directory
  FM350_DEV_FW = "fm350-dev-fw"  # DEV_OTA file
  FM350_OEM_FW = "fm350-oem-fw"  # OEM_OTA file
  FM350_CARRIER_FW = "fm350-carrier-fw"  # OP_OTA file

  def __str__(self):
    return str(self.value)


MIRROR_PATH = "gs://chromeos-localmirror/distfiles/"
FIBOCOM_TARBALL_PREFIX = "cellular-firmware-fibocom-"
ROLLING_TARBALL_PREFIX = "cellular-firmware-rolling-"
QUECTEL_TARBALL_PREFIX = "cellular-firmware-quectel-"
NETPRISMA_TARBALL_PREFIX = "cellular-firmware-netprisma-"
L850_TARBALL_PREFIX = FIBOCOM_TARBALL_PREFIX + "l850-"
NL668_TARBALL_PREFIX = FIBOCOM_TARBALL_PREFIX + "nl668-"
FM101_TARBALL_PREFIX = FIBOCOM_TARBALL_PREFIX + "fm101-"
RW101_TARBALL_PREFIX = ROLLING_TARBALL_PREFIX + "rw101-"
RW135_TARBALL_PREFIX = ROLLING_TARBALL_PREFIX + "rw135-"
FM350_TARBALL_PREFIX = FIBOCOM_TARBALL_PREFIX + "fm350-"
EM060_TARBALL_PREFIX = QUECTEL_TARBALL_PREFIX + "em060-"
LCUK54_TARBALL_PREFIX = NETPRISMA_TARBALL_PREFIX + "lcuk54-"

FM350_MISC_PREFIXES = ["OEM_OTA_", "DEV_OTA_", "OP_OTA_"]
FM101_MISC_PREFIXES = ["OEM_OTA_", "DEV_OTA_", "OP_OTA_"]
RW101_MISC_PREFIXES = ["OEM_OTA_", "DEV_OTA_", "OP_OTA_"]
RW135_MISC_PREFIXES = ["OEM_OTA_", "DEV_OTA_", "OP_OTA_"]

OEM_FW_PREFIX = "OEM_cust."
OEM_FW_POSTFIX = "_signed.fls3.xz"


class TempDir:
  """Context manager to make sure temporary directories are cleaned up."""

  def __init__(self, keep_tmp_files):
    self._keep_tmp_files = keep_tmp_files
    self._tempdir = None

  def __enter__(self):
    self._tempdir = tempfile.mkdtemp()
    return self._tempdir

  def __exit__(self, exc_type, exc_val, exc_tb):
    if not self._keep_tmp_files:
      logging.info("Removing temporary files")
      shutil.rmtree(self._tempdir)
    return False


class FwUploader:
  """Class to verify the files and upload the tarball to a gs bucket."""

  def __init__(self, path, upload, tarball_postfix, tarball_dir_name):
    self.path = os.path.abspath(path)
    self.upload = upload
    self.basename = os.path.basename(self.path)
    self.tarball_dir_name = tarball_dir_name
    self.tarball_postfix = tarball_postfix

  def process_fw_and_upload(self, keep_tmp_files):
    if not self.validate():
      return os.EX_USAGE

    tarball_dir_name = self.tarball_dir_name
    if self.tarball_postfix:
        tarball_dir_name += "-" + self.tarball_postfix

    with TempDir(keep_tmp_files) as tempdir:
      path_to_package = os.path.join(tempdir, tarball_dir_name)
      os.mkdir(path_to_package)

      if not self.prepare_files(self.path, path_to_package):
        logging.error("Failed to prepare files for packaging")
        return os.EX_OSFILE

      os.chdir(tempdir)
      tarball_name = f"{tarball_dir_name}.tar.xz"
      subprocess.run(
          [
              "tar",
              "-Ipixz",
              "-cf",
              f"{tarball_name}",
              f"{tarball_dir_name}/",
          ],
          stderr=subprocess.DEVNULL,
          check=True,
      )
      tarball_path = os.path.join(tempdir, tarball_name)
      logging.info("Tarball created: %s", tarball_path)

      gs_bucket_path = os.path.join(MIRROR_PATH, tarball_name)
      if self.upload:
        logging.info("Uploading file %s to %s", tarball_path, gs_bucket_path)
        subprocess.run(
            [
                "gsutil",
                "cp",
                "-n",
                "-a",
                "public-read",
                f"{tarball_path}",
                f"{gs_bucket_path}",
            ],
            stderr=subprocess.DEVNULL,
            check=True,
        )
        logging.info("Setting ACLs on %s", gs_bucket_path)
        subprocess.run(
            [
                "gsutil",
                "acl",
                "ch",
                "-g",
                "mdb.croscellular@google.com:O",
                f"{gs_bucket_path}",
            ],
            stderr=subprocess.DEVNULL,
            check=True,
        )
      else:
        logging.info(
            "Use --upload flag to upload file %s to %s",
            tarball_path,
            gs_bucket_path,
        )

    return os.EX_OK


class L850MainFw(FwUploader):
  """Uploader class for L850GL main FW."""

  def __init__(self, path, upload, tarball_postfix):
    super().__init__(path, upload, tarball_postfix, None)
    self.tarball_dir_name = L850_TARBALL_PREFIX + self.basename.replace(
        ".fls3.xz", ""
    )

  def validate(self):
    main_fw_postfix = "Secureboot.fls3.xz"
    if not self.path.endswith(main_fw_postfix):
      logging.error(
          "The main FW file `%s` name does not match `*%s`",
          self.path,
          main_fw_postfix,
      )
      return False
    return True

  @staticmethod
  def prepare_files(fw_path, target_path):
    logging.info("Copying %s into %s", fw_path, target_path)
    shutil.copy(fw_path, target_path)
    return True


class L850OemFw(FwUploader):
  """Uploader class for L850GL OEM FW."""

  def __init__(self, path, upload, tarball_postfix):
    super().__init__(path, upload, tarball_postfix, None)
    self.tarball_dir_name = (
        f"{L850_TARBALL_PREFIX}"
        + f'[{self.basename.replace(OEM_FW_POSTFIX, "")}]'
    )

  def validate(self):
    if not (
        self.basename.startswith(OEM_FW_PREFIX)
        and self.basename.endswith(OEM_FW_POSTFIX)
    ):
      logging.error(
          "The OEM FW file `%s` name does not match `%s*%s`",
          self.basename,
          OEM_FW_PREFIX,
          OEM_FW_POSTFIX,
      )
      return False
    return True

  @staticmethod
  def prepare_files(fw_path, target_path):
    logging.info("Copying %s into %s", fw_path, target_path)
    shutil.copy(fw_path, target_path)
    return True


class L850OemDir(FwUploader):
  """Uploader class for L850GL cust packs directory."""

  def __init__(self, path, upload, tarball_postfix, revision, board):
    super().__init__(path, upload, tarball_postfix, None)
    self.tarball_dir_name = (
        f"{L850_TARBALL_PREFIX}{board}"
        + f"-carriers_OEM_{self.basename}-{revision}"
    )
    self.revision = revision

  def validate(self):
    if not self.revision.startswith("r") or not self.revision[1:].isdigit():
      logging.error("The revision should be in the form of r##")
      return False
    if len(self.basename) != 4 or not self.basename.isdigit():
      logging.error(
          "The OEM carrier directory name is expected to consist of 4 digits"
      )
      return False
    return True

  def prepare_files(self, dir_path, target_path):
    logging.info("Copying %s into %s", dir_path, target_path)
    os.mkdir(os.path.join(target_path, self.basename))
    distutils.dir_util.copy_tree(
        dir_path, os.path.join(target_path, self.basename)
    )

    return True


class NL668MainFw(FwUploader):
  """Uploader class for NL668 main FW."""

  def __init__(self, path, upload, tarball_postfix):
    super().__init__(path, upload, tarball_postfix, None)
    self.tarball_dir_name = NL668_TARBALL_PREFIX + self.basename

  def validate(self):
    if not os.path.isdir(self.path):
      logging.error("The NL668 FW should be a directory")
      return False
    return True

  def prepare_files(self, dir_path, target_path):
    logging.info("Copying %s into %s", dir_path, target_path)
    os.mkdir(os.path.join(target_path, self.basename))
    distutils.dir_util.copy_tree(
        dir_path, os.path.join(target_path, self.basename)
    )
    return True


class FM101Fw(FwUploader):
  """Uploader class for FM101 main FW."""

  def __init__(self, path, upload, tarball_postfix):
    super().__init__(path, upload, tarball_postfix, None)
    self.tarball_dir_name = FM101_TARBALL_PREFIX + self.basename

  def validate(self):
    if not os.path.isdir(self.path):
      logging.error("The FM101 FW should be a directory")
      return False
    return True

  def prepare_files(self, dir_path, target_path):
    logging.info("Copying %s into %s", dir_path, target_path)
    os.mkdir(os.path.join(target_path, self.basename))
    distutils.dir_util.copy_tree(
        dir_path, os.path.join(target_path, self.basename)
    )
    return True

class FM101RecoveryFw(FwUploader):
  """Uploader class for FM101 recovery FW."""

  def __init__(self, path, upload, tarball_postfix, main_fw_version, ap_fw_version, extra_ap_fw_version):
    super().__init__(path, upload, tarball_postfix, None)
    if not main_fw_version or not ap_fw_version:
      logging.error("The main, and AP FW versions are required for FM101 recovery FW")
      return os.EX_USAGE
    self.main_fw_version = main_fw_version
    self.ap_fw_version = ap_fw_version
    self.extra_ap_fw_version = extra_ap_fw_version
    self.tarball_dir_name = FM101_TARBALL_PREFIX + self.basename + "-" + self.main_fw_version + "-" + self.ap_fw_version
    if self.extra_ap_fw_version:
      self.tarball_dir_name += "-" + self.extra_ap_fw_version

  def validate(self):
    if not os.path.isdir(self.path):
      logging.error("The FM101 recovery FW should be a directory")
      return False
    return True

  def prepare_files(self, dir_path, target_path):
    logging.info("Copying %s into %s", dir_path, target_path)
    os.mkdir(os.path.join(target_path, self.basename))
    distutils.dir_util.copy_tree(
        dir_path, os.path.join(target_path, self.basename)
    )
    return True

class RW101Fw(FwUploader):
  """Uploader class for RW101 main FW."""

  def __init__(self, path, upload, tarball_postfix):
    super().__init__(path, upload, tarball_postfix, None)
    self.tarball_dir_name = RW101_TARBALL_PREFIX + self.basename

  def validate(self):
    if not os.path.isdir(self.path):
      logging.error("The RW101 FW should be a directory")
      return False
    return True

  def prepare_files(self, dir_path, target_path):
    logging.info("Copying %s into %s", dir_path, target_path)
    os.mkdir(os.path.join(target_path, self.basename))
    distutils.dir_util.copy_tree(
        dir_path, os.path.join(target_path, self.basename)
    )
    return True

class RW101RecoveryFw(FwUploader):
  """Uploader class for RW101 recovery FW."""

  def __init__(self, path, upload, tarball_postfix, main_fw_version, ap_fw_version, extra_ap_fw_version):
    super().__init__(path, upload, tarball_postfix, None)
    if not main_fw_version or not ap_fw_version:
      logging.error("The main, and AP FW versions are required for RW101 recovery FW")
      return os.EX_USAGE
    self.main_fw_version = main_fw_version
    self.ap_fw_version = ap_fw_version
    self.extra_ap_fw_version = extra_ap_fw_version
    self.tarball_dir_name = RW101_TARBALL_PREFIX + self.basename + "-" + self.main_fw_version + "-" + self.ap_fw_version
    if self.extra_ap_fw_version:
      self.tarball_dir_name += "-" + self.extra_ap_fw_version

  def validate(self):
    if not os.path.isdir(self.path):
      logging.error("The RW101 recovery FW should be a directory")
      return False
    return True

  def prepare_files(self, dir_path, target_path):
    logging.info("Copying %s into %s", dir_path, target_path)
    os.mkdir(os.path.join(target_path, self.basename))
    distutils.dir_util.copy_tree(
        dir_path, os.path.join(target_path, self.basename)
    )
    return True

class RW135Fw(FwUploader):
  """Uploader class for RW135 main FW."""

  def __init__(self, path, upload, tarball_postfix):
    super().__init__(path, upload, tarball_postfix, None)
    self.tarball_dir_name = RW135_TARBALL_PREFIX + self.basename

  def validate(self):
    if not os.path.isdir(self.path):
      logging.error("The RW135 FW should be a directory")
      return False
    return True

  def prepare_files(self, dir_path, target_path):
    logging.info("Copying %s into %s", dir_path, target_path)
    os.mkdir(os.path.join(target_path, self.basename))
    distutils.dir_util.copy_tree(
        dir_path, os.path.join(target_path, self.basename)
    )
    return True

class RW135RecoveryFw(FwUploader):
  """Uploader class for RW135 recovery FW."""

  def __init__(self, path, upload, tarball_postfix, main_fw_version, ap_fw_version, extra_ap_fw_version):
    super().__init__(path, upload, tarball_postfix, None)
    if not main_fw_version or not ap_fw_version:
      logging.error("The main, and AP FW versions are required for RW135 recovery FW")
      return os.EX_USAGE
    self.main_fw_version = main_fw_version
    self.ap_fw_version = ap_fw_version
    self.extra_ap_fw_version = extra_ap_fw_version
    self.tarball_dir_name = RW135_TARBALL_PREFIX + self.basename + "-" + self.main_fw_version + "-" + self.ap_fw_version
    if self.extra_ap_fw_version:
      self.tarball_dir_name += "-" + self.extra_ap_fw_version

  def validate(self):
    if not os.path.isdir(self.path):
      logging.error("The RW135 recovery FW should be a directory")
      return False
    return True

  def prepare_files(self, dir_path, target_path):
    logging.info("Copying %s into %s", dir_path, target_path)
    os.mkdir(os.path.join(target_path, self.basename))
    distutils.dir_util.copy_tree(
        dir_path, os.path.join(target_path, self.basename)
    )
    return True



class FM350MainFw(FwUploader):
  """Uploader class for FM350 main FW.

  This should be used for both main and AP firmware payloads.
  """

  def __init__(self, path, upload, tarball_postfix):
    super().__init__(path, upload, tarball_postfix, None)
    self.tarball_dir_name = FM350_TARBALL_PREFIX + self.basename

  def validate(self):
    if not os.path.isdir(self.path):
      logging.error("The FM350 FW should be a directory")
      return False
    return True

  def prepare_files(self, dir_path, target_path):
    logging.info("Copying %s into %s", dir_path, target_path)
    os.mkdir(os.path.join(target_path, self.basename))
    distutils.dir_util.copy_tree(
        dir_path, os.path.join(target_path, self.basename)
    )
    return True


class FM350MiscFw(FwUploader):
  """Uploader class for FM350 non-main payloads.

  This should be used for OEM_OTA, DEV_OTA, and OP_OTA payloads.
  """

  def __init__(self, path, upload, tarball_postfix):
    super().__init__(path, upload, tarball_postfix, None)
    self.tarball_dir_name = FM350_TARBALL_PREFIX + self.basename

  def validate(self):
    if os.path.isdir(self.path):
      logging.error("Misc FM350 FW should not be a directory")
      return False
    if not any(
        self.basename.startswith(prefix) for prefix in FM350_MISC_PREFIXES
    ):
      logging.error(
          "Expected non-main payload to begin with one of %s",
          FM350_MISC_PREFIXES,
      )
      return False

    return True

  @staticmethod
  def prepare_files(fw_path, target_path):
    logging.info("Copying %s into %s", fw_path, target_path)
    shutil.copy(fw_path, target_path)
    return True


class EM060DerivFw(FwUploader):
  """Uploader class for EM060 and derivative payloads.

  This should be used for oem.bin, main.bin, and carrier.bin payloads.

  The expectation is that the .bin file is sitting in a parent directory
  whose name is the firmware version, for instance "01.300/main.bin". The
  upload_fw_to_gs_bucket script should be provided the path to the .bin file
  as path argument.
  """

  def __init__(self, path, upload, tarball_postfix, module_type):
    super().__init__(path, upload, tarball_postfix, None)

    self.firmware_version = None
    self.payload_type = None
    self.tarball_prefix = None
    self.module_type = module_type

  def validate(self):
    fw_file_to_directory_name = {
        "oem.bin": "OEM",
        "main.bin": "MAIN",
        "carrier.bin": "CARRIER",
    }
    required_recovery_files = [
        "prog_nand_firehose_9x55.mbn",
        "rawprogram_nand_p2K_b128K_recovery.xml",
        "sbl1_recovery.mbn",
    ]
    module_type_to_tarball_prefix = {
        PackageType.EM060_FW: EM060_TARBALL_PREFIX,
        PackageType.LCUK54_FW: LCUK54_TARBALL_PREFIX,
    }

    if os.path.isdir(self.path):
      logging.error("Path should point to a firmware file")
      return False
    if self.basename not in fw_file_to_directory_name.keys():
      logging.error("File should have name {main,carrier,oem}.bin")
      return False

    optional_recovery_infix = "-"
    if "oem.bin" in self.basename:
      optional_recovery_infix = "-with-recovery-"
      files_present = os.listdir(os.path.dirname(self.path))
      for recovery_file in required_recovery_files:
        if recovery_file not in files_present:
          logging.error("Recovery file %s missing", recovery_file)
          return False

    # Grab package version from parent directory name, i.e. "01.300"
    parent_dir_name = self.path.split("/")[-2]
    parsed_version = re.findall(r"\d{2}.\d{3}", parent_dir_name)
    if not parsed_version:
      logging.error("Parent directory should have version in XX.YYY format")
      return False

    self.firmware_version = parsed_version[0]

    # Example path - cellular-firmware-quectel-em060-MAIN-01.200/
    self.payload_type = fw_file_to_directory_name[self.basename]
    self.tarball_prefix = module_type_to_tarball_prefix[self.module_type]
    self.tarball_dir_name = (
        self.tarball_prefix
        + self.payload_type
        + optional_recovery_infix
        + self.firmware_version
    )

    return True

  def prepare_files(self, fw_path, target_path):
    if not self.firmware_version or not self.payload_type:
      return False

    package_dir_structure = os.path.join(
        target_path, self.payload_type, self.firmware_version
    )
    os.makedirs(package_dir_structure)
    for f_rel in os.listdir(os.path.dirname(fw_path)):
      f_absl = os.path.join(os.path.dirname(fw_path), f_rel)
      logging.info("Copying %s into %s", f_absl, package_dir_structure)
      shutil.copy(f_absl, package_dir_structure)
    return True


def parse_arguments(argv):
  """Parses command line arguments.

  Args:
      argv: List of commandline arguments.

  Returns:
      Namespace object containing parsed arguments.
  """

  parser = argparse.ArgumentParser(
      description=__doc__, formatter_class=argparse.RawTextHelpFormatter
  )

  parser.add_argument(
      "type",
      type=PackageType,
      choices=list(PackageType),
      help="The type of package to create",
  )

  parser.add_argument(
      "path", help="The path to the FW file or directory to be packaged."
  )

  parser.add_argument(
      "--board",
      help="The ChromeOS board in which this cust pack will be used.",
  )

  parser.add_argument(
      "--revision",
      help=(
          "The next ebuild number for that board. If the current ebuild "
          "revision is r12, enter r13. Only used on L850 OEM FW"
      ),
  )

  parser.add_argument(
      "--tarball-postfix",
      help=(
          "Add a postfix to the tarball dir name. Useful for testing EB FWs and"
          " to fix mistakes when an incorrect FW is uploaded."
      ),
  )

  parser.add_argument(
      "--upload",
      default=False,
      action="store_true",
      help="upload file to GS bucket.",
  )

  parser.add_argument(
      "--keep-files",
      default=False,
      action="store_true",
      help=(
          "Don't delete the tarball files in /tmp. Useful "
          "for Partners. Googlers should not upload files "
          "manually."
      ),
  )

  parser.add_argument(
      "--main-fw-version",
      default=None,
      help="The version of the main firmware.",
  )

  parser.add_argument(
      "--ap-fw-version",
      default=None,
      help="The version of the AP firmware.",
  )

  parser.add_argument(
      "--extra-ap-fw-version",
      default=None,
      help="The version of the dev firmware.",
  )

  return parser.parse_args(argv[1:])


def main(argv):
  """Main function."""

  logging.basicConfig(level=logging.DEBUG)
  opts = parse_arguments(argv)
  logging.info("opts: %s", opts)
  if opts.type == PackageType.L850_MAIN_FW:
    fw_uploader = L850MainFw(opts.path, opts.upload, opts.tarball_postfix)
  elif opts.type == PackageType.L850_OEM_FW:
    fw_uploader = L850OemFw(opts.path, opts.upload, opts.tarball_postfix)
  elif opts.type == PackageType.L850_OEM_DIR_ONLY:
    if not opts.revision:
      logging.error(
          "The ebuild revision is needed to pack it, since "
          "the tarballs need to be unique."
      )
      return os.EX_USAGE
    if not opts.board:
      logging.error("Please enter the board name.")
      return os.EX_USAGE
    fw_uploader = L850OemDir(opts.path, opts.upload, opts.tarball_postfix, opts.revision, opts.board)
  elif opts.type == PackageType.NL668_MAIN_FW:
    fw_uploader = NL668MainFw(opts.path, opts.upload, opts.tarball_postfix)
  elif opts.type in [
      PackageType.FM101_MAIN_FW,
      PackageType.FM101_AP_FW,
      PackageType.FM101_DEV_FW,
      PackageType.FM101_OEM_FW,
      PackageType.FM101_OP_FW,
  ]:
    fw_uploader = FM101Fw(opts.path, opts.upload, opts.tarball_postfix)
  elif opts.type in [
      PackageType.FM101_RECOVERY_FW,
  ]:
    fw_uploader = FM101RecoveryFw(opts.path, opts.upload, opts.tarball_postfix, opts.main_fw_version, opts.ap_fw_version, opts.extra_ap_fw_version)
  elif opts.type in [PackageType.FM350_MAIN_FW, PackageType.FM350_AP_FW]:
    fw_uploader = FM350MainFw(opts.path, opts.upload, opts.tarball_postfix)
  elif opts.type in [
      PackageType.FM350_DEV_FW,
      PackageType.FM350_OEM_FW,
      PackageType.FM350_CARRIER_FW,
  ]:
    fw_uploader = FM350MiscFw(opts.path, opts.upload, opts.tarball_postfix)
  elif opts.type in [
      PackageType.RW101_MAIN_FW,
      PackageType.RW101_AP_FW,
      PackageType.RW101_DEV_FW,
      PackageType.RW101_OEM_FW,
      PackageType.RW101_OP_FW,
  ]:
    fw_uploader = RW101Fw(opts.path, opts.upload, opts.tarball_postfix)
  elif opts.type in [
      PackageType.RW101_RECOVERY_FW,
  ]:
    fw_uploader = RW101RecoveryFw(opts.path, opts.upload, opts.tarball_postfix, opts.main_fw_version, opts.ap_fw_version, opts.extra_ap_fw_version)
  elif opts.type in [
      PackageType.RW135_MAIN_FW,
      PackageType.RW135_AP_FW,
      PackageType.RW135_DEV_FW,
      PackageType.RW135_OEM_FW,
      PackageType.RW135_OP_FW,
  ]:
    fw_uploader = RW135Fw(opts.path, opts.upload, opts.tarball_postfix)
  elif opts.type in [
      PackageType.RW135_RECOVERY_FW,
  ]:
    fw_uploader = RW135RecoveryFw(opts.path, opts.upload, opts.tarball_postfix, opts.main_fw_version, opts.ap_fw_version, opts.extra_ap_fw_version)
  # EM060 and its derivatives
  elif opts.type in [PackageType.EM060_FW, PackageType.LCUK54_FW]:
    fw_uploader = EM060DerivFw(opts.path, opts.upload, opts.tarball_postfix, opts.type)

  return fw_uploader.process_fw_and_upload(opts.keep_files)


if __name__ == "__main__":
  sys.exit(main(sys.argv))
