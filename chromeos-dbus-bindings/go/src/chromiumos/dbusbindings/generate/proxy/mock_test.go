// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package proxy

import (
	"bytes"
	"testing"

	"github.com/google/go-cmp/cmp"

	"chromiumos/dbusbindings/introspect"
	"chromiumos/dbusbindings/serviceconfig"
)

func TestGenerateMockProxies(t *testing.T) {
	itf := introspect.Interface{
		Name: "fi.w1.wpa_supplicant1.Interface",
		Methods: []introspect.Method{
			{
				Name: "Scan",
				Args: []introspect.MethodArg{
					{
						Name: "args",
						Type: "ah",
					},
				},
			}, {
				Name: "PassMeProtos",
				Args: []introspect.MethodArg{
					{
						Name:      "request",
						Type:      "ay",
						Direction: "in",
						Annotation: introspect.Annotation{
							Name:  "org.chromium.DBus.Argument.ProtobufClass",
							Value: "PassMeProtosRequest",
						},
					},
				},
				Annotations: []introspect.Annotation{
					{
						Name:  "org.chromium.DBus.Method.Kind",
						Value: "async",
					},
				},
				DocString: "\n        method doc\n      ",
			},
		},
		Signals: []introspect.Signal{
			{
				Name: "BSSRemoved",
				Args: []introspect.SignalArg{
					{
						Name: "BSSDetail1",
						Type: "ay",
						Annotation: introspect.Annotation{
							Name:  "org.chromium.DBus.Argument.ProtobufClass",
							Value: "YetAnotherProto",
						},
					}, {
						Name: "BSSDetail2",
						Type: "(ih)",
					},
				},
				DocString: "\n        signal doc\n      ",
			},
		},
		Properties: []introspect.Property{
			{
				Name:      "Capabilities",
				Type:      "a{sv}",
				Access:    "read",
				DocString: "\n        property doc\n      ",
			},
		},
		DocString: "\n      interface doc\n    ",
	}

	emptyItf := introspect.Interface{
		Name: "EmptyInterface",
	}

	introspections := []introspect.Introspection{
		{
			Name:       "/org/chromium/Test",
			Interfaces: []introspect.Interface{itf},
		}, {
			Interfaces: []introspect.Interface{emptyItf},
		},
	}

	sc := serviceconfig.Config{
		ObjectManager: serviceconfig.ObjectManagerConfig{
			Name: "foo.bar.ObjectManager",
		},
	}
	out := new(bytes.Buffer)
	if err := GenerateMock(introspections, out, "/tmp/mock.h", "", sc); err != nil {
		t.Fatalf("Generate got error, want nil: %v", err)
	}

	const want = `// Automatic generation of D-Bus interface mock proxies for:
//  - fi.w1.wpa_supplicant1.Interface
//  - EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#define ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#include <string>
#include <vector>

#include <base/callback_forward.h>
#include <base/logging.h>
#include <brillo/any.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <gmock/gmock.h>

namespace fi {
namespace w1 {
namespace wpa_supplicant1 {

// Abstract interface proxy for fi::w1::wpa_supplicant1::Interface.
// interface doc
class InterfaceProxyInterface {
 public:
  virtual ~InterfaceProxyInterface() = default;

  virtual bool Scan(
      const std::vector<brillo::dbus_utils::FileDescriptor>& in_args,
      brillo::ErrorPtr* error,
      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) = 0;

  virtual void ScanAsync(
      const std::vector<brillo::dbus_utils::FileDescriptor>& in_args,
      base::OnceCallback<void()> success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) = 0;

  // method doc
  virtual bool PassMeProtos(
      const PassMeProtosRequest& in_request,
      brillo::ErrorPtr* error,
      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) = 0;

  // method doc
  virtual void PassMeProtosAsync(
      const PassMeProtosRequest& in_request,
      base::OnceCallback<void()> success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) = 0;

  virtual void RegisterBSSRemovedSignalHandler(
      const base::RepeatingCallback<void(const YetAnotherProto&,
                                         const std::tuple<int32_t, base::ScopedFD>&)>& signal_callback,
      dbus::ObjectProxy::OnConnectedCallback on_connected_callback) = 0;

  static const char* CapabilitiesName() { return "Capabilities"; }
  virtual const brillo::VariantDictionary& capabilities() const = 0;

  virtual const dbus::ObjectPath& GetObjectPath() const = 0;
  virtual dbus::ObjectProxy* GetObjectProxy() const = 0;

  virtual void SetPropertyChangedCallback(
      const base::RepeatingCallback<void(InterfaceProxyInterface*, const std::string&)>& callback) = 0;
};

}  // namespace wpa_supplicant1
}  // namespace w1
}  // namespace fi

namespace fi {
namespace w1 {
namespace wpa_supplicant1 {

// Mock object for InterfaceProxyInterface.
class InterfaceProxyMock : public InterfaceProxyInterface {
 public:
  InterfaceProxyMock() = default;
  InterfaceProxyMock(const InterfaceProxyMock&) = delete;
  InterfaceProxyMock& operator=(const InterfaceProxyMock&) = delete;

};
}  // namespace wpa_supplicant1
}  // namespace w1
}  // namespace fi

// Abstract interface proxy for EmptyInterface.
class EmptyInterfaceProxyInterface {
 public:
  virtual ~EmptyInterfaceProxyInterface() = default;

  virtual const dbus::ObjectPath& GetObjectPath() const = 0;
  virtual dbus::ObjectProxy* GetObjectProxy() const = 0;
};



// Mock object for EmptyInterfaceProxyInterface.
class EmptyInterfaceProxyMock : public EmptyInterfaceProxyInterface {
 public:
  EmptyInterfaceProxyMock() = default;
  EmptyInterfaceProxyMock(const EmptyInterfaceProxyMock&) = delete;
  EmptyInterfaceProxyMock& operator=(const EmptyInterfaceProxyMock&) = delete;

};


#endif  // ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
`

	if diff := cmp.Diff(out.String(), want); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}

