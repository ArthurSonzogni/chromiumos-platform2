// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * FTDI device interface layer.
 * FTDI APP note AN_255 used as reference.
 */
#include <iomanip>
#include <iostream>
#include <vector>

#include <unistd.h>
#include <libusb-1.0/libusb.h>

#include "hps/lib/ftdi.h"

namespace {

const int kTimeoutMS = 500;                    // MS timeout
const int kResetDelay = 10;                    // MS delay after reset.
const uint16_t kClockDivisor = (300 / 2 - 1);  // Clock divisor (100KHz)
const int kReadSize = 64;
//  const int kStrSize = 64;
const bool kDebug = false;

// Commands to FTDI module.
enum {
  BYTE_OUT_RISING = 0x10,
  BYTE_OUT_FALLING = 0x11,
  BIT_OUT_RISING = 0x12,
  BIT_OUT_FALLING = 0x13,
  BYTE_IN_RISING = 0x20,
  BIT_IN_RISING = 0x22,
  BYTE_IN_FALLING = 0x24,
  BIT_IN_FALLING = 0x26,
  SET_PINS = 0x80,  // Write to ADBUS 0-7
  FLUSH = 0x87,
};

// ADBUS0/ADBUS1 bits for I2C I/O
enum {
  SCK = 1,
  SDATA = 2,
  GPIO = 8,  // For debugging.
};

// Set the state of the I/O pins.
void i2c_pins(std::vector<uint8_t>* b, uint8_t val, uint8_t dir) {
  b->push_back(SET_PINS);
  b->push_back(val);
  b->push_back(dir | GPIO);
}

// Add a Start sequence to the buffer.
void i2c_start(std::vector<uint8_t>* b) {
  for (auto i = 0; i < 10; i++) {
    i2c_pins(b, SCK | SDATA, SCK | SDATA);  // Let line be pulled up.
  }
  for (auto i = 0; i < 10; i++) {
    i2c_pins(b, SCK, SCK | SDATA);
  }
  for (auto i = 0; i < 10; i++) {
    i2c_pins(b, 0, SCK | SDATA);
  }
}

// Add a Stop sequence to the buffer.
void i2c_stop(std::vector<uint8_t>* b) {
  for (auto i = 0; i < 10; i++) {
    i2c_pins(b, 0, SCK | SDATA);
  }
  for (auto i = 0; i < 10; i++) {
    i2c_pins(b, SCK, SCK | SDATA);
  }
  for (auto i = 0; i < 10; i++) {
    i2c_pins(b, SCK | SDATA, SCK | SDATA);
  }
  i2c_pins(b, SCK | SDATA, 0);
}

}  // namespace

