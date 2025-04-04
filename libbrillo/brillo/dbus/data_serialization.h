// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_DBUS_DATA_SERIALIZATION_H_
#define LIBBRILLO_BRILLO_DBUS_DATA_SERIALIZATION_H_

// The main functionality provided by this header file is methods to serialize
// native C++ data over D-Bus. This includes three major parts:
// - Methods to write arbitrary C++ data to D-Bus MessageWriter:
//     void AppendValueToWriter(dbus::MessageWriter* writer, const T& value);
//     void AppendValueToWriterAsVariant(dbus::MessageWriter*, const T&);
// - Methods to read arbitrary C++ data from D-Bus MessageReader:
//     bool PopValueFromReader(dbus::MessageReader* reader, T* value);
//     bool PopVariantValueFromReader(dbus::MessageReader* reader, T* value);

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <brillo/brillo_export.h>
#include <brillo/dbus/dbus_signature.h>
#include <brillo/type_name_undecorate.h>
#include <dbus/message.h>

namespace google {
namespace protobuf {
class MessageLite;
}  // namespace protobuf
}  // namespace google

namespace brillo {

// Forward-declare only. Can't include any.h right now because it needs
// AppendValueToWriter() declared below.
class Any;

namespace dbus_utils {

// Base class for DBusType<T> for T not supported by D-Bus. This used to
// implement IsTypeSupported<> below.
struct Unsupported {};

// Generic definition of DBusType<T> which will be specialized for particular
// types later.
// The second template parameter is used only in SFINAE situations to resolve
// class hierarchy chains for protobuf-derived classes. This type is defaulted
// to be 'void' in all other cases and simply ignored.
// See DBusType specialization for google::protobuf::MessageLite below for more
// detailed information.
template <typename T, typename = void>
struct DBusType : public Unsupported {};

// A helper type trait to determine if all of the types listed in Types... are
// supported by D-Bus. This is a generic forward-declaration which will be
// specialized for different type combinations.
template <typename... Types>
struct IsTypeSupported;

// Both T and the Types... must be supported for the complete set to be
// supported.
template <typename T, typename... Types>
struct IsTypeSupported<T, Types...>
    : public std::integral_constant<bool,
                                    IsTypeSupported<T>::value &&
                                        IsTypeSupported<Types...>::value> {};

// For a single type T, check if DBusType<T> derives from Unsupported.
// If it does, then the type is not supported by the D-Bus.
template <typename T>
struct IsTypeSupported<T>
    : public std::integral_constant<
          bool,
          !std::is_base_of<Unsupported, DBusType<T>>::value> {};

// Empty set is not supported.
template <>
struct IsTypeSupported<> : public std::false_type {};

//----------------------------------------------------------------------------
// AppendValueToWriter<T>(dbus::MessageWriter* writer, const T& value)
// Write the |value| of type T to D-Bus message.
// Explicitly delete the overloads for scalar types that are not supported by
// D-Bus.
void AppendValueToWriter(::dbus::MessageWriter* writer, char value) = delete;
void AppendValueToWriter(::dbus::MessageWriter* writer, float value) = delete;

//----------------------------------------------------------------------------
// PopValueFromReader<T>(dbus::MessageWriter* writer, T* value)
// Reads the |value| of type T from D-Bus message.
// Explicitly delete the overloads for scalar types that are not supported by
// D-Bus.
void PopValueFromReader(::dbus::MessageReader* reader, char* value) = delete;
void PopValueFromReader(::dbus::MessageReader* reader, float* value) = delete;

namespace details {
// Helper method used by the many overloads of PopValueFromReader().
// If the current value in the reader is of Variant type, the method descends
// into the Variant and updates the |*reader_ref| with the transient
// |variant_reader| MessageReader instance passed in.
// Returns false if it fails to descend into the Variant.
// TODO(b/289932268): To catch actual errors, unwrapping should be done
// explicitly. Thus, this should be removed. Handling D-Bus variant should be
// done in other place explicitly.
BRILLO_EXPORT bool DescendIntoVariantIfPresent(
    ::dbus::MessageReader** reader_ref,
    ::dbus::MessageReader* variant_reader,
    bool for_any = false);
}  // namespace details

//=============================================================================
// Specializations/overloads for AppendValueToWriter, PopValueFromReader and
// DBusType<T> for various C++ types that can be serialized over D-Bus.

// bool -----------------------------------------------------------------------
BRILLO_EXPORT void AppendValueToWriter(::dbus::MessageWriter* writer,
                                       bool value);
BRILLO_EXPORT bool PopValueFromReader(::dbus::MessageReader* reader,
                                      bool* value);

template <>
struct DBusType<bool> {
  inline static void Write(::dbus::MessageWriter* writer, bool value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader, bool* value) {
    return PopValueFromReader(reader, value);
  }
};

// uint8_t --------------------------------------------------------------------
BRILLO_EXPORT void AppendValueToWriter(::dbus::MessageWriter* writer,
                                       uint8_t value);
BRILLO_EXPORT bool PopValueFromReader(::dbus::MessageReader* reader,
                                      uint8_t* value);

template <>
struct DBusType<uint8_t> {
  inline static void Write(::dbus::MessageWriter* writer, uint8_t value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader, uint8_t* value) {
    return PopValueFromReader(reader, value);
  }
};

// int16_t --------------------------------------------------------------------
BRILLO_EXPORT void AppendValueToWriter(::dbus::MessageWriter* writer,
                                       int16_t value);
BRILLO_EXPORT bool PopValueFromReader(::dbus::MessageReader* reader,
                                      int16_t* value);

template <>
struct DBusType<int16_t> {
  inline static void Write(::dbus::MessageWriter* writer, int16_t value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader, int16_t* value) {
    return PopValueFromReader(reader, value);
  }
};

// uint16_t -------------------------------------------------------------------
BRILLO_EXPORT void AppendValueToWriter(::dbus::MessageWriter* writer,
                                       uint16_t value);
BRILLO_EXPORT bool PopValueFromReader(::dbus::MessageReader* reader,
                                      uint16_t* value);

template <>
struct DBusType<uint16_t> {
  inline static void Write(::dbus::MessageWriter* writer, uint16_t value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader, uint16_t* value) {
    return PopValueFromReader(reader, value);
  }
};

// int32_t --------------------------------------------------------------------
BRILLO_EXPORT void AppendValueToWriter(::dbus::MessageWriter* writer,
                                       int32_t value);
BRILLO_EXPORT bool PopValueFromReader(::dbus::MessageReader* reader,
                                      int32_t* value);

template <>
struct DBusType<int32_t> {
  inline static void Write(::dbus::MessageWriter* writer, int32_t value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader, int32_t* value) {
    return PopValueFromReader(reader, value);
  }
};

// uint32_t -------------------------------------------------------------------
BRILLO_EXPORT void AppendValueToWriter(::dbus::MessageWriter* writer,
                                       uint32_t value);
BRILLO_EXPORT bool PopValueFromReader(::dbus::MessageReader* reader,
                                      uint32_t* value);

template <>
struct DBusType<uint32_t> {
  inline static void Write(::dbus::MessageWriter* writer, uint32_t value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader, uint32_t* value) {
    return PopValueFromReader(reader, value);
  }
};

// int64_t --------------------------------------------------------------------
BRILLO_EXPORT void AppendValueToWriter(::dbus::MessageWriter* writer,
                                       int64_t value);
BRILLO_EXPORT bool PopValueFromReader(::dbus::MessageReader* reader,
                                      int64_t* value);

template <>
struct DBusType<int64_t> {
  inline static void Write(::dbus::MessageWriter* writer, int64_t value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader, int64_t* value) {
    return PopValueFromReader(reader, value);
  }
};

// uint64_t -------------------------------------------------------------------
BRILLO_EXPORT void AppendValueToWriter(::dbus::MessageWriter* writer,
                                       uint64_t value);
BRILLO_EXPORT bool PopValueFromReader(::dbus::MessageReader* reader,
                                      uint64_t* value);

template <>
struct DBusType<uint64_t> {
  inline static void Write(::dbus::MessageWriter* writer, uint64_t value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader, uint64_t* value) {
    return PopValueFromReader(reader, value);
  }
};

// double ---------------------------------------------------------------------
BRILLO_EXPORT void AppendValueToWriter(::dbus::MessageWriter* writer,
                                       double value);
BRILLO_EXPORT bool PopValueFromReader(::dbus::MessageReader* reader,
                                      double* value);

template <>
struct DBusType<double> {
  inline static void Write(::dbus::MessageWriter* writer, double value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader, double* value) {
    return PopValueFromReader(reader, value);
  }
};

// std::string ----------------------------------------------------------------
BRILLO_EXPORT void AppendValueToWriter(::dbus::MessageWriter* writer,
                                       const std::string& value);
BRILLO_EXPORT bool PopValueFromReader(::dbus::MessageReader* reader,
                                      std::string* value);

template <>
struct DBusType<std::string> {
  inline static void Write(::dbus::MessageWriter* writer,
                           const std::string& value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader, std::string* value) {
    return PopValueFromReader(reader, value);
  }
};

// const char*
BRILLO_EXPORT void AppendValueToWriter(::dbus::MessageWriter* writer,
                                       const char* value);

template <>
struct DBusType<const char*> {
  inline static void Write(::dbus::MessageWriter* writer, const char* value) {
    AppendValueToWriter(writer, value);
  }
};

// const char[]
template <>
struct DBusType<const char[]> {
  inline static void Write(::dbus::MessageWriter* writer, const char* value) {
    AppendValueToWriter(writer, value);
  }
};

// dbus::ObjectPath -----------------------------------------------------------
BRILLO_EXPORT void AppendValueToWriter(::dbus::MessageWriter* writer,
                                       const ::dbus::ObjectPath& value);
BRILLO_EXPORT bool PopValueFromReader(::dbus::MessageReader* reader,
                                      ::dbus::ObjectPath* value);

template <>
struct DBusType<::dbus::ObjectPath> {
  inline static void Write(::dbus::MessageWriter* writer,
                           const ::dbus::ObjectPath& value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader,
                          ::dbus::ObjectPath* value) {
    return PopValueFromReader(reader, value);
  }
};

// base::ScopedFD -------------------------------------------------------------
BRILLO_EXPORT void AppendValueToWriter(::dbus::MessageWriter* writer,
                                       const base::ScopedFD& value);
BRILLO_EXPORT bool PopValueFromReader(::dbus::MessageReader* reader,
                                      base::ScopedFD* value);

template <>
struct DBusType<base::ScopedFD> {
  inline static void Write(::dbus::MessageWriter* writer,
                           const base::ScopedFD& value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader,
                          base::ScopedFD* value) {
    return PopValueFromReader(reader, value);
  }
};

// brillo::Any --------------------------------------------------------------
BRILLO_EXPORT void AppendValueToWriter(::dbus::MessageWriter* writer,
                                       const brillo::Any& value);
BRILLO_EXPORT bool PopValueFromReader(::dbus::MessageReader* reader,
                                      brillo::Any* value);

template <>
struct DBusType<brillo::Any> {
  inline static void Write(::dbus::MessageWriter* writer,
                           const brillo::Any& value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader, brillo::Any* value) {
    return PopValueFromReader(reader, value);
  }
};

// std::vector = D-Bus ARRAY. -------------------------------------------------
template <typename T, typename ALLOC>
typename std::enable_if<IsTypeSupported<T>::value>::type AppendValueToWriter(
    ::dbus::MessageWriter* writer, const std::vector<T, ALLOC>& value) {
  ::dbus::MessageWriter array_writer(nullptr);
  writer->OpenArray(GetDBusSignature<T>(), &array_writer);
  for (const auto& element : value) {
    // Use DBusType<T>::Write() instead of AppendValueToWriter() to delay
    // binding to AppendValueToWriter() to the point of instantiation of this
    // template.
    DBusType<T>::Write(&array_writer, element);
  }
  writer->CloseContainer(&array_writer);
}

template <typename T, typename ALLOC>
typename std::enable_if<IsTypeSupported<T>::value, bool>::type
PopValueFromReader(::dbus::MessageReader* reader,
                   std::vector<T, ALLOC>* value) {
  ::dbus::MessageReader array_reader(nullptr);
  if (!reader->PopArray(&array_reader)) {
    return false;
  }
  value->clear();
  while (array_reader.HasMoreData()) {
    T data;
    // Use DBusType<T>::Read() instead of PopValueFromReader() to delay
    // binding to PopValueFromReader() to the point of instantiation of this
    // template.
    if (!DBusType<T>::Read(&array_reader, &data)) {
      return false;
    }
    value->push_back(std::move(data));
  }
  return true;
}

namespace details {
// DBusArrayType<> is a helper base class for DBusType<vector<T>> that provides
// Write/Read methods for T types that are supported by D-Bus
// and not having those methods for types that are not supported by D-Bus.
template <bool inner_type_supported, typename T, typename ALLOC>
struct DBusArrayType {
  inline static void Write(::dbus::MessageWriter* writer,
                           const std::vector<T, ALLOC>& value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader,
                          std::vector<T, ALLOC>* value) {
    return PopValueFromReader(reader, value);
  }
};

// Explicit specialization for unsupported type T.
template <typename T, typename ALLOC>
struct DBusArrayType<false, T, ALLOC> : public Unsupported {};

}  // namespace details

template <typename T, typename ALLOC>
struct DBusType<std::vector<T, ALLOC>>
    : public details::DBusArrayType<IsTypeSupported<T>::value, T, ALLOC> {};

// std::pair = D-Bus STRUCT with two elements. --------------------------------
template <typename U, typename V>
typename std::enable_if<IsTypeSupported<U, V>::value>::type AppendValueToWriter(
    ::dbus::MessageWriter* writer, const std::pair<U, V>& value) {
  ::dbus::MessageWriter struct_writer(nullptr);
  writer->OpenStruct(&struct_writer);
  // Use DBusType<T>::Write() instead of AppendValueToWriter() to delay
  // binding to AppendValueToWriter() to the point of instantiation of this
  // template.
  DBusType<U>::Write(&struct_writer, value.first);
  DBusType<V>::Write(&struct_writer, value.second);
  writer->CloseContainer(&struct_writer);
}

template <typename U, typename V>
typename std::enable_if<IsTypeSupported<U, V>::value, bool>::type
PopValueFromReader(::dbus::MessageReader* reader, std::pair<U, V>* value) {
  ::dbus::MessageReader struct_reader(nullptr);
  if (!reader->PopStruct(&struct_reader)) {
    return false;
  }
  // Use DBusType<T>::Read() instead of PopValueFromReader() to delay
  // binding to PopValueFromReader() to the point of instantiation of this
  // template.
  return DBusType<U>::Read(&struct_reader, &value->first) &&
         DBusType<V>::Read(&struct_reader, &value->second);
}

namespace details {

// DBusArrayType<> is a helper base class for DBusType<pair<U, V>> that provides
// Write/Read methods for types that are supported by D-Bus
// and not having those methods for types that are not supported by D-Bus.
template <bool inner_type_supported, typename U, typename V>
struct DBusPairType {
  inline static void Write(::dbus::MessageWriter* writer,
                           const std::pair<U, V>& value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader,
                          std::pair<U, V>* value) {
    return PopValueFromReader(reader, value);
  }
};

// Either U, or V, or both are not supported by D-Bus.
template <typename U, typename V>
struct DBusPairType<false, U, V> : public Unsupported {};

}  // namespace details

template <typename U, typename V>
struct DBusType<std::pair<U, V>>
    : public details::DBusPairType<IsTypeSupported<U, V>::value, U, V> {};

// std::tuple = D-Bus STRUCT with arbitrary number of members. ----------------
namespace details {

// TupleIterator<I, N, T...> is a helper class to iterate over all the elements
// of a tuple<T...> from index I to N. TupleIterator<>::Read and ::Write methods
// are called for each element of the tuple and iteration continues until I == N
// in which case the specialization for I==N below stops the recursion.
template <size_t I, size_t N, typename... T>
struct TupleIterator {
  // Tuple is just a convenience alias to a tuple containing elements of type T.
  using Tuple = std::tuple<T...>;
  // ValueType is the type of the element at index I.
  using ValueType = typename std::tuple_element<I, Tuple>::type;

