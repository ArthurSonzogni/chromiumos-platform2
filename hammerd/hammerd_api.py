# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The python wrapper of the hammerd API."""

from __future__ import print_function

import ctypes

# Load hammerd-api library.
_DLL = ctypes.CDLL('libhammerd-api.so')
ENTROPY_SIZE = ctypes.c_int.in_dll(_DLL, 'kEntropySize').value


class UpdateExtraCommand(object):
  """The enumeration of extra vendor subcommands."""
  ImmediateReset = 0
  JumpToRW = 1
  StayInRO = 2
  UnlockRW = 3
  UnlockRollback = 4
  InjectEntropy = 5
  PairChallenge = 6
  TouchpadInfo = 7


class SectionName(object):
  """The enumeration of the image sections."""
  RO = 0
  RW = 1
  Invalid = 2


class FirstResponsePdu(ctypes.Structure):
  """Struct of response of first PDU from src/platform/ec/include/update_fw.h"""
  _pack_ = 1
  _fields_ = [("return_value", ctypes.c_uint32),
              ("header_type", ctypes.c_uint16),
              ("protocol_version", ctypes.c_uint16),
              ("maximum_pdu_size", ctypes.c_uint32),
              ("flash_protection", ctypes.c_uint32),
              ("offset", ctypes.c_uint32),
              ("version", ctypes.c_ubyte * 32),
              ("min_rollback", ctypes.c_int32),
              ("key_version", ctypes.c_uint32)]

  def __str__(self):
    ret = ''
    for field_name, unused_field_type in self._fields_:
      ret += '%s: %s\n' % (field_name, getattr(self, field_name))
    return ret


class ByteString(ctypes.Structure):
  """Intermediary type between Python string to C++ string.

  Because ctypes doesn't support C++ std::string, we need to pass C-style
  char pointer and the size of string first. Then convert it to std::string in
  other side.
  """
  _fields_ = [
      ('ptr', ctypes.c_char_p),
      ('size', ctypes.c_size_t)]


class WrapperMetaclass(type):
  """The metaclass of the wrapper class.

  Each wrapper class should declare the method signature in "METHODS" fields,
  which is a list of (method name, [arg0 type, arg1 type, ...], return type).
  Also, each class should initiate the instance to "self.object" field.
  """
  def __new__(mcs, name, bases, dct):
    for method_name, argtypes, restype in dct['METHODS']:
      dct[method_name] = WrapperMetaclass.GenerateMethod(
          name, method_name, argtypes, restype)
    return super(WrapperMetaclass, mcs).__new__(mcs, name, bases, dct)

  @staticmethod
  def GenerateMethod(cls_name, method_name, argtypes, restype):
    """Generates the wrapper function by the function signature."""
    def method(self, *args):
      print('Call %s' % method_name)
      if len(args) != len(argtypes) - 1:  # argtypes includes object itself.
        raise TypeError('%s expected %d arguments, got %d.' %
                        (method_name, len(argtypes) - 1, len(args)))
      # Convert Python string to ByteString.
      args = list(args)  # Convert args from tuple to list.
      for idx, arg_type in enumerate(argtypes[1:]):
        if arg_type == ctypes.POINTER(ByteString):
          args[idx] = WrapperMetaclass.ConvertString(args[idx])
      func = getattr(_DLL, '%s_%s' % (cls_name, method_name))
      func.argtypes = argtypes
      func.restype = restype
      return func(self.object, *args)
    return method

  @staticmethod
  def ConvertString(string):
    """Converts Python string to a ctypes ByteString pointer.

    Args:
      string: a Python string.

    Returns:
      A ctypes pointer to ByteString.
    """
    buffer_size = len(string)
    buffer_ptr = ctypes.cast(ctypes.create_string_buffer(string, buffer_size),
                             ctypes.c_char_p)
    return ctypes.byref(ByteString(buffer_ptr, buffer_size))


class FirmwareUpdater(object):
  """The wrapper of FirmwareUpdater class."""
  __metaclass__ = WrapperMetaclass

  METHODS = [
      ('LoadEcImage',
       [ctypes.c_voidp, ctypes.POINTER(ByteString)], ctypes.c_bool),
      ('LoadTouchpadImage',
       [ctypes.c_voidp, ctypes.POINTER(ByteString)], ctypes.c_bool),
      ('TryConnectUsb', [ctypes.c_voidp], ctypes.c_bool),
      ('CloseUsb', [ctypes.c_voidp], None),
      ('SendFirstPdu', [ctypes.c_voidp], ctypes.c_bool),
      ('SendDone', [ctypes.c_voidp], None),
      ('InjectEntropy', [ctypes.c_voidp], ctypes.c_bool),
      ('InjectEntropyWithPayload',
       [ctypes.c_voidp, ctypes.POINTER(ByteString)], ctypes.c_bool),
      ('SendSubcommand', [ctypes.c_voidp, ctypes.c_uint16], ctypes.c_bool),
      ('SendSubcommandWithPayload',
       [ctypes.c_voidp, ctypes.c_uint16, ctypes.POINTER(ByteString)],
       ctypes.c_bool),
      ('SendSubcommandReceiveResponse',
       [ctypes.c_voidp, ctypes.c_uint16, ctypes.POINTER(ByteString),
        ctypes.c_voidp, ctypes.c_size_t], ctypes.c_bool),
      ('TransferImage', [ctypes.c_voidp, ctypes.c_int], ctypes.c_bool),
      ('TransferTouchpadFirmware',
       [ctypes.c_voidp, ctypes.c_uint32, ctypes.c_size_t], ctypes.c_bool),
      ('CurrentSection', [ctypes.c_voidp], ctypes.c_bool),
      ('UpdatePossible', [ctypes.c_voidp, ctypes.c_int], ctypes.c_bool),
      ('VersionMismatch', [ctypes.c_voidp, ctypes.c_int], ctypes.c_bool),
      ('IsSectionLocked', [ctypes.c_voidp, ctypes.c_int], ctypes.c_bool),
      ('UnlockRW', [ctypes.c_voidp], ctypes.c_bool),
      ('IsRollbackLocked', [ctypes.c_voidp], ctypes.c_bool),
      ('UnlockRollback', [ctypes.c_voidp], ctypes.c_bool),
      ('GetFirstResponsePdu',
       [ctypes.c_voidp], ctypes.POINTER(FirstResponsePdu)),
      ('GetSectionVersion', [ctypes.c_voidp, ctypes.c_int], ctypes.c_char_p),
  ]

  def __init__(self, vendor_id, product_id, bus=-1, port=-1):
    func = _DLL.FirmwareUpdater_New
    func.argtypes = [ctypes.c_uint16, ctypes.c_uint16,
                     ctypes.c_int, ctypes.c_int]
    func.restype = ctypes.c_void_p
    self.object = func(vendor_id, product_id, bus, port)
