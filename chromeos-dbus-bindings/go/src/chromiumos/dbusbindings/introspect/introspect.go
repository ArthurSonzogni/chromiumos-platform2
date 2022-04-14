// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package introspect provides data type of introspection and its utility.
// Method and signal handlers are generated from introspection.
package introspect

import (
	"chromiumos/dbusbindings/dbustype"
	"fmt"
)

// TODO(chromium:983008): Add checks for the presence of unexpected elements in XML files.

// MethodKind is an enum to represent the kind of a method.
type MethodKind int

const (
	// MethodKindSimple indicates that the method doesn't fail and no brillo::ErrorPtr argument is given.
	MethodKindSimple MethodKind = iota

	// MethodKindNormal indicates that the method returns false and sets a brillo::ErrorPtr on failure.
	MethodKindNormal

	// MethodKindAsync indicates that instead of returning "out" arguments directly,
	// the method takes a DBusMethodResponse argument templated on the types of the "out" arguments.
	MethodKindAsync

	// MethodKindRaw indicates that the method takes a dbus::MethodCall and dbus::ExportedObject::ResponseSender
	// object directly.
	MethodKindRaw
)

// Annotation adds settings to MethodArg, SignalArg and Method.
type Annotation struct {
	Name  string `xml:"name,attr"`
	Value string `xml:"value,attr"`
}

// MethodArg represents method argument or return value.
type MethodArg struct {
	Name      string `xml:"name,attr"`
	Type      string `xml:"type,attr"`
	Direction string `xml:"direction,attr"`
	// For now, MethodArg supports only ProtobufClass annotation only,
	// so it can have at most one annotation.
	Annotation Annotation `xml:"annotation"`
}

// Method represents method provided by a object through a interface.
// TODO(crbug.com/983008): Some xml files are missing tp namespace; add
// "http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0" xml tag to DocString after
// fixing.
type Method struct {
	Name        string       `xml:"name,attr"`
	Args        []MethodArg  `xml:"arg"`
	Annotations []Annotation `xml:"annotation"`
	DocString   string       `xml:"docstring"`
}

// SignalArg represents signal message.
type SignalArg struct {
	Name string `xml:"name,attr"`
	Type string `xml:"type,attr"`
	// For now, MethodArg supports only ProtobufClass annotation only,
	// so it can have at most one annotation.
	Annotation Annotation `xml:"annotation"`
}

// Signal represents signal provided by a object through a interface.
// TODO(crbug.com/983008): Some xml files are missing tp namespace; add
// "http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0" xml tag to DocString after
// fixing.
type Signal struct {
	Name      string      `xml:"name,attr"`
	Args      []SignalArg `xml:"arg"`
	DocString string      `xml:"docstring"`
}

// Property represents property provided by a object through a interface.
// TODO(crbug.com/983008): Some xml files are missing tp namespace; add
// "http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0" xml tag to DocString after
// fixing.
type Property struct {
	Name      string `xml:"name,attr"`
	Type      string `xml:"type,attr"`
	Access    string `xml:"access,attr"`
	DocString string `xml:"docstring"`
}

// Interface represents interface provided by a object.
// TODO(crbug.com/983008): Some xml files are missing tp namespace; add
// "http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0" xml tag to DocString after
// fixing.
type Interface struct {
	Name       string     `xml:"name,attr"`
	Methods    []Method   `xml:"method"`
	Signals    []Signal   `xml:"signal"`
	Properties []Property `xml:"property"`
	DocString  string     `xml:"docstring"`
}

// Introspection represents object specification required for generating
// method and signal handlers.
type Introspection struct {
	Name       string      `xml:"name,attr"`
	Interfaces []Interface `xml:"interface"`
}

// InputArguments returns the array of input arguments extracted from method arguments.
func (m *Method) InputArguments() []MethodArg {
	var ret []MethodArg
	for _, a := range m.Args {
		if a.Direction == "in" || a.Direction == "" { // default direction is "in"
			ret = append(ret, a)
		}
	}
	return ret
}

// OutputArguments returns the array of output arguments extracted from method arguments.
func (m *Method) OutputArguments() []MethodArg {
	var ret []MethodArg
	for _, a := range m.Args {
		if a.Direction == "out" {
			ret = append(ret, a)
		}
	}
	return ret
}

