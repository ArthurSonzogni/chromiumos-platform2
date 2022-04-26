// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package proxy outputs client-side bindings classes based on introspects.
package proxy

import (
	"io"
	"strings"
	"text/template"

	"chromiumos/dbusbindings/dbustype"
	"chromiumos/dbusbindings/generate/genutil"
	"chromiumos/dbusbindings/introspect"
	"chromiumos/dbusbindings/serviceconfig"
)

var funcMap = template.FuncMap{
	"add":                    func(a, b int) int { return a + b },
	"extractNameSpaces":      genutil.ExtractNameSpaces,
	"formatComment":          genutil.FormatComment,
	"makeFullItfName":        genutil.MakeFullItfName,
	"makeFullProxyName":      genutil.MakeFullProxyName,
	"makeMethodParams":       makeMethodParams,
	"makeMethodCallbackType": makeMethodCallbackType,
	"makeProxyInterfaceArgs": makeProxyInterfaceArgs,
	"makeProxyInterfaceName": genutil.MakeProxyInterfaceName,
	"makeProxyName":          genutil.MakeProxyName,
	"makePropertyBaseTypeExtract": func(p *introspect.Property) (string, error) {
		return p.BaseType(dbustype.DirectionExtract)
	},
	"makeProxyInArgTypeProxy": func(p *introspect.Property) (string, error) {
		return p.InArgType(dbustype.ReceiverProxy)
	},
	"makeSignalCallbackType": makeSignalCallbackType,
	"makeVariableName":       genutil.MakeVariableName,
	"nindent":                genutil.Nindent,
	"trimLeft": func(cutset, s string) string {
		// Swap the args to fit with template's context.
		return strings.TrimLeft(s, cutset)
	},
	"repeat":  strings.Repeat,
	"reverse": genutil.Reverse,
}

