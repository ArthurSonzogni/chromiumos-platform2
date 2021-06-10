// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * FTDI device interface layer.
 * FTDI APP note AN_255 used as reference.
 */
#include "hps/hal/ftdi.h"

#include <iomanip>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include <libusb-1.0/libusb.h>
#include <stdlib.h>

#include <base/check.h>
#include <base/strings/string_number_conversions.h>
#include <base/threading/thread.h>
#include <base/time/time.h>

namespace {

static const int kTimeoutMS = 500;  // MS timeout
static const int kResetDelay = 10;  // MS delay after reset.
static const int kReadSize = 64;
static const bool kDebug = false;

// Commands to FTDI module.
enum {
  kByteOutRising = 0x10,
  kByteOutFalling = 0x11,
  kBitOutRising = 0x12,
  kBitOutFalling = 0x13,
  kByteInRising = 0x20,
  kBitInRising = 0x22,
  kByteInFalling = 0x24,
  kBitInFalling = 0x26,
  kSetPins = 0x80,  // Write to ADBUS 0-7
  kFlush = 0x87,
};

// ADBUS0/ADBUS1 bits for I2C I/O
enum {
  kSclock = 1,
  kSdata = 2,
  kGpio = 8,  // For debugging.
};

// Set the state of the I/O pins.
void Pins(std::vector<uint8_t>* b, uint8_t val, uint8_t dir) {
  b->push_back(kSetPins);
  b->push_back(val);
  b->push_back(dir | kGpio);
}

// Add a I2C Start sequence to the buffer.
void Start(std::vector<uint8_t>* b) {
  for (auto i = 0; i < 10; i++) {
    Pins(b, kSclock | kSdata, kSclock | kSdata);  // Let line be pulled up.
  }
  for (auto i = 0; i < 10; i++) {
    Pins(b, kSclock, kSclock | kSdata);
  }
  for (auto i = 0; i < 10; i++) {
    Pins(b, 0, kSclock | kSdata);
  }
}

// Add a I2C Stop sequence to the buffer.
void Stop(std::vector<uint8_t>* b) {
  for (auto i = 0; i < 10; i++) {
    Pins(b, 0, kSclock | kSdata);
  }
  for (auto i = 0; i < 10; i++) {
    Pins(b, kSclock, kSclock | kSdata);
  }
  for (auto i = 0; i < 10; i++) {
    Pins(b, kSclock | kSdata, kSclock | kSdata);
  }
  Pins(b, kSclock | kSdata, 0);
}

/*
 * Calculate clock divider from bus speed.
 * See AN 255 for a complete explanation of the clock divider formula.
 * For 2 phase clock:
 * speed = 60Mhz / ((1 + divisor) * 2)
 * For 3 phase clock, final divisor = divisor * 2 / 3;
 * So:
 *   speed = 60MHz / (((1 + divisor) * 2 / 3) * 2)
 *   divisor = 60000 / (speedKHz * 2) - 1
 *   divisor = divisor * 2 / 3
 */
uint16_t ClockDivisor(uint32_t speedKHz) {
  return (((60 * 1000) / (speedKHz * 2) - 1) * 2) / 3;
}

}  // namespace

