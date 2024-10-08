# Copyright 2013 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tools for reading, verifying and applying Chrome OS update payloads."""

import hashlib
import io
import struct
import zipfile

from update_payload import applier
from update_payload import checker
from update_payload import common
from update_payload import update_metadata_pb2
from update_payload.error import PayloadError


#
# Helper functions.
#
def _ReadInt(file_obj, size, is_unsigned, hasher=None):
    """Reads a binary-encoded integer from a file.

    It will do the correct conversion based on the reported size and whether or
    not a signed number is expected. Assumes a network (big-endian) byte
    ordering.

    Args:
      file_obj: a file object
      size: the integer size in bytes (2, 4 or 8)
      is_unsigned: whether it is signed or not
      hasher: an optional hasher to pass the value through

    Returns:
      An "unpacked" (Python) integer value.

    Raises:
      PayloadError if an read error occurred.
    """
    return struct.unpack(
        common.IntPackingFmtStr(size, is_unsigned),
        common.Read(file_obj, size, hasher=hasher),
    )[0]


#
# Update payload.
#
class Payload:
    """Chrome OS update payload processor."""

    class _PayloadHeader:
        """Update payload header struct."""

        # Header constants; sizes are in bytes.
        _MAGIC = b"CrAU"
        _VERSION_SIZE = 8
        _MANIFEST_LEN_SIZE = 8
        _METADATA_SIGNATURE_LEN_SIZE = 4

        def __init__(self):
            self.version = None
            self.manifest_len = None
            self.metadata_signature_len = None
            self.size = None

        def ReadFromPayload(self, payload_file, hasher=None):
            """Reads the payload header from a file.

            Reads the payload header from the |payload_file| and updates the |hasher|
            if one is passed. The parsed header is stored in the _PayloadHeader
            instance attributes.

            Args:
              payload_file: a file object
              hasher: an optional hasher to pass the value through

            Returns:
              None.

            Raises:
              PayloadError if a read error occurred or the header is invalid.
            """
            # Verify magic
            magic = common.Read(payload_file, len(self._MAGIC), hasher=hasher)
            if magic != self._MAGIC:
                raise PayloadError("invalid payload magic: %s" % magic)

            self.version = _ReadInt(
                payload_file, self._VERSION_SIZE, True, hasher=hasher
            )
            self.manifest_len = _ReadInt(
                payload_file, self._MANIFEST_LEN_SIZE, True, hasher=hasher
            )
            self.size = (
                len(self._MAGIC) + self._VERSION_SIZE + self._MANIFEST_LEN_SIZE
            )
            self.metadata_signature_len = 0

            if self.version == common.BRILLO_MAJOR_PAYLOAD_VERSION:
                self.size += self._METADATA_SIGNATURE_LEN_SIZE
                self.metadata_signature_len = _ReadInt(
                    payload_file,
                    self._METADATA_SIGNATURE_LEN_SIZE,
                    True,
                    hasher=hasher,
                )

    def __init__(self, payload_file, payload_file_offset=0, is_zip=False):
        """Initialize the payload object.

        Args:
          payload_file: update payload file object open for reading
          payload_file_offset: the offset of the actual payload
          is_zip: whether the payload_file is a zip file
        """
        # TODO: https://github.com/python/cpython/issues/72680
        try:
            if zipfile.is_zipfile(payload_file):
                with zipfile.ZipFile(payload_file) as zfp:
                    with zfp.open("payload.bin") as payload_fp:
                        payload_file = io.BytesIO(payload_fp.read())
        # pylint: disable=W0703
        except Exception as e:
            if is_zip:
                raise e
        self.payload_file = payload_file
        self.payload_file_offset = payload_file_offset
        self.manifest_hasher = None
        self.is_init = False
        self.header = None
        self.manifest = None
        self.data_offset = None
        self.metadata_signature = None
        self.metadata_size = None

    def _ReadHeader(self):
        """Reads and returns the payload header.

        Returns:
          A payload header object.

        Raises:
          PayloadError if a read error occurred.
        """
        header = self._PayloadHeader()
        header.ReadFromPayload(self.payload_file, self.manifest_hasher)
        return header

    def _ReadManifest(self):
        """Reads and returns the payload manifest.

        Returns:
          A string containing the payload manifest in binary form.

        Raises:
          PayloadError if a read error occurred.
        """
        if not self.header:
            raise PayloadError("payload header not present")

        return common.Read(
            self.payload_file,
            self.header.manifest_len,
            hasher=self.manifest_hasher,
        )

    def _ReadMetadataSignature(self):
        """Reads and returns the metadata signatures.

        Returns:
          A string containing the metadata signatures protobuf in binary form or
          an empty string if no metadata signature found in the payload.

        Raises:
          PayloadError if a read error occurred.
        """
        if not self.header:
            raise PayloadError("payload header not present")

        return common.Read(
            self.payload_file,
            self.header.metadata_signature_len,
            offset=self.payload_file_offset
            + self.header.size
            + self.header.manifest_len,
        )

    def ReadDataBlob(self, offset, length):
        """Reads and returns a single data blob from the update payload.

        Args:
          offset: offset to the beginning of the blob from the end of the manifest
          length: the blob's length

        Returns:
          A string containing the raw blob data.

        Raises:
          PayloadError if a read error occurred.
        """
        return common.Read(
            self.payload_file,
            length,
            offset=self.payload_file_offset + self.data_offset + offset,
        )

    def Init(self):
        """Initializes the payload object.

        This is a prerequisite for any other public API call.

        Raises:
          PayloadError if object already initialized or fails to initialize
          correctly.
        """
        if self.is_init:
            raise PayloadError("payload object already initialized")

        self.manifest_hasher = hashlib.sha256()

        # Read the file header.
        self.payload_file.seek(self.payload_file_offset)
        self.header = self._ReadHeader()

        # Read the manifest.
        manifest_raw = self._ReadManifest()
        self.manifest = update_metadata_pb2.DeltaArchiveManifest()
        self.manifest.ParseFromString(manifest_raw)

        # Read the metadata signature (if any).
        metadata_signature_raw = self._ReadMetadataSignature()
        if metadata_signature_raw:
            self.metadata_signature = update_metadata_pb2.Signatures()
            self.metadata_signature.ParseFromString(metadata_signature_raw)

        self.metadata_size = self.header.size + self.header.manifest_len
        self.data_offset = (
            self.metadata_size + self.header.metadata_signature_len
        )

        self.is_init = True

    def _AssertInit(self):
        """Raises an exception if the object was not initialized."""
        if not self.is_init:
            raise PayloadError("payload object not initialized")

    def ResetFile(self):
        """Resets the offset of the payload file to right past the manifest."""
        self.payload_file.seek(self.payload_file_offset + self.data_offset)

    def IsDelta(self):
        """Returns True iff the payload appears to be a delta."""
        self._AssertInit()
        return any(
            partition.HasField("old_partition_info")
            for partition in self.manifest.partitions
        )

    def IsFull(self):
        """Returns True iff the payload appears to be a full."""
        return not self.IsDelta()

    def Check(
        self,
        pubkey_file_name=None,
        metadata_sig_file=None,
        metadata_size=0,
        report_out_file=None,
        assert_type=None,
        block_size=0,
        part_sizes=None,
        allow_unhashed=False,
        disabled_tests=(),
    ):
        """Checks the payload integrity.

        Args:
          pubkey_file_name: public key used for signature verification
          metadata_sig_file: metadata signature, if verification is desired
          metadata_size: metadata size, if verification is desired
          report_out_file: file object to dump the report to
          assert_type: assert that payload is either 'full' or 'delta'
          block_size: expected filesystem / payload block size
          part_sizes: map of partition label to (physical) size in bytes
          allow_unhashed: allow unhashed operation blobs
          disabled_tests: list of tests to disable

        Raises:
          PayloadError if payload verification failed.
        """
        self._AssertInit()

        # Create a short-lived payload checker object and run it.
        helper = checker.PayloadChecker(
            self,
            assert_type=assert_type,
            block_size=block_size,
            allow_unhashed=allow_unhashed,
            disabled_tests=disabled_tests,
        )
        helper.Run(
            pubkey_file_name=pubkey_file_name,
            metadata_sig_file=metadata_sig_file,
            metadata_size=metadata_size,
            part_sizes=part_sizes,
            report_out_file=report_out_file,
        )

    def Apply(
        self,
        new_parts,
        old_parts=None,
        bsdiff_in_place=True,
        bspatch_path=None,
        puffpatch_path=None,
        truncate_to_expected_size=True,
    ):
        """Applies the update payload.

        Args:
          new_parts: map of partition name to dest partition file
          old_parts: map of partition name to partition file (optional)
          bsdiff_in_place: whether to perform BSDIFF operations in-place (optional)
          bspatch_path: path to the bspatch binary (optional)
          puffpatch_path: path to the puffpatch binary (optional)
          truncate_to_expected_size: whether to truncate the resulting partitions
                                     to their expected sizes, as specified in the
                                     payload (optional)

        Raises:
          PayloadError if payload application failed.
        """
        self._AssertInit()

        # Create a short-lived payload applier object and run it.
        helper = applier.PayloadApplier(
            self,
            bsdiff_in_place=bsdiff_in_place,
            bspatch_path=bspatch_path,
            puffpatch_path=puffpatch_path,
            truncate_to_expected_size=truncate_to_expected_size,
        )
        helper.Run(new_parts, old_parts=old_parts)
