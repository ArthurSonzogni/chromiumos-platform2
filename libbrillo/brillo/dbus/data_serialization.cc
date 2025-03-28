// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/dbus/data_serialization.h>

#include <sys/wait.h>
#include <unistd.h>

#include <base/check.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <brillo/any.h>
#include <brillo/variant_dictionary.h>

namespace brillo::dbus_utils {

namespace details {
namespace {
AutoVariantUnwrapState g_auto_variant_unwrap_state =
    AutoVariantUnwrapState::kDumpWithoutCrash;
}  // namespace

bool DescendIntoVariantIfPresent(::dbus::MessageReader** reader_ref,
                                 ::dbus::MessageReader* variant_reader,
                                 bool for_any) {
  if ((*reader_ref)->GetDataType() != ::dbus::Message::VARIANT) {
    return true;
  }

  if (!for_any) {
    switch (g_auto_variant_unwrap_state) {
      case AutoVariantUnwrapState::kEnabled:
        // Do nothing.
        break;
      case AutoVariantUnwrapState::kDumpWithoutCrash: {
        // TODO(b/289932268): Callers of this function, which is often
        // message readers, should know the schema of the argument,
        // so unwrapping VARIANT should be done explicitly, i.e., this part
        // should not be executed conceptually. Though, unfortunately, this
        // function is (also) used in generated code, and so currently there
        // are many callsites, and fixing each caller one-by-one is not so
        // easy.
        // To be safer, record crash log here to see whether there are no
        // unexpected uses.
        // base::DumpWithoutCrash won't work with platform packages, so we
        // fork then let the child process crash to make a crash report.
        //
        // If the process has a seccomp policy active and doesn't allow fork,
        // then we'll crash immediately instead of in the background,
        // but it's not feasible to detect, and this situation in general
        // should be uncommon enough.
        pid_t pid = fork();
        if (pid == 0) {
          // Let the child process just crash.
          RAW_LOG(FATAL, "Variant unwrapping is done unexpectedly");
        }
        // Collect the crashed child process.
        // Do it as a blocking operation, since this is unexpected so we do not
        // worry about its performance.
        auto ret = HANDLE_EINTR(waitpid(pid, nullptr, 0));
        if (ret != 0) {
          PLOG(ERROR) << "failed on waitpid(" << pid << ")";
        }
        break;
      }
      case AutoVariantUnwrapState::kDisabled:
        // Unexpected variant, but return as "succeeded" without do anything,
        // So this function will look "no-op" from callers.
        // Often, following message reading calls will fail due to type
        // mismatch.
        LOG(ERROR) << "Unexpceted variant unwrap";
        return true;
    }
  }

  if (!(*reader_ref)->PopVariant(variant_reader)) {
    return false;
  }
  *reader_ref = variant_reader;
  return true;
}

}  // namespace details

void AppendValueToWriter(dbus::MessageWriter* writer, bool value) {
  writer->AppendBool(value);
}

void AppendValueToWriter(dbus::MessageWriter* writer, uint8_t value) {
  writer->AppendByte(value);
}

void AppendValueToWriter(dbus::MessageWriter* writer, int16_t value) {
  writer->AppendInt16(value);
}

void AppendValueToWriter(dbus::MessageWriter* writer, uint16_t value) {
  writer->AppendUint16(value);
}

void AppendValueToWriter(dbus::MessageWriter* writer, int32_t value) {
  writer->AppendInt32(value);
}

void AppendValueToWriter(dbus::MessageWriter* writer, uint32_t value) {
  writer->AppendUint32(value);
}

void AppendValueToWriter(dbus::MessageWriter* writer, int64_t value) {
  writer->AppendInt64(value);
}

void AppendValueToWriter(dbus::MessageWriter* writer, uint64_t value) {
  writer->AppendUint64(value);
}

void AppendValueToWriter(dbus::MessageWriter* writer, double value) {
  writer->AppendDouble(value);
}

void AppendValueToWriter(dbus::MessageWriter* writer,
                         const std::string& value) {
  writer->AppendString(value);
}

void AppendValueToWriter(dbus::MessageWriter* writer, const char* value) {
  AppendValueToWriter(writer, std::string(value));
}

void AppendValueToWriter(dbus::MessageWriter* writer,
                         const dbus::ObjectPath& value) {
  writer->AppendObjectPath(value);
}

void AppendValueToWriter(dbus::MessageWriter* writer,
                         const base::ScopedFD& value) {
  writer->AppendFileDescriptor(value.get());
}

void AppendValueToWriter(dbus::MessageWriter* writer,
                         const brillo::Any& value) {
  value.AppendToDBusMessageWriter(writer);
}

///////////////////////////////////////////////////////////////////////////////

bool PopValueFromReader(dbus::MessageReader* reader, bool* value) {
  return reader->PopBool(value);
}

bool PopValueFromReader(dbus::MessageReader* reader, uint8_t* value) {
  return reader->PopByte(value);
}

bool PopValueFromReader(dbus::MessageReader* reader, int16_t* value) {
  return reader->PopInt16(value);
}

bool PopValueFromReader(dbus::MessageReader* reader, uint16_t* value) {
  return reader->PopUint16(value);
}

bool PopValueFromReader(dbus::MessageReader* reader, int32_t* value) {
  return reader->PopInt32(value);
}

bool PopValueFromReader(dbus::MessageReader* reader, uint32_t* value) {
  return reader->PopUint32(value);
}

bool PopValueFromReader(dbus::MessageReader* reader, int64_t* value) {
  return reader->PopInt64(value);
}

bool PopValueFromReader(dbus::MessageReader* reader, uint64_t* value) {
  return reader->PopUint64(value);
}

bool PopValueFromReader(dbus::MessageReader* reader, double* value) {
  return reader->PopDouble(value);
}

bool PopValueFromReader(dbus::MessageReader* reader, std::string* value) {
  return reader->PopString(value);
}

bool PopValueFromReader(dbus::MessageReader* reader, dbus::ObjectPath* value) {
  return reader->PopObjectPath(value);
}

bool PopValueFromReader(dbus::MessageReader* reader, base::ScopedFD* value) {
  return reader->PopFileDescriptor(value);
}

namespace {

// Helper methods for PopValueFromReader(dbus::MessageReader*, Any*)
// implementation. Pops a value of particular type from |reader| and assigns
// it to |value| of type Any.
template <typename T>
bool PopTypedValueFromReader(dbus::MessageReader* reader, brillo::Any* value) {
  T data{};
  if (!PopValueFromReader(reader, &data)) {
    return false;
  }
  *value = std::move(data);
  return true;
}

// std::vector<T> overload.
template <typename T>
bool PopTypedArrayFromReader(dbus::MessageReader* reader, brillo::Any* value) {
  return PopTypedValueFromReader<std::vector<T>>(reader, value);
}

// std::map<KEY, VALUE> overload.
template <typename KEY, typename VALUE>
bool PopTypedMapFromReader(dbus::MessageReader* reader, brillo::Any* value) {
  return PopTypedValueFromReader<std::map<KEY, VALUE>>(reader, value);
}

// Helper methods for reading common ARRAY signatures into a Variant.
// Note that only common types are supported. If an additional specific
// type signature is required, feel free to add support for it.
bool PopArrayValueFromReader(dbus::MessageReader* reader, brillo::Any* value) {
  std::string signature = reader->GetDataSignature();
  if (signature == "ab") {
    return PopTypedArrayFromReader<bool>(reader, value);
  } else if (signature == "ay") {
    return PopTypedArrayFromReader<uint8_t>(reader, value);
  } else if (signature == "an") {
    return PopTypedArrayFromReader<int16_t>(reader, value);
  } else if (signature == "aq") {
    return PopTypedArrayFromReader<uint16_t>(reader, value);
  } else if (signature == "ai") {
    return PopTypedArrayFromReader<int32_t>(reader, value);
  } else if (signature == "au") {
    return PopTypedArrayFromReader<uint32_t>(reader, value);
  } else if (signature == "ax") {
    return PopTypedArrayFromReader<int64_t>(reader, value);
  } else if (signature == "at") {
    return PopTypedArrayFromReader<uint64_t>(reader, value);
  } else if (signature == "ad") {
    return PopTypedArrayFromReader<double>(reader, value);
  } else if (signature == "as") {
    return PopTypedArrayFromReader<std::string>(reader, value);
  } else if (signature == "ao") {
    return PopTypedArrayFromReader<dbus::ObjectPath>(reader, value);
  } else if (signature == "av") {
    return PopTypedArrayFromReader<brillo::Any>(reader, value);
  } else if (signature == "a{ss}") {
    return PopTypedMapFromReader<std::string, std::string>(reader, value);
  } else if (signature == "a{sv}") {
    return PopTypedValueFromReader<brillo::VariantDictionary>(reader, value);
  } else if (signature == "aa{ss}") {
    return PopTypedArrayFromReader<std::map<std::string, std::string>>(reader,
                                                                       value);
  } else if (signature == "aay") {
    return PopTypedArrayFromReader<std::vector<uint8_t>>(reader, value);
  } else if (signature == "aa{sv}") {
    return PopTypedArrayFromReader<brillo::VariantDictionary>(reader, value);
  } else if (signature == "a{sa{ss}}") {
    return PopTypedMapFromReader<std::string,
                                 std::map<std::string, std::string>>(reader,
                                                                     value);
  } else if (signature == "a{sa{sv}}") {
    return PopTypedMapFromReader<std::string, brillo::VariantDictionary>(reader,
                                                                         value);
  } else if (signature == "a{qay}") {
    return PopTypedMapFromReader<uint16_t, std::vector<uint8_t>>(reader, value);
  } else if (signature == "a{say}") {
    return PopTypedMapFromReader<std::string, std::vector<uint8_t>>(reader,
                                                                    value);
  } else if (signature == "a{uv}") {
    return PopTypedMapFromReader<uint32_t, brillo::Any>(reader, value);
  } else if (signature == "a(su)") {
    return PopTypedArrayFromReader<std::tuple<std::string, uint32_t>>(reader,
                                                                      value);
  } else if (signature == "a{uu}") {
    return PopTypedMapFromReader<uint32_t, uint32_t>(reader, value);
  } else if (signature == "a(uu)") {
    return PopTypedArrayFromReader<std::tuple<uint32_t, uint32_t>>(reader,
                                                                   value);
  } else if (signature == "a(ubay)") {
    return PopTypedArrayFromReader<
        std::tuple<uint32_t, bool, std::vector<uint8_t>>>(reader, value);
  }

  // When a use case for particular array signature is found, feel free
  // to add handing for it here.
  LOG(ERROR) << "Variant de-serialization of array containing data of "
             << "type '" << signature << "' is not yet supported";
  return false;
}

// Helper methods for reading common STRUCT signatures into a Variant.
// Note that only common types are supported. If an additional specific
// type signature is required, feel free to add support for it.
bool PopStructValueFromReader(dbus::MessageReader* reader, brillo::Any* value) {
  std::string signature = reader->GetDataSignature();
  if (signature == "(ii)") {
    return PopTypedValueFromReader<std::tuple<int, int>>(reader, value);
  } else if (signature == "(ss)") {
    return PopTypedValueFromReader<std::tuple<std::string, std::string>>(reader,
                                                                         value);
  } else if (signature == "(ub)") {
    return PopTypedValueFromReader<std::tuple<uint32_t, bool>>(reader, value);
  } else if (signature == "(uu)") {
    return PopTypedValueFromReader<std::tuple<uint32_t, uint32_t>>(reader,
                                                                   value);
  } else if (signature == "(ua{sv})") {
    return PopTypedValueFromReader<
        std::tuple<uint32_t, brillo::VariantDictionary>>(reader, value);
  }

  // When a use case for particular struct signature is found, feel free
  // to add handing for it here.
  LOG(ERROR) << "Variant de-serialization of structs of type '" << signature
             << "' is not yet supported";
  return false;
}

}  // anonymous namespace

bool PopValueFromReader(dbus::MessageReader* reader, brillo::Any* value) {
  dbus::MessageReader variant_reader(nullptr);
  if (!details::DescendIntoVariantIfPresent(&reader, &variant_reader,
                                            /*for_any=*/true)) {
    return false;
  }

  switch (reader->GetDataType()) {
    case dbus::Message::BYTE:
      return PopTypedValueFromReader<uint8_t>(reader, value);
    case dbus::Message::BOOL:
      return PopTypedValueFromReader<bool>(reader, value);
    case dbus::Message::INT16:
      return PopTypedValueFromReader<int16_t>(reader, value);
    case dbus::Message::UINT16:
      return PopTypedValueFromReader<uint16_t>(reader, value);
    case dbus::Message::INT32:
      return PopTypedValueFromReader<int32_t>(reader, value);
    case dbus::Message::UINT32:
      return PopTypedValueFromReader<uint32_t>(reader, value);
    case dbus::Message::INT64:
      return PopTypedValueFromReader<int64_t>(reader, value);
    case dbus::Message::UINT64:
      return PopTypedValueFromReader<uint64_t>(reader, value);
    case dbus::Message::DOUBLE:
      return PopTypedValueFromReader<double>(reader, value);
    case dbus::Message::STRING:
      return PopTypedValueFromReader<std::string>(reader, value);
    case dbus::Message::OBJECT_PATH:
      return PopTypedValueFromReader<dbus::ObjectPath>(reader, value);
    case dbus::Message::ARRAY:
      return PopArrayValueFromReader(reader, value);
    case dbus::Message::STRUCT:
      return PopStructValueFromReader(reader, value);
    case dbus::Message::DICT_ENTRY:
      LOG(ERROR) << "Variant of DICT_ENTRY is invalid";
      return false;
    case dbus::Message::VARIANT:
      LOG(ERROR) << "Variant containing a variant is invalid";
      return false;
    case dbus::Message::UNIX_FD:
      CHECK(dbus::IsDBusTypeUnixFdSupported()) << "UNIX_FD data not supported";
      // File descriptors don't use copyable types. Cannot be returned via
      // brillo::Any. Fail here.
      LOG(ERROR) << "Cannot return FileDescriptor via Any";
      return false;
    case dbus::Message::INVALID_DATA:
      LOG(ERROR) << "Invalid D-Bus data type";
      return false;
    default:
      LOG(FATAL) << "Unknown D-Bus data type " << reader->GetDataType()
                 << " with signature \"" << reader->GetDataSignature() << "\"";
  }
}

void SetAutoVariantUnwrapState(AutoVariantUnwrapState state) {
  details::g_auto_variant_unwrap_state = state;
}

AutoVariantUnwrapState GetAutoVariantUnwrapState() {
  return details::g_auto_variant_unwrap_state;
}

}  // namespace brillo::dbus_utils