namespace hps {

bool Ftdi::Init(uint32_t speedKHz) {
  // Max is 1MHz, minimum is 10Khz.
  if (speedKHz > 1000 || speedKHz < 10) {
    std::cerr << "FTDI illegal speed, max 1MHz, min 10KHz" << std::endl;
    return false;
  }
  ftdi_init(&this->context_);
  struct ftdi_device_list* devlist;

  // Read the list of all FTDI devices.
  // vid/pid of 0 will search for the default FTDI device types.
  if (this->Check(ftdi_usb_find_all(&this->context_, &devlist, 0, 0) < 0,
                  "find"))
    return false;
  // Use the first device found. It's unlikely that multiple FTDI
  // devices will be attached - if so, some means of selecting the
  // correct device must be added.
  if (this->Check(devlist == 0, "no device"))
    return false;
#if 0
  uint8_t bus = libusb_get_bus_number(devlist->dev);
  uint8_t addr = libusb_get_device_address(devlist->dev);
  char m[kStrSize], d[kStrSize], s[kStrSize];
  bool chk =
      this->Check(ftdi_usb_get_strings(&this->context_, devlist->dev, m,
                                       kStrSize, d, kStrSize, s, kStrSize) < 0,
                  "get str");
  // Free the device list after all the data has been extracted from it.
  ftdi_list_free(&devlist);
  if (chk)
    return false;
  this->manuf_ = m;
  this->descr_ = d;
  this->serial_ = s;
#endif
  if (this->Check(ftdi_usb_open_dev(&this->context_, devlist->dev) < 0,
                  "open")) {
    ftdi_list_free(&devlist);
    return false;
  }
  ftdi_list_free(&devlist);
  if (this->Check(ftdi_set_interface(&this->context_, INTERFACE_A) < 0,
                  "set interface"))
    return false;
  if (this->Check(ftdi_usb_reset(&this->context_) < 0, "reset"))
    return false;
  if (this->Check(ftdi_usb_purge_buffers(&this->context_) < 0, "flush"))
    return false;
  if (this->Check(ftdi_set_event_char(&this->context_, 0, false) < 0,
                  "ev char"))
    return false;
  if (this->Check(ftdi_set_error_char(&this->context_, 0, false) < 0,
                  "err char"))
    return false;
  if (this->Check(ftdi_set_latency_timer(&this->context_, 16) < 0,
                  "set latency"))
    return false;
  if (this->Check(ftdi_set_bitmode(&this->context_, 0xFF, BITMODE_RESET) < 0,
                  "mode reset"))
    return false;
  if (this->Check(ftdi_set_bitmode(&this->context_, 0xFF, BITMODE_MPSSE) < 0,
                  "mode MPSSE"))
    return false;
  base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(50));
  std::vector<uint8_t> rdq;
  // Flush any data in the queue.
  this->GetRaw(&rdq);
  // Device opened successfully, verify MPSSE mode.
  std::vector<uint8_t> tx(1);
  tx[0] = 0xAA;
  if (this->Check(this->PutRaw(tx) != tx.size(), "verify write"))
    return false;
  rdq.resize(2);
  if (this->Check(
          !this->GetRawBlock(2, &rdq) || rdq[0] != 0xFA || rdq[1] != 0xAA,
          "verify read"))
    return false;
  tx.clear();
  // Init MPSSE settings.
  tx.push_back(0x8A);  // Disable divide/5 clock mode
  tx.push_back(0x97);  // Disable adaptive clocking
  tx.push_back(0x8C);  // Enable 3 phase clocking
  if (this->Check(this->PutRaw(tx) != tx.size(), "MPSEE setting"))
    return false;
  tx.clear();
  Pins(&tx, kSclock | kSdata, kSclock);
  tx.push_back(0x86);  // Set clock divisor
  uint16_t div = ClockDivisor(speedKHz);
  tx.push_back(div & 0xFF);
  tx.push_back((div >> 8) & 0xFF);
  if (this->Check(this->PutRaw(tx) != tx.size(), "MPSEE clock setting"))
    return false;
  base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(20));
  tx.clear();
  tx.push_back(0x85);  // Turn off loopback.
  if (this->Check(this->PutRaw(tx) != tx.size(), "loopback disable"))
    return false;
  base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(20));
  if (kDebug)
    this->Dump();
  return true;
}

void Ftdi::Close() {
  ftdi_deinit(&this->context_);
}

bool Ftdi::Read(uint8_t cmd, uint8_t* data, size_t len) {
  if (len == 0) {
    return false;
  }
  std::vector<uint8_t> b;
  // Flush anything in the read queue.
  this->GetRaw(&b);
  b.clear();
  Start(&b);
  if (!this->SendByte(this->address_, &b)) {
    this->Reset();
    return false;
  }
  b.clear();
  if (!this->SendByte(cmd, &b)) {
    this->Reset();
    return false;
  }
  b.clear();
  Start(&b);
  if (!this->SendByte(this->address_ | 1, &b)) {
    this->Reset();
    return false;
  }
  for (size_t i = 0; i < len; i++) {
    if (!this->ReadByte(&data[i], i == (len - 1))) {
      this->Reset();
      return false;
    }
  }
  return true;
}

bool Ftdi::Write(uint8_t cmd, const uint8_t* data, size_t len) {
  std::vector<uint8_t> b;
  // Flush read queue.
  this->GetRaw(&b);
  b.clear();
  Start(&b);
  if (!this->SendByte(this->address_, &b)) {
    this->Reset();
    return false;
  }
  b.clear();
  if (!this->SendByte(cmd, &b)) {
    this->Reset();
    return false;
  }
  for (int i = 0; i < len; ++i) {
    b.clear();
    if (!this->SendByte(data[i], &b)) {
      this->Reset();
      return false;
    }
  }
  b.clear();
  Stop(&b);
  if (this->PutRaw(b) != b.size()) {
    return false;
  }
  return true;
}