const (
	templateText = `// Automatic generation of D-Bus interfaces:
{{range .Introspects}}{{range .Interfaces -}}
//  - {{.Name}}
{{end}}{{end -}}

#ifndef {{.HeaderGuard}}
#define {{.HeaderGuard}}
#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/dbus_property.h>
#include <brillo/dbus/dbus_signal_handler.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_manager.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>

{{if .ObjectManagerName -}}
{{range extractNameSpaces .ObjectManagerName -}}
namespace {{.}} {
{{end -}}
class {{makeProxyName .ObjectManagerName}};
{{range extractNameSpaces .ObjectManagerName | reverse -}}
}  // namespace {{.}}
{{end}}
{{end -}}
{{range $introspect := .Introspects}}{{range $itf := .Interfaces -}}
{{- $itfName := makeProxyInterfaceName .Name -}}
{{template "proxyInterface" (makeProxyInterfaceArgs . $.ObjectManagerName) }}
{{range extractNameSpaces .Name -}}
namespace {{.}} {
{{end}}
// Interface proxy for {{makeFullItfName .Name}}.
{{formatComment .DocString 0 -}}
{{- $proxyName := makeProxyName .Name -}}
class {{$proxyName}} final : public {{$itfName}} {
 public:
{{- if or $.ObjectManagerName .Properties }}
  class PropertySet : public dbus::PropertySet {
   public:
    PropertySet(dbus::ObjectProxy* object_proxy,
                const PropertyChangedCallback& callback)
        : dbus::PropertySet{object_proxy,
                            "{{.Name}}",
                            callback} {
{{- range .Properties}}
      RegisterProperty({{.Name}}Name(), &{{makeVariableName .Name}});
{{- end}}
    }
    PropertySet(const PropertySet&) = delete;
    PropertySet& operator=(const PropertySet&) = delete;
{{range .Properties}}
    brillo::dbus_utils::Property<{{makePropertyBaseTypeExtract .}}> {{makeVariableName .Name}};
{{- end}}

  };
{{- end}}

{{- /* TODO(crbug.com/983008): Simplify the format into Chromium style. */ -}}
{{- if and $.ServiceName $introspect.Name (or (not $.ObjectManagerName) (not .Properties))}}
  {{$proxyName}}(const scoped_refptr<dbus::Bus>& bus) :
      bus_{bus},
      dbus_object_proxy_{
           bus_->GetObjectProxy(service_name_, object_path_)} {
  }
{{- else}}
  {{$proxyName}}(
      const scoped_refptr<dbus::Bus>& bus
{{- if not $.ServiceName}},
      const std::string& service_name
{{- end}}
{{- if not $introspect.Name}},
      const dbus::ObjectPath& object_path
{{- end}}
{{- if and $.ObjectManagerName .Properties}},
      PropertySet property_set
{{- end}}) :
          bus_{bus},
{{- if not $.ServiceName}}
          service_name_{service_name},
{{- end}}
{{- if not $introspect.Name}}
          object_path_{object_path},
{{- end}}
{{- if and $.ObjectManagerName .Properties}}
          property_set_{property_set},
{{- end}}
          dbus_object_proxy_{
              bus_->GetObjectProxy(service_name_, object_path_)} {
  }
{{- end}}

  {{$proxyName}}(const {{$proxyName}}&) = delete;
  {{$proxyName}}& operator=(const {{$proxyName}}&) = delete;

  ~{{$proxyName}}() override {
  }
{{- range .Signals}}

  void Register{{.Name}}SignalHandler(
      {{- makeSignalCallbackType .Args | nindent 6}} signal_callback,
      dbus::ObjectProxy::OnConnectedCallback on_connected_callback) override {
    brillo::dbus_utils::ConnectToSignal(
        dbus_object_proxy_,
        "{{$itf.Name}}",
        "{{.Name}}",
        signal_callback,
        std::move(on_connected_callback));
  }
{{- end}}

  void ReleaseObjectProxy(base::OnceClosure callback) {
    bus_->RemoveObjectProxy(service_name_, object_path_, std::move(callback));
  }

  const dbus::ObjectPath& GetObjectPath() const override {
    return object_path_;
  }

  dbus::ObjectProxy* GetObjectProxy() const override {
    return dbus_object_proxy_;
  }

{{- if .Properties}}
{{if $.ObjectManagerName}}
  void SetPropertyChangedCallback(
      const base::RepeatingCallback<void({{$itfName}}*, const std::string&)>& callback) override {
    on_property_changed_ = callback;
  }
{{- else}}
  void InitializeProperties(
      const base::RepeatingCallback<void({{$itfName}}*, const std::string&)>& callback) override {
{{- /* TODO(crbug.com/983008): Use std::make_unique. */}}
    property_set_.reset(
        new PropertySet(dbus_object_proxy_, base::BindRepeating(callback, this)));
    property_set_->ConnectSignals();
    property_set_->GetAll();
  }
{{- end}}

  const PropertySet* GetProperties() const { return &(*property_set_); }
  PropertySet* GetProperties() { return &(*property_set_); }
{{- end}}

{{- range .Methods}}
{{- $inParams := makeMethodParams 0 .InputArguments -}}
{{- $outParams := makeMethodParams (len .InputArguments) .OutputArguments}}

{{formatComment .DocString 2 -}}
{{"  "}}bool {{.Name}}(
{{- range $inParams }}
      {{.Type}} {{.Name}},
{{- end}}
{{- range $outParams }}
      {{.Type}} {{.Name}},
{{- end}}
      brillo::ErrorPtr* error,
      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) override {
    auto response = brillo::dbus_utils::CallMethodAndBlockWithTimeout(
        timeout_ms,
        dbus_object_proxy_,
        "{{$itf.Name}}",
        "{{.Name}}",
        error
{{- range $inParams }},
        {{.Name}}
{{- end}});
    return response && brillo::dbus_utils::ExtractMethodCallResults(
        response.get(), error{{range $i, $param := $outParams}}, {{.Name}}{{end}});
  }

{{formatComment .DocString 2 -}}
{{"  "}}void {{.Name}}Async(
{{- range $inParams}}
      {{.Type}} {{.Name}},
{{- end}}
      {{makeMethodCallbackType .OutputArguments}} success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) override {
    brillo::dbus_utils::CallMethodWithTimeout(
        timeout_ms,
        dbus_object_proxy_,
        "{{$itf.Name}}",
        "{{.Name}}",
        std::move(success_callback),
        std::move(error_callback)
{{- range $inParams}},
        {{.Name}}
{{- end}});
  }

{{- end}}

{{- range .Properties}}
{{- $name := makeVariableName .Name -}}
{{- $type := makeProxyInArgTypeProxy . }}

  {{$type}} {{$name}}() const override {
    return property_set_->{{$name}}.value();
  }
{{- if eq .Access "readwrite"}}

  void set_{{$name}}({{$type}} value,
           {{repeat " " (len $name)}} base::OnceCallback<void(bool)> callback) override {
    property_set_->{{$name}}.Set(value, std::move(callback));
  }
{{- end}}
{{- end}}

 private:
{{- if and $.ObjectManagerName .Properties}}
  void OnPropertyChanged(const std::string& property_name) {
    if (!on_property_changed_.is_null())
      on_property_changed_.Run(this, property_name);
  }
{{/* blank line separator */}}
{{- end}}
  scoped_refptr<dbus::Bus> bus_;
{{- if $.ServiceName}}
  const std::string service_name_{"{{$.ServiceName}}"};
{{- else}}
  std::string service_name_;
{{- end}}

{{- if $introspect.Name}}
  const dbus::ObjectPath object_path_{"{{$introspect.Name}}"};
{{- else}}
  dbus::ObjectPath object_path_;
{{- end}}
{{- if and $.ObjectManagerName .Properties}}
  PropertySet* property_set_;
  base::RepeatingCallback<void({{$itfName}}*, const std::string&)> on_property_changed_;
{{- end}}
  dbus::ObjectProxy* dbus_object_proxy_;
{{- if and (not $.ObjectManagerName) .Properties}}
  std::unique_ptr<PropertySet> property_set_;
{{- end}}
{{- if and $.ObjectManagerName .Properties}}
  friend class {{makeFullProxyName $.ObjectManagerName}};
{{- end}}

};

{{range extractNameSpaces .Name | reverse -}}
}  // namespace {{.}}
{{end}}
{{end}}{{end -}}
{{- if .ObjectManagerName }}
{{- range extractNameSpaces .ObjectManagerName}}
namespace {{.}} {
{{- end}}

{{ $className := makeProxyName .ObjectManagerName -}}
class {{$className}} : public dbus::ObjectManager::Interface {
 public:
  {{$className}}(const scoped_refptr<dbus::Bus>& bus
{{- if (not .ServiceName) }},
  {{repeat " " (len $className)}} const std::string& service_name
{{- end}})
      : bus_{bus},
{{- if (not .ServiceName) }}
        service_name_{service_name},
{{- end}}
        dbus_object_manager_{bus->GetObjectManager(
{{- if .ServiceName }}
            "{{.ServiceName}}",
{{- else}}
            service_name,
{{- end}}
            dbus::ObjectPath{"{{.ObjectManagerPath}}"})} {
{{- range .Introspects}}{{range .Interfaces}}
    dbus_object_manager_->RegisterInterface("{{.Name}}", this);
{{- end}}{{end}}
  }

  {{$className}}(const {{$className}}&) = delete;
  {{$className}}& operator=(const {{$className}}&) = delete;

  ~{{$className}}() override {
{{- range .Introspects}}{{range .Interfaces}}
    dbus_object_manager_->UnregisterInterface("{{.Name}}");
{{- end}}{{end}}
  }

  dbus::ObjectManager* GetObjectManagerProxy() const {
    return dbus_object_manager_;
  }

{{/* TODO(crbug.com/983008): Add public APIs. */}}

 private:
{{/* TODO(crbug.com/983008): Add private APIs. */}}
  base::WeakPtrFactory<{{$className}}> weak_ptr_factory_{this};
};
{{range extractNameSpaces .ObjectManagerName | reverse }}
}  // namespace {{.}}
{{- end}}
{{end}}
#endif  // {{.HeaderGuard}}
`
)

// Generate outputs the header file containing proxy interfaces into f.
// outputFilePath is used to make a unique header guard.
func Generate(introspects []introspect.Introspection, f io.Writer, outputFilePath string, config serviceconfig.Config) error {
	tmpl, err := template.New("proxy").Funcs(funcMap).Parse(templateText)
	if err != nil {
		return err
	}

	if _, err := tmpl.Parse(proxyInterfaceTemplate); err != nil {
		return err
	}

	var omName, omPath string
	if config.ObjectManager != nil {
		omName = config.ObjectManager.Name
		omPath = config.ObjectManager.ObjectPath
	}

	headerGuard := genutil.GenerateHeaderGuard(outputFilePath)
	return tmpl.Execute(f, struct {
		Introspects       []introspect.Introspection
		HeaderGuard       string
		ServiceName       string
		ObjectManagerName string
		ObjectManagerPath string
	}{
		Introspects:       introspects,
		HeaderGuard:       headerGuard,
		ServiceName:       config.ServiceName,
		ObjectManagerName: omName,
		ObjectManagerPath: omPath,
	})
}