namespace hps {

bool Ftdi::Init() {
  ftdi_init(&this->context_);
  struct ftdi_device_list* devlist;

  // Read the list of all FTDI devices.
  // vid/pid of 0 will search for the default FTDI device types.
  if (this->check(ftdi_usb_find_all(&this->context_, &devlist, 0, 0) < 0,
                  "find"))
    return false;
  // Use the first device found. It's unlikely that multiple FTDI
  // devices will be attached - if so, some means of selecting the
  // correct device must be added.
  if (this->check(devlist == 0, "no device"))
    return false;
#if 0
  uint8_t bus = libusb_get_bus_number(devlist->dev);
  uint8_t addr = libusb_get_device_address(devlist->dev);
  char m[kStrSize], d[kStrSize], s[kStrSize];
  bool chk =
      this->check(ftdi_usb_get_strings(&this->context_, devlist->dev, m,
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
  if (this->check(ftdi_usb_open_dev(&this->context_, devlist->dev) < 0,
                  "open")) {
    ftdi_list_free(&devlist);
    return false;
  }
  ftdi_list_free(&devlist);
  if (this->check(ftdi_set_interface(&this->context_, INTERFACE_A) < 0,
                  "set interface"))
    return false;
  if (this->check(ftdi_usb_reset(&this->context_) < 0, "reset"))
    return false;
  if (this->check(ftdi_usb_purge_buffers(&this->context_) < 0, "flush"))
    return false;
  if (this->check(ftdi_set_event_char(&this->context_, 0, false) < 0,
                  "ev char"))
    return false;
  if (this->check(ftdi_set_error_char(&this->context_, 0, false) < 0,
                  "err char"))
    return false;
  if (this->check(ftdi_set_latency_timer(&this->context_, 16) < 0,
                  "set latency"))
    return false;
  if (this->check(ftdi_set_bitmode(&this->context_, 0xFF, BITMODE_RESET) < 0,
                  "mode reset"))
    return false;
  if (this->check(ftdi_set_bitmode(&this->context_, 0xFF, BITMODE_MPSSE) < 0,
                  "mode MPSSE"))
    return false;
  usleep(50 * 1000);
  std::vector<uint8_t> rdq;
  // Flush any data in the queue.
  this->ft_get(&rdq);
  // Device opened successfully, verify MPSSE mode.
  std::vector<uint8_t> tx(1);
  tx[0] = 0xAA;
  if (this->check(this->ft_put(tx) != tx.size(), "verify write"))
    return false;
  rdq.resize(2);
  if (this->check(!this->ft_read(2, &rdq) || rdq[0] != 0xFA || rdq[1] != 0xAA,
                  "verify read"))
    return false;
  tx.clear();
  // Init MPSSE settings.
  tx.push_back(0x8A);  // Disable divide/5 clock mode
  tx.push_back(0x97);  // Disable adaptive clocking
  tx.push_back(0x8C);  // Enable 3 phase clocking
  if (this->check(this->ft_put(tx) != tx.size(), "MPSEE setting"))
    return false;
  tx.clear();
  i2c_pins(&tx, SCK | SDATA, SCK);
  tx.push_back(0x86);  // Set clock divisor
  tx.push_back(kClockDivisor & 0xFF);
  tx.push_back(kClockDivisor >> 8);
  if (this->check(this->ft_put(tx) != tx.size(), "MPSEE clock setting"))
    return false;
  usleep(20 * 1000);
  tx.clear();
  tx.push_back(0x85);  // Turn off loopback.
  if (this->check(this->ft_put(tx) != tx.size(), "loopback disable"))
    return false;
  usleep(20 * 1000);
  if (kDebug)
    this->dump();
  return true;
}

void Ftdi::Close() {
  ftdi_deinit(&this->context_);
}

bool Ftdi::read(uint8_t cmd, std::vector<uint8_t>* data) {
  if (data->size() == 0) {
    return false;
  }
  std::vector<uint8_t> b;
  // Flush anything in the read queue.
  this->ft_get(&b);
  b.clear();
  i2c_start(&b);
  if (!this->ft_sendbyte(this->address, &b)) {
    this->i2c_reset();
    return false;
  }
  b.clear();
  if (!this->ft_sendbyte(cmd, &b)) {
    this->i2c_reset();
    return false;
  }
  b.clear();
  i2c_start(&b);
  if (!this->ft_sendbyte(this->address | 1, &b)) {
    this->i2c_reset();
    return false;
  }
  for (size_t i = 0; i < data->size(); i++) {
    if (!this->ft_readbyte(&(*data)[i], i == (data->size() - 1))) {
      this->i2c_reset();
      return false;
    }
  }
  return true;
}

bool Ftdi::write(uint8_t cmd, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> b;
  // Flush read queue.
  this->ft_get(&b);
  b.clear();
  i2c_start(&b);
  if (!this->ft_sendbyte(this->address, &b)) {
    this->i2c_reset();
    return false;
  }
  b.clear();
  if (!this->ft_sendbyte(cmd, &b)) {
    this->i2c_reset();
    return false;
  }
  for (auto it = data.begin(); it != data.end(); ++it) {
    b.clear();
    if (!this->ft_sendbyte(*it, &b)) {
      this->i2c_reset();
      return false;
    }
  }
  b.clear();
  i2c_stop(&b);
  if (this->ft_put(b) != b.size()) {
    return false;
  }
  return true;
}

// Read a block of data from the FTDI chip.
// A timeout is used in case it hangs.
bool Ftdi::ft_read(size_t count, std::vector<uint8_t>* input) {
  input->clear();
  std::vector<uint8_t> buf;
  int timeout = kTimeoutMS;
  while (count > 0) {
    // Read whatever is available.
    if (!this->ft_get(&buf)) {
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
      usleep(1000);  // Sleep for 1 ms
      if (--timeout <= 0) {
        std::cerr << "Rd timeout" << std::endl;
        return false;
      }
    }
  }
  return true;
}

// Send a byte to the I2C bus and wait for an ack/nak.
bool Ftdi::ft_sendbyte(uint8_t data, std::vector<uint8_t>* b) {
  // SDA/SCLK low.
  i2c_pins(b, 0, SCK | SDATA);
  b->push_back(BIT_OUT_FALLING);
  b->push_back(0x07);
  b->push_back(data);
  // Switch to SDA input to read ack/nak.
  i2c_pins(b, 0, SCK);
  b->push_back(BIT_IN_RISING);
  b->push_back(0x00);
  b->push_back(FLUSH);
  if (this->ft_put(*b) != b->size()) {
    return false;
  }
  b->clear();
  std::vector<uint8_t> in;
  // Check for nak.
  if (!this->ft_read(1, &in) || (in[0] & 0x01) != 0x00) {
    return false;
  }
  return true;
}

// Read a byte from the I2C and send a NAK/ACK in response.
bool Ftdi::ft_readbyte(uint8_t* result, bool nak) {
  std::vector<uint8_t> b;
  // SCK out/low, SDA in
  i2c_pins(&b, 0, SCK);
  b.push_back(BIT_IN_RISING);
  b.push_back(0x07);
  i2c_pins(&b, 0, SCK | SDATA);
  b.push_back(BIT_OUT_FALLING);
  b.push_back(0x00);
  b.push_back(nak ? 0x80 : 0x00);
  i2c_pins(&b, 0, SCK);
  b.push_back(FLUSH);
  if (this->ft_put(b) != b.size()) {
    return false;
  }
  // Read byte.
  b.resize(1);
  if (!this->ft_read(b.size(), &b)) {
    return false;
  }
  *result = b[0];
  if (nak) {
    // Send Stop.
    b.clear();
    i2c_stop(&b);
    if (this->ft_put(b) != b.size()) {
      return false;
    }
  }
  return true;
}

// Read from the module whatever data is available.
bool Ftdi::ft_get(std::vector<uint8_t>* get) {
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
size_t Ftdi::ft_put(const std::vector<uint8_t>& output) {
  return ftdi_write_data(&this->context_, const_cast<uint8_t*>(&output[0]),
                         output.size());
}

// Reset the state of the bus to idle.
void Ftdi::i2c_reset() {
  std::vector<uint8_t> b;
  i2c_stop(&b);
  this->ft_put(b);
  usleep(kResetDelay * 1000);
}

// Check and print error message.
bool Ftdi::check(bool cond, const char* tag) {
  if (cond) {
    std::cerr << "FTDI error: " << tag << ": "
              << ftdi_get_error_string(&this->context_) << std::endl;
    this->dump();
    return true;
  }
  return false;
}

void Ftdi::dump() {
  std::cerr << "Type: " << this->context_.type << " Interface: "
            << this->context_.interface << " index: " << this->context_.index
            << " IN_EP: " << this->context_.in_ep
            << " OUT_EP: " << this->context_.out_ep << std::endl;
  std::cerr << "Manuf: " << this->manuf_ << " Descr: " << this->descr_
            << " Serial: " << this->serial_ << std::endl;
  struct ftdi_version_info v = ftdi_get_library_version();
  std::cerr << "Lib version: " << v.version_str << std::endl;
}

}  // namespace hps
