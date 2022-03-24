// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package dbustype provides utility functions for generators to parse a signature and
// generate the corresponding C++ type.
package dbustype

// Direction is an enum to represent if you are reading the argument from a message or not.
type Direction int

const (
	// DirectionExtract indicates that you are reading an argument from a message.
	DirectionExtract Direction = iota

	// DirectionAppend indicates that you are in other cases.
	DirectionAppend
)

// Receiver is an enum to represent what you are generating.
type Receiver int

const (
	// ReceiverAdaptor indicates that you are generating an adaptor.
	ReceiverAdaptor Receiver = iota

	// ReceiverProxy indicates that you are generating a proxy.
	ReceiverProxy
)

// DBusType represents a D-Bus type.
type DBusType struct {
	// TODO(chromium:983008): Implement struct contents.
}

// ParseSignature returns a DBusType corresponding to the D-Bus signature given in |signature|.
// |signature| needs to be a single complete type.
func ParseSignature(signature string) (DBusType, error) {
	// TODO(chromium:983008): Implement function contents.
	return DBusType{}, nil
}

// BaseType returns the C++ type corresponding to a D-Bus type.
func (d *DBusType) BaseType(direction Direction) string {
	// TODO(chromium:983008): Implement function contents.
	return "i"
}

// InArgType returns the C++ type corresponding to a D-Bus type of a receiver.
// Use these if possible, they will give you e.g. the correct reffiness.
func (d *DBusType) InArgType(receiver Receiver) string {
	// TODO(chromium:983008): Implement function contents.
	return "i"
}

// OutArgType returns the C++ type corresponding to a D-Bus type of a receiver.
// Use these if possible, they will give you e.g. the correct reffiness.
func (d *DBusType) OutArgType(receiver Receiver) string {
	// TODO(chromium:983008): Implement function contents.
	return "i"
}