  // Write the tuple element at index I to D-Bus message.
  static void Write(::dbus::MessageWriter* writer, const Tuple& value) {
    // Use DBusType<T>::Write() instead of AppendValueToWriter() to delay
    // binding to AppendValueToWriter() to the point of instantiation of this
    // template.
    DBusType<ValueType>::Write(writer, std::get<I>(value));
    TupleIterator<I + 1, N, T...>::Write(writer, value);
  }

  // Read the tuple element at index I from D-Bus message.
  static bool Read(::dbus::MessageReader* reader, Tuple* value) {
    // Use DBusType<T>::Read() instead of PopValueFromReader() to delay
    // binding to PopValueFromReader() to the point of instantiation of this
    // template.
    return DBusType<ValueType>::Read(reader, &std::get<I>(*value)) &&
           TupleIterator<I + 1, N, T...>::Read(reader, value);
  }
};

// Specialization to end the iteration when the index reaches the last element.
template <size_t N, typename... T>
struct TupleIterator<N, N, T...> {
  using Tuple = std::tuple<T...>;
  static void Write(::dbus::MessageWriter* /* writer */,
                    const Tuple& /* value */) {}
  static bool Read(::dbus::MessageReader* /* reader */, Tuple* /* value */) {
    return true;
  }
};

}  // namespace details

template <typename... T>
typename std::enable_if<IsTypeSupported<T...>::value>::type AppendValueToWriter(
    ::dbus::MessageWriter* writer, const std::tuple<T...>& value) {
  ::dbus::MessageWriter struct_writer(nullptr);
  writer->OpenStruct(&struct_writer);
  details::TupleIterator<0, sizeof...(T), T...>::Write(&struct_writer, value);
  writer->CloseContainer(&struct_writer);
}

template <typename... T>
typename std::enable_if<IsTypeSupported<T...>::value, bool>::type
PopValueFromReader(::dbus::MessageReader* reader, std::tuple<T...>* value) {
  ::dbus::MessageReader struct_reader(nullptr);
  if (!reader->PopStruct(&struct_reader)) {
    return false;
  }
  return details::TupleIterator<0, sizeof...(T), T...>::Read(&struct_reader,
                                                             value);
}

namespace details {

// DBusTupleType<> is a helper base class for DBusType<tuple<T...>> that
// provides Write/Read methods for types that are supported by
// D-Bus and not having those methods for types that are not supported by D-Bus.
template <bool inner_type_supported, typename... T>
struct DBusTupleType {
  inline static void Write(::dbus::MessageWriter* writer,
                           const std::tuple<T...>& value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader,
                          std::tuple<T...>* value) {
    return PopValueFromReader(reader, value);
  }
};

// Some/all of types T... are not supported by D-Bus.
template <typename... T>
struct DBusTupleType<false, T...> : public Unsupported {};

}  // namespace details

template <typename... T>
struct DBusType<std::tuple<T...>>
    : public details::DBusTupleType<IsTypeSupported<T...>::value, T...> {};

// std::map = D-Bus ARRAY of DICT_ENTRY. --------------------------------------
template <typename KEY, typename VALUE, typename PRED, typename ALLOC>
typename std::enable_if<IsTypeSupported<KEY, VALUE>::value>::type
AppendValueToWriter(::dbus::MessageWriter* writer,
                    const std::map<KEY, VALUE, PRED, ALLOC>& value) {
  ::dbus::MessageWriter dict_writer(nullptr);
  writer->OpenArray(internal::StrJoin("{", DBusSignature<KEY>::kValue,
                                      DBusSignature<VALUE>::kValue, "}")
                        .data(),
                    &dict_writer);
  for (const auto& pair : value) {
    ::dbus::MessageWriter entry_writer(nullptr);
    dict_writer.OpenDictEntry(&entry_writer);
    // Use DBusType<T>::Write() instead of AppendValueToWriter() to delay
    // binding to AppendValueToWriter() to the point of instantiation of this
    // template.
    DBusType<KEY>::Write(&entry_writer, pair.first);
    DBusType<VALUE>::Write(&entry_writer, pair.second);
    dict_writer.CloseContainer(&entry_writer);
  }
  writer->CloseContainer(&dict_writer);
}

template <typename KEY, typename VALUE, typename PRED, typename ALLOC>
typename std::enable_if<IsTypeSupported<KEY, VALUE>::value, bool>::type
PopValueFromReader(::dbus::MessageReader* reader,
                   std::map<KEY, VALUE, PRED, ALLOC>* value) {
  ::dbus::MessageReader array_reader(nullptr);
  if (!reader->PopArray(&array_reader)) {
    return false;
  }
  value->clear();
  while (array_reader.HasMoreData()) {
    ::dbus::MessageReader dict_entry_reader(nullptr);
    if (!array_reader.PopDictEntry(&dict_entry_reader)) {
      return false;
    }
    KEY key;
    VALUE data;
    // Use DBusType<T>::Read() instead of PopValueFromReader() to delay
    // binding to PopValueFromReader() to the point of instantiation of this
    // template.
    if (!DBusType<KEY>::Read(&dict_entry_reader, &key) ||
        !DBusType<VALUE>::Read(&dict_entry_reader, &data)) {
      return false;
    }
    value->emplace(std::move(key), std::move(data));
  }
  return true;
}

namespace details {

// DBusArrayType<> is a helper base class for DBusType<map<K, V>> that provides
// Write/Read methods for T types that are supported by D-Bus
// and not having those methods for types that are not supported by D-Bus.
template <bool inner_types_supported,
          typename KEY,
          typename VALUE,
          typename PRED,
          typename ALLOC>
struct DBusMapType {
  inline static void Write(::dbus::MessageWriter* writer,
                           const std::map<KEY, VALUE, PRED, ALLOC>& value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader,
                          std::map<KEY, VALUE, PRED, ALLOC>* value) {
    return PopValueFromReader(reader, value);
  }
};

// Types KEY, VALUE or both are not supported by D-Bus.
template <typename KEY, typename VALUE, typename PRED, typename ALLOC>
struct DBusMapType<false, KEY, VALUE, PRED, ALLOC> : public Unsupported {};

}  // namespace details

template <typename KEY, typename VALUE, typename PRED, typename ALLOC>
struct DBusType<std::map<KEY, VALUE, PRED, ALLOC>>
    : public details::DBusMapType<IsTypeSupported<KEY, VALUE>::value,
                                  KEY,
                                  VALUE,
                                  PRED,
                                  ALLOC> {};

// google::protobuf::MessageLite = D-Bus ARRAY of BYTE ------------------------
inline void AppendValueToWriter(::dbus::MessageWriter* writer,
                                const google::protobuf::MessageLite& value) {
  writer->AppendProtoAsArrayOfBytes(value);
}

inline bool PopValueFromReader(::dbus::MessageReader* reader,
                               google::protobuf::MessageLite* value) {
  return reader->PopArrayOfBytesAsProto(value);
}

// is_protobuf_t<T> is a helper type trait to determine if type T derives from
// google::protobuf::MessageLite.
template <typename T>
using is_protobuf = std::is_base_of<google::protobuf::MessageLite, T>;

// Specialize DBusType<T> for classes that derive from protobuf::MessageLite.
// Here we perform a partial specialization of DBusType<T> only for types
// that derive from google::protobuf::MessageLite. This is done by employing
// the second template parameter in DBusType and this basically relies on C++
// SFINAE rules. "typename std::enable_if<is_protobuf<T>::value>::type" will
// evaluate to "void" for classes T that descend from MessageLite and will be
// an invalid construct for other types/classes which will automatically
// remove this particular specialization from name resolution context.
template <typename T>
struct DBusType<T, typename std::enable_if<is_protobuf<T>::value>::type> {
  inline static void Write(::dbus::MessageWriter* writer, const T& value) {
    AppendValueToWriter(writer, value);
  }
  inline static bool Read(::dbus::MessageReader* reader, T* value) {
    return PopValueFromReader(reader, value);
  }
};

//----------------------------------------------------------------------------
// AppendValueToWriterAsVariant<T>(::dbus::MessageWriter* writer, const T&
// value) Write the |value| of type T to D-Bus message as a VARIANT. This
// overload is provided only if T is supported by D-Bus.
template <typename T>
typename std::enable_if<IsTypeSupported<T>::value>::type
AppendValueToWriterAsVariant(::dbus::MessageWriter* writer, const T& value) {
  ::dbus::MessageWriter variant_writer(nullptr);
  writer->OpenVariant(GetDBusSignature<T>(), &variant_writer);
  // Use DBusType<T>::Write() instead of AppendValueToWriter() to delay
  // binding to AppendValueToWriter() to the point of instantiation of this
  // template.
  DBusType<T>::Write(&variant_writer, value);
  writer->CloseContainer(&variant_writer);
}

// Special case: do not allow to write a Variant containing a Variant.
// Just redirect to normal AppendValueToWriter().
inline void AppendValueToWriterAsVariant(::dbus::MessageWriter* writer,
                                         const brillo::Any& value) {
  return AppendValueToWriter(writer, value);
}

//----------------------------------------------------------------------------
// PopVariantValueFromReader<T>(::dbus::MessageWriter* writer, T* value)
// Reads a Variant containing the |value| of type T from D-Bus message.
// Note that the generic PopValueFromReader<T>(...) can do this too.
// This method is provided for two reasons:
//   1. For API symmetry with AppendValueToWriter/AppendValueToWriterAsVariant.
//   2. To be used when it is important to assert that the data was sent
//      specifically as a Variant.
// This overload is provided only if T is supported by D-Bus.
template <typename T>
typename std::enable_if<IsTypeSupported<T>::value, bool>::type
PopVariantValueFromReader(::dbus::MessageReader* reader, T* value) {
  ::dbus::MessageReader variant_reader(nullptr);
  if (!reader->PopVariant(&variant_reader)) {
    return false;
  }
  // Use DBusType<T>::Read() instead of PopValueFromReader() to delay
  // binding to PopValueFromReader() to the point of instantiation of this
  // template.
  return DBusType<T>::Read(&variant_reader, value);
}

// Special handling of request to read a Variant of Variant.
inline bool PopVariantValueFromReader(::dbus::MessageReader* reader,
                                      Any* value) {
  return PopValueFromReader(reader, value);
}

// Returns the type to store the D-Bus argument value.
// In-arg type is const T&, while out-arg type is T*.
// They will be converted into T.
template <typename T>
using StorageType = std::decay_t<std::remove_pointer_t<T>>;

// Reads the D-Bus arguments from |reader| to each |args| in order.
// Returns true on success, including there's no arguments remaining.
template <typename... Args>
bool ReadDBusArgs(dbus::MessageReader* reader, Args*... args) {
  return (DBusType<std::decay_t<Args>>::Read(reader, args) && ...) &&
         !reader->HasMoreData();
}

// Taking a tuple of either 1) references to arguments, or 2) storages
// of arguments, then read the D-Bus arguments to there.
template <typename Tuple>
bool ApplyReadDBusArgs(dbus::MessageReader* reader, Tuple&& tuple) {
  return std::apply(
      [reader](auto&&... in_args) { return ReadDBusArgs(reader, &in_args...); },
      std::forward<Tuple>(tuple));
}

// Writes the D-Bus arguments to |writer| in order.
template <typename... Args>
void WriteDBusArgs(dbus::MessageWriter* writer, const Args&... args) {
  (DBusType<std::decay_t<Args>>::Write(writer, args), ...);
}

// Currently, D-Bus arg reading function automatically unwraps variant,
// so even if the type is different from the spec, that may be read
// unexpectedly. That will be removed, and this is the flag for its
// safer migration. See also b/289932268.
enum class AutoVariantUnwrapState {
  // Enabled the automatic variant unwrapping, i.e., the original state.
  kEnabled,

  // Still automatically unwrapped, but crash dump (without actual crash) will
  // be reported when it happens. So, we can identify the cases in the world.
  kDumpWithoutCrash,

  // Disabled the automatic variant, which is the eventual state.
  kDisabled,
};
BRILLO_EXPORT void SetAutoVariantUnwrapState(AutoVariantUnwrapState state);
BRILLO_EXPORT AutoVariantUnwrapState GetAutoVariantUnwrapState();

}  // namespace dbus_utils
}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_DBUS_DATA_SERIALIZATION_H_