// Read a block of raw data from the FTDI chip.
// A timeout is used in case it hangs.
bool Ftdi::GetRawBlock(size_t count, std::vector<uint8_t>* input) {
  input->clear();
  std::vector<uint8_t> buf;
  int timeout = kTimeoutMS;
  while (count > 0) {
    // Read whatever is available.
    if (!this->GetRaw(&buf)) {
      return false;
    }
    // Append to input buffer.
    if (buf.size() != 0) {
      if (buf.size() > count) {
        // Hmm extra data...
        buf.resize(count);
      }
      input->insert(input->end(), buf.begin(), buf.end());
      count -= buf.size();
    } else {
      // No data available, sleep for a while
      // and try again.
      base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(1));
      if (--timeout <= 0) {
        std::cerr << "Rd timeout" << std::endl;
        return false;
      }
    }
  }
  return true;
}

// Send a byte to the I2C bus and wait for an ack/nak.
bool Ftdi::SendByte(uint8_t data, std::vector<uint8_t>* b) {
  // SDA/SCLK low.
  Pins(b, 0, kSclock | kSdata);
  b->push_back(kBitOutFalling);
  b->push_back(0x07);
  b->push_back(data);
  // Switch to SDA input to read ack/nak.
  Pins(b, 0, kSclock);
  b->push_back(kBitInRising);
  b->push_back(0x00);
  b->push_back(kFlush);
  if (this->PutRaw(*b) != b->size()) {
    return false;
  }
  b->clear();
  std::vector<uint8_t> in;
  // Check for nak.
  if (!this->GetRawBlock(1, &in) || (in[0] & 0x01) != 0x00) {
    return false;
  }
  return true;
}

// Read a byte from the I2C and send a NAK/ACK in response.
bool Ftdi::ReadByte(uint8_t* result, bool nak) {
  std::vector<uint8_t> b;
  // SCK out/low, SDA in
  Pins(&b, 0, kSclock);
  b.push_back(kBitInRising);
  b.push_back(0x07);
  Pins(&b, 0, kSclock | kSdata);
  b.push_back(kBitOutFalling);
  b.push_back(0x00);
  b.push_back(nak ? 0x80 : 0x00);
  Pins(&b, 0, kSclock);
  b.push_back(kFlush);
  if (this->PutRaw(b) != b.size()) {
    return false;
  }
  // Read byte.
  b.resize(1);
  if (!this->GetRawBlock(b.size(), &b)) {
    return false;
  }
  *result = b[0];
  if (nak) {
    // Send Stop.
    b.clear();
    Stop(&b);
    if (this->PutRaw(b) != b.size()) {
      return false;
    }
  }
  return true;
}

// Read from the module whatever data is available.
bool Ftdi::GetRaw(std::vector<uint8_t>* get) {
  get->resize(kReadSize);
  int actual = ftdi_read_data(&this->context_, &(*get)[0], get->size());
  // Nothing to read, return empty vector.
  if (actual <= 0) {
    get->clear();
    return true;
  }
  get->resize(actual);
  return true;
}

// Write the data to the module..
size_t Ftdi::PutRaw(const std::vector<uint8_t>& output) {
  return ftdi_write_data(&this->context_, const_cast<uint8_t*>(&output[0]),
                         output.size());
}

// Reset the state of the bus to idle.
void Ftdi::Reset() {
  std::vector<uint8_t> b;
  Stop(&b);
  this->PutRaw(b);
  base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(kResetDelay));
}

// Check and print error message.
bool Ftdi::Check(bool cond, const char* tag) {
  if (cond) {
    std::cerr << "FTDI error: " << tag << ": "
              << ftdi_get_error_string(&this->context_) << std::endl;
    this->Dump();
    return true;
  }
  return false;
}

void Ftdi::Dump() {
  std::cerr << "Type: " << this->context_.type << " Interface: "
            << this->context_.interface << " index: " << this->context_.index
            << " IN_EP: " << this->context_.in_ep
            << " OUT_EP: " << this->context_.out_ep << std::endl;
  std::cerr << "Manuf: " << this->manuf_ << " Descr: " << this->descr_
            << " Serial: " << this->serial_ << std::endl;
  struct ftdi_version_info v = ftdi_get_library_version();
  std::cerr << "Lib version: " << v.version_str << std::endl;
}

// Static factory method.
std::unique_ptr<DevInterface> Ftdi::Create(uint8_t address, uint32_t speedKHz) {
  // Use new so that private constructor can be accessed.
  auto dev = std::unique_ptr<Ftdi>(new Ftdi(address));
  CHECK(dev->Init(speedKHz));
  return std::unique_ptr<DevInterface>(std::move(dev));
}

}  // namespace hps