// Kind returns the kind of method.
func (m *Method) Kind() MethodKind {
	for _, a := range m.Annotations {
		// Support GLib.Async annotation as well.
		if a.Name == "org.freedesktop.DBus.GLib.Async" {
			return MethodKindAsync
		}

		if a.Name == "org.chromium.DBus.Method.Kind" {
			switch a.Value {
			case "simple":
				return MethodKindSimple
			case "normal":
				return MethodKindNormal
			case "async":
				return MethodKindAsync
			case "raw":
				return MethodKindRaw
			}
		}
	}

	return MethodKindNormal
}

// IncludeDBusMessage returns true if the method needs a message argument added.
func (m *Method) IncludeDBusMessage() bool {
	for _, a := range m.Annotations {
		if a.Name == "org.chromium.DBus.Method.IncludeDBusMessage" {
			return a.Value == "true"
		}
	}
	return false
}

// Const returns true if the method is a const member function.
func (m *Method) Const() bool {
	for _, a := range m.Annotations {
		if a.Name == "org.chromium.DBus.Method.Const" {
			return a.Value == "true"
		}
	}
	return false
}

// BaseType returns the C++ type corresponding to the type that the argument describes.
func (a *MethodArg) BaseType(dir dbustype.Direction) (string, error) {
	return baseTypeInternal(a.Type, dir, &a.Annotation)
}

// InArgType returns the C++ type corresponding to the type that the argument describes
// for an in argument.
func (a *MethodArg) InArgType(receiver dbustype.Receiver) (string, error) {
	return inArgTypeInternal(a.Type, receiver, &a.Annotation)
}

// OutArgType returns the C++ type corresponding to the type that the argument describes
// for an out argument.
func (a *MethodArg) OutArgType(receiver dbustype.Receiver) (string, error) {
	return outArgTypeInternal(a.Type, receiver, &a.Annotation)
}

// BaseType returns the C++ type corresponding to the type that the argument describes.
func (a *SignalArg) BaseType(dir dbustype.Direction) (string, error) {
	return baseTypeInternal(a.Type, dir, &a.Annotation)
}

// InArgType returns the C++ type corresponding to the type that the argument describes
// for an in argument.
func (a *SignalArg) InArgType(receiver dbustype.Receiver) (string, error) {
	return inArgTypeInternal(a.Type, receiver, &a.Annotation)
}

// OutArgType returns the C++ type corresponding to the type that the argument describes
// for an out argument.
func (a *SignalArg) OutArgType(receiver dbustype.Receiver) (string, error) {
	return outArgTypeInternal(a.Type, receiver, &a.Annotation)
}

// BaseType returns the C++ type corresponding to the type that the property describes.
func (p *Property) BaseType(dir dbustype.Direction) (string, error) {
	return baseTypeInternal(p.Type, dir, nil)
}

// InArgType returns the C++ type corresponding to the type that the property describes
// for an in argument.
func (p *Property) InArgType(receiver dbustype.Receiver) (string, error) {
	return inArgTypeInternal(p.Type, receiver, nil)
}

// OutArgType returns the C++ type corresponding to the type that the property describes
// for an out argument.
func (p *Property) OutArgType(receiver dbustype.Receiver) (string, error) {
	return outArgTypeInternal(p.Type, receiver, nil)
}

func baseTypeInternal(s string, dir dbustype.Direction, a *Annotation) (string, error) {
	// chromeos-dbus-binding supports native protobuf types.
	if a != nil && a.Name == "org.chromium.DBus.Argument.ProtobufClass" {
		return a.Value, nil
	}

	typ, err := dbustype.Parse(s)
	if err != nil {
		return "", err
	}
	return typ.BaseType(dir), nil
}

func inArgTypeInternal(s string, receiver dbustype.Receiver, a *Annotation) (string, error) {
	// chromeos-dbus-binding supports native protobuf types.
	if a != nil && a.Name == "org.chromium.DBus.Argument.ProtobufClass" {
		return fmt.Sprintf("const %s&", a.Value), nil
	}

	typ, err := dbustype.Parse(s)
	if err != nil {
		return "", err
	}
	return typ.InArgType(receiver), nil
}

func outArgTypeInternal(s string, receiver dbustype.Receiver, a *Annotation) (string, error) {
	// chromeos-dbus-binding supports native protobuf types.
	if a != nil && a.Name == "org.chromium.DBus.Argument.ProtobufClass" {
		return fmt.Sprintf("%s*", a.Value), nil
	}

	typ, err := dbustype.Parse(s)
	if err != nil {
		return "", err
	}
	return typ.OutArgType(receiver), nil
}
