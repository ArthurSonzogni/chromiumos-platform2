// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROMO_SMS_CACHE_H_
#define CROMO_SMS_CACHE_H_

#include <map>
#include <vector>

#include <dbus-c++/types.h>  // for DBus::Error &

#include "cromo/sms_message.h"
#include "cromo/utilities.h"

namespace cromo {

// Low-level routines that the caller needs to implement
class SmsModemOperations {
 public:
  // Given an integer storage index, returns a new SmsMessageFragment object
  // representing that fragment on the device. On error, returns NULL and sets
  // the error parameter.
  virtual SmsMessageFragment* GetSms(int index,
                                     DBus::Error& error) = 0;  // NOLINT - refs.

  // Deletes the message fragment at the given index from the device.
  virtual void DeleteSms(int index,
                         DBus::Error& error) = 0;  // NOLINT - refs.

  // Return a list of the storage indexes of all of the message
  // fragments currently on the device.
  virtual std::vector<int>* ListSms(DBus::Error& error) = 0;  // NOLINT - refs.
};

// Cache of SMS messages and their index numbers in storage which
// assists in assembling multipart messages.

// Multipart messages are made out of several individual messages with
// the same reference number and part count. The multipart message as
// a whole is referred to by one index number, the canonical index
// number, which is generally the index number of the first part of
// the message seen by the cache. Most operations that take index
// numbers only take canonical index numbers and do not operate on
// bare message fragments.
class SmsCache {
 public:
  SmsCache() {}

  ~SmsCache();

  // The user of the cache invokes this when they receive notification
  // of a new message (fragment), passing in the storage index of the
  // new fragment.  If the fragment was a standalone message, or if
  // the fragment completes an existing multipart message, a
  // SmsMessage object is returned; otherwise this returns NULL.
  SmsMessage* SmsReceived(int index,
                          DBus::Error& error,  // NOLINT - refs.
                          SmsModemOperations* impl);

  // Retrieve a complete SMS message with the given canonical index.
  // Suitable for implementing org.freedesktop.ModemManager.Modem.Gsm.SMS.Get
  // Returns the SMS message as a DBusPropertyMap of key/value pairs.
  utilities::DBusPropertyMap* Get(int index,
                                  DBus::Error& error,  // NOLINT - refs.
                                  SmsModemOperations* impl);

  // Delete all fragments of a SMS message with a given canonical index
  // from the cache and from the underlying device.
  // Suitable for implementing org.freedesktop.ModemManager.Modem.Gsm.SMS.Delete
  void Delete(int index,
              DBus::Error& error,  // NOLINT - refs.
              SmsModemOperations* impl);

  // Return all of the complete SMS messages in the cache.
  // Suitable for implementing org.freedesktop.ModemManager.Modem.Gsm.SMS.List
  // Returns each SMS message as a DBusPropertyMap of key/value pairs.
  std::vector<utilities::DBusPropertyMap>* List(
      DBus::Error& error, SmsModemOperations* impl);  // NOLINT - refs.

 private:
  // Adds the message fragment to the cache, taking ownership of the
  // fragment.
  void AddToCache(SmsMessageFragment* message);

  // Get the message corresponding to the index number from the cache,
  // or NULL if there is no such message.
  // If the index refers to the canonical index of a multipart
  // message, the multipart message is returned rather than the
  // original fragment. If the index refers to a non-canonical index
  // of a multipart message, NULL is returned.
  SmsMessage* GetFromCache(int index);

  // Take the index number of a message fragment and return the
  // canonical index number of the message that fragment belongs to.
  // Returns -1 if no such fragment exists.
  int GetCanonicalIndex(int index);

  // Remove and free the message with the corresponding canonical index.
  void RemoveFromCache(int index);

  // Empty the entire cache.
  void ClearCache();

  // Messages by canonical index.
  // Owns messages and hence their fragments.
  std::map<int, SmsMessage*> messages_;

  // Mapping from fragment index to canonical index.
  std::map<int, int> fragments_;

  // Mapping from multipart reference numbers to canonical index
  // of corresponding messages.
  std::map<uint16_t, int> multiparts_;

  DISALLOW_COPY_AND_ASSIGN(SmsCache);
};

}  // namespace cromo

#endif  // CROMO_SMS_CACHE_H_