func TestGenerateMockProxiesEmpty(t *testing.T) {
	emptyItf := introspect.Interface{
		Name: "EmptyInterface",
	}

	introspections := []introspect.Introspection{{
		Interfaces: []introspect.Interface{emptyItf},
	}}

	sc := serviceconfig.Config{}
	out := new(bytes.Buffer)
	if err := GenerateMock(introspections, out, "/tmp/mock.h", "", sc); err != nil {
		t.Fatalf("Generate got error, want nil: %v", err)
	}

	const want = `// Automatic generation of D-Bus interface mock proxies for:
//  - EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#define ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#include <string>
#include <vector>

#include <base/callback_forward.h>
#include <base/logging.h>
#include <brillo/any.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <gmock/gmock.h>


// Abstract interface proxy for EmptyInterface.
class EmptyInterfaceProxyInterface {
 public:
  virtual ~EmptyInterfaceProxyInterface() = default;

  virtual const dbus::ObjectPath& GetObjectPath() const = 0;
  virtual dbus::ObjectProxy* GetObjectProxy() const = 0;
};



// Mock object for EmptyInterfaceProxyInterface.
class EmptyInterfaceProxyMock : public EmptyInterfaceProxyInterface {
 public:
  EmptyInterfaceProxyMock() = default;
  EmptyInterfaceProxyMock(const EmptyInterfaceProxyMock&) = delete;
  EmptyInterfaceProxyMock& operator=(const EmptyInterfaceProxyMock&) = delete;

};


#endif  // ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
`

	if diff := cmp.Diff(out.String(), want); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}

func TestGenerateMockProxiesWithProxyPath(t *testing.T) {
	emptyItf := introspect.Interface{
		Name: "EmptyInterface",
	}

	introspections := []introspect.Introspection{{
		Interfaces: []introspect.Interface{emptyItf},
	}}

	sc := serviceconfig.Config{}
	out := new(bytes.Buffer)
	if err := GenerateMock(introspections, out, "/tmp/mock.h", "../proxy.h", sc); err != nil {
		t.Fatalf("Generate got error, want nil: %v", err)
	}

	const want = `// Automatic generation of D-Bus interface mock proxies for:
//  - EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#define ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#include <string>
#include <vector>

#include <base/callback_forward.h>
#include <base/logging.h>
#include <brillo/any.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <gmock/gmock.h>

#include "../proxy.h"



// Mock object for EmptyInterfaceProxyInterface.
class EmptyInterfaceProxyMock : public EmptyInterfaceProxyInterface {
 public:
  EmptyInterfaceProxyMock() = default;
  EmptyInterfaceProxyMock(const EmptyInterfaceProxyMock&) = delete;
  EmptyInterfaceProxyMock& operator=(const EmptyInterfaceProxyMock&) = delete;

};


#endif  // ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
`

	if diff := cmp.Diff(out.String(), want); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}
