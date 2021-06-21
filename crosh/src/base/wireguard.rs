// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "wireguard" for crosh which can configure and control a WireGuard service
// in Shill.

use std::collections::HashMap;

use dbus::{
    self,
    arg::{ArgType, RefArg, Variant},
    blocking::Connection,
};
use sys_util::error;
use system_api::client::OrgChromiumFlimflamManager;
use system_api::client::OrgChromiumFlimflamService;

use crate::dispatcher::{self, Arguments, Command, Dispatcher};
use crate::util::DEFAULT_DBUS_TIMEOUT;

// TODO(b/177877310): Add private-key and preshared-key for set command.
// TODO(b/177877310): Consider default values.
const USAGE: &str = r#"wireguard <cmd> [<args>]

Available subcommands:

list
    Show all configured WireGuard services.
show <name>
    Show the WireGuard service with name <name>.
new <name>
    Create a new WireGuard service with name <name>.
del <name>
    Delete the configured WireGuard service with name <name>.
set <name> [local-ip <ip>] [mtu <mtu>] [dns <ip1>[,<ip2>]...]
    Configure properties for the WireGuard service with name <name>. Most options should
    have the same meaning and usage as in `wireguard-tools` (and `wg-quick`). Exceptions
    are:
    - Only IPv4 is supported for the VPN overlay, so `local-ip` and `dns` should be set
      to IPv4 address(es).
connect <name>
    Connect to the configured WireGuard service with name <name>.
disconnect <name>
    Disconnect from the configured WireGuard service with name <name>.
"#;

// TODO(b/177877310): Call this function in mod.rs to enable the command after we have the chrome
// flag.
pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new(
            "wireguard".to_string(),
            USAGE.to_string(),
            "Utility to configure and control a WireGuard VPN service".to_string(),
        )
        .set_command_callback(Some(execute_wireguard)),
    );
}

#[derive(Debug)]
enum Error {
    InvalidArguments(String),
    ServiceNotFound(String),
    ServiceAlreadyExists(String),
    Internal(String),
}

fn execute_wireguard(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    let args: Vec<&str> = args.get_args().iter().map(String::as_str).collect();
    match args.as_slice() {
        [] => Err(Error::InvalidArguments("no command".to_string())),
        ["show"] | ["list"] => wireguard_list(),
        ["show", service_name] => wireguard_show(service_name),
        ["new", service_name] => wireguard_new(service_name),
        ["del", service_name] => wireguard_del(service_name),
        ["set", service_name, remaining @ ..] => wireguard_set(service_name, remaining),
        ["connect", service_name] => wireguard_connect(service_name),
        ["disconnect", service_name] => wireguard_disconnect(service_name),
        [other, ..] => Err(Error::InvalidArguments(other.to_string())),
    }
    .map_err(|err| match err {
        Error::InvalidArguments(val) => dispatcher::Error::CommandInvalidArguments(val),
        Error::ServiceNotFound(val) => {
            println!("WireGuard service with name {} does not exist", val);
            dispatcher::Error::CommandReturnedError
        }
        Error::ServiceAlreadyExists(val) => {
            println!("WireGuard service with name {} already exists", val);
            dispatcher::Error::CommandReturnedError
        }
        Error::Internal(val) => {
            error!("ERROR: {}", val);
            dispatcher::Error::CommandReturnedError
        }
    })
}

// Represents the properties returned by GetProperties method from D-Bus.
type InputPropMap = HashMap<String, Variant<Box<dyn RefArg>>>;
// Represents the properties nested as a value of another property in the InputPropMap.
type InnerPropMap<'a> = HashMap<&'a str, &'a dyn RefArg>;
// Represents the properties used to configure a service via D-Bus.
type OutputPropMap = HashMap<&'static str, Variant<Box<dyn RefArg>>>;

// Helper functions for reading a value with a specific type from a property map.
trait GetPropExt {
    fn get_arg(&self, key: &str) -> Result<&dyn RefArg, Error>;

    fn get_str(&self, key: &str) -> Result<&str, Error> {
        self.get_arg(key)?
            .as_str()
            .ok_or_else(|| get_prop_error(key, "str"))
    }

    fn get_string(&self, key: &str) -> Result<String, Error> {
        Ok(self.get_str(key)?.to_string())
    }

    fn get_i32(&self, key: &str) -> Result<i32, Error> {
        self.get_arg(key)?
            .as_i64()
            .map(|x| x as i32)
            .ok_or_else(|| get_prop_error(key, "i32"))
    }

    fn get_strings(&self, key: &str) -> Result<Vec<String>, Error> {
        self.get_arg(key)?
            .as_iter()
            .ok_or_else(|| get_prop_error(key, "vec"))?
            .map(|arg| {
                arg.as_str()
                    .ok_or_else(|| get_prop_error(key, "str"))
                    .map(|x| x.to_string())
            })
            .collect()
    }

    fn get_inner_prop_map(&self, key: &str) -> Result<InnerPropMap, Error> {
        parse_arg_to_map(self.get_arg(key)?).ok_or_else(|| get_prop_error(key, "map"))
    }

    fn get_inner_prop_maps(&self, key: &str) -> Result<Vec<InnerPropMap>, Error> {
        self.get_arg(key)?
            .as_iter()
            .ok_or_else(|| get_prop_error(key, "vec"))?
            .map(|arg| parse_arg_to_map(arg).ok_or_else(|| get_prop_error(key, "map")))
            .collect()
    }
}

fn get_prop_error(k: &str, t: &str) -> Error {
    Error::Internal(format!("Failed to parse properties {} with type {}", k, t))
}

fn parse_arg_to_map(arg: &dyn RefArg) -> Option<InnerPropMap> {
    let mut kvs = HashMap::new();
    let mut itr = arg.as_iter()?;
    while let Some(val) = itr.next() {
        let key = val.as_str()?;
        let next = itr.next()?;
        // Unwraps it if the value type is Variant, since it may affect iterating over the inner
        // value if the inner type is dict.
        if next.arg_type() == ArgType::Variant {
            let mut inner_itr = next.as_iter().unwrap();
            kvs.insert(key, inner_itr.next().unwrap());
        } else {
            kvs.insert(key, next);
        }
    }
    Some(kvs)
}

impl GetPropExt for InputPropMap {
    fn get_arg(&self, key: &str) -> Result<&dyn RefArg, Error> {
        Ok(&self
            .get(&key.to_string())
            .ok_or_else(|| get_prop_error(key, "ref_arg"))?
            .0)
    }
}

impl GetPropExt for InnerPropMap<'_> {
    fn get_arg(&self, key: &str) -> Result<&dyn RefArg, Error> {
        Ok(self
            .get(key)
            .ok_or_else(|| get_prop_error(key, "ref_arg"))?)
    }
}

// The following constants are defined in system_api/dbus/shill/dbus-constants.h.
// Also see shill/doc/service-api.txt for their meanings.
// Property names in a service.
const PROPERTY_TYPE: &str = "Type";
const PROPERTY_NAME: &str = "Name";
const PROPERTY_PROVIDER_TYPE: &str = "Provider.Type";
const PROPERTY_PROVIDER_HOST: &str = "Provider.Host";
const PROPERTY_WIREGUARD_PUBLIC_KEY: &str = "WireGuard.PublicKey";
const PROPERTY_WIREGUARD_PEERS: &str = "WireGuard.Peers";
const PROPERTY_STATIC_IP_CONFIG: &str = "StaticIPConfig";
const PROPERTY_SAVE_CREDENTIALS: &str = "SaveCredentials";

// Property names for WireGuard in "WireGuard.Peers".
const PROPERTY_PEER_PUBLIC_KEY: &str = "PublicKey";
const PROPERTY_PEER_ENDPOINT: &str = "Endpoint";
const PROPERTY_PEER_ALLOWED_IPS: &str = "AllowedIPs";
const PROPERTY_PEER_PERSISTENT_KEEPALIVE: &str = "PersistentKeepalive";

// Property names in "StaticIPConfig"
const PROPERTY_ADDRESS: &str = "Address";
const PROPERTY_NAME_SERVERS: &str = "NameServers";
const PROPERTY_MTU: &str = "Mtu";

// Property values.
const TYPE_VPN: &str = "vpn";
const TYPE_WIREGUARD: &str = "wireguard";

// Represents a WireGuard service in Shill.
#[derive(Clone, Debug, PartialEq)]
struct WireGuardService {
    path: Option<String>,
    name: String,
    local_ip: Option<String>,
    public_key: String,
    mtu: Option<i32>,
    name_servers: Option<Vec<String>>,
    peers: Vec<WireGuardPeer>,
}

#[derive(Clone, Debug, PartialEq)]
struct WireGuardPeer {
    public_key: String,
    endpoint: String,
    allowed_ips: String,
    persistent_keepalive: String,
}

impl WireGuardService {
    fn parse_from_prop_map(service_properties: &InputPropMap) -> Result<Self, Error> {
        let service_name = service_properties.get_str(PROPERTY_NAME)?;

        let not_wg_service_err =
            Error::ServiceNotFound(format!("{} is not a WireGuard VPN service", service_name));

        let service_type = service_properties.get_str(PROPERTY_TYPE)?;
        if service_type != TYPE_VPN {
            return Err(not_wg_service_err);
        }

        // Reads address, dns servers, and mtu from StaticIPConfig. This property and all the
        // sub-properties could be empty.
        let mut local_ip = None;
        let mut mtu = None;
        let mut name_servers = None;
        if let Ok(props) = service_properties.get_inner_prop_map(PROPERTY_STATIC_IP_CONFIG) {
            // Note that the ok() below may potentially ignore some real parsing failures. It
            // should not happen if shill is working correctly so we use ok() here for simplicity.
            local_ip = props.get_string(PROPERTY_ADDRESS).ok();
            name_servers = props.get_strings(PROPERTY_NAME_SERVERS).ok();
            mtu = props.get_i32(PROPERTY_MTU).ok();
        }

        // The value of the "Provider" property is a map. Translates it into a HashMap at first.
        let provider_properties = service_properties.get_inner_prop_map("Provider")?;

        if provider_properties.get_str(PROPERTY_TYPE)? != TYPE_WIREGUARD {
            return Err(not_wg_service_err);
        }

        let ret = WireGuardService {
            path: None,
            name: service_name.to_string(),
            local_ip,
            public_key: provider_properties.get_string(PROPERTY_WIREGUARD_PUBLIC_KEY)?,
            mtu,
            name_servers,
            peers: provider_properties
                .get_inner_prop_maps(PROPERTY_WIREGUARD_PEERS)?
                .into_iter()
                .map(|p| {
                    Ok(WireGuardPeer {
                        public_key: p.get_string(PROPERTY_PEER_PUBLIC_KEY)?,
                        endpoint: p.get_string(PROPERTY_PEER_ENDPOINT)?,
                        allowed_ips: p.get_string(PROPERTY_PEER_ALLOWED_IPS)?,
                        persistent_keepalive: p.get_string(PROPERTY_PEER_PERSISTENT_KEEPALIVE)?,
                    })
                })
                .collect::<Result<Vec<WireGuardPeer>, Error>>()?,
        };

        Ok(ret)
    }

    fn encode_into_prop_map(&self) -> OutputPropMap {
        let mut properties: OutputPropMap = HashMap::new();
        let mut insert_string_field = |k: &'static str, v: &str| {
            properties.insert(k, Variant(Box::new(v.to_string())));
        };

        insert_string_field(PROPERTY_TYPE, TYPE_VPN);
        insert_string_field(PROPERTY_NAME, &self.name);
        insert_string_field(PROPERTY_PROVIDER_TYPE, TYPE_WIREGUARD);
        insert_string_field(PROPERTY_PROVIDER_HOST, TYPE_WIREGUARD);

        let mut static_ip_properties = HashMap::new();
        let mut insert_ip_field = |k: &str, v: Box<dyn RefArg>| {
            static_ip_properties.insert(k.to_string(), Variant(v));
        };
        if let Some(local_ip) = &self.local_ip {
            insert_ip_field(PROPERTY_ADDRESS, Box::new(local_ip.clone()));
        }
        if let Some(name_servers) = &self.name_servers {
            insert_ip_field(PROPERTY_NAME_SERVERS, Box::new(name_servers.clone()));
        }
        if let Some(mtu) = self.mtu {
            insert_ip_field(PROPERTY_MTU, Box::new(mtu));
        }
        if !static_ip_properties.is_empty() {
            properties.insert(
                PROPERTY_STATIC_IP_CONFIG,
                Variant(Box::new(static_ip_properties)),
            );
        }

        let mut peers_buf = Vec::new();
        for peer in &self.peers {
            let mut peer_properties = HashMap::new();
            let mut insert_peer_field = |k: &str, v: &str| {
                peer_properties.insert(k.to_string(), v.to_string());
            };
            insert_peer_field(PROPERTY_PEER_PUBLIC_KEY, &peer.public_key);
            insert_peer_field(PROPERTY_PEER_ENDPOINT, &peer.endpoint);
            insert_peer_field(PROPERTY_PEER_ALLOWED_IPS, &peer.allowed_ips);
            insert_peer_field(
                PROPERTY_PEER_PERSISTENT_KEEPALIVE,
                &peer.persistent_keepalive,
            );
            peers_buf.push(peer_properties);
        }
        properties.insert(PROPERTY_WIREGUARD_PEERS, Variant(Box::new(peers_buf)));

        // Always persists keys.
        properties.insert(PROPERTY_SAVE_CREDENTIALS, Variant(Box::new(true)));

        properties
    }

    fn update_from_args(&mut self, args: &[&str]) -> Result<(), Error> {
        use option_util::*;

        let mut iter = args.iter();
        while let Some(key) = iter.next() {
            // Forwards the iterator to read the value for the currently processing option.
            let mut get_next_as_val = || {
                iter.next().ok_or_else(|| {
                    Error::InvalidArguments(format!(
                        "Option '{}' expects one parameter but none is given",
                        key
                    ))
                })
            };

            match *key {
                "local-ip" => self.local_ip = Some(parse_local_ip(get_next_as_val()?)?),
                "dns" => self.name_servers = Some(parse_dns(get_next_as_val()?)?),
                "mtu" => self.mtu = Some(parse_mtu(get_next_as_val()?)?),
                _ => return Err(Error::InvalidArguments(format!("Unknown option `{}`", key))),
            }
        }

        Ok(())
    }

    fn print(&self) {
        // TODO(b/177877310): Print the connection state.
        println!("name: {}", self.name);
        // Always shows local ip since it's mandatory.
        println!(
            "  local ip: {}",
            self.local_ip.as_ref().unwrap_or(&"".to_string())
        );
        println!("  public key: {}", self.public_key);
        println!("  private key: (hidden)");
        if let Some(dns) = &self.name_servers {
            println!("  name servers: {}", dns.join(", "));
        }
        if let Some(mtu) = self.mtu {
            println!("  mtu: {}", mtu);
        }
        println!();
        for p in &self.peers {
            println!("  peer: {}", p.public_key);
            println!("    preshared key: (hidden or not set)");
            println!("    endpoint: {}", p.endpoint);
            println!("    allowed ips: {}", p.allowed_ips);
            println!("    persistent keepalive: {}", p.persistent_keepalive);
            println!();
        }
    }
}

mod option_util {
    use std::net::Ipv4Addr;

    use super::Error;

    pub(super) fn parse_local_ip(val: &str) -> Result<String, Error> {
        val.parse::<Ipv4Addr>()
            .map(|ip| ip.to_string())
            .map_err(|_| {
                Error::InvalidArguments(format!(
                    "'local-ip' should contain a valid IPv4 address, but got '{}'",
                    val
                ))
            })
    }

    pub(super) fn parse_dns(val: &str) -> Result<Vec<String>, Error> {
        val.split(',')
            .map(|x| x.parse::<Ipv4Addr>())
            .map(|x| x.map(|y| y.to_string()))
            .collect::<Result<Vec<_>, _>>()
            .map_err(|_| {
                Error::InvalidArguments(format!(
                    "'dns' should be a comma-separated list of valid IPv4 addresses, but got '{}'",
                    val
                ))
            })
    }

    pub(super) fn parse_mtu(val: &str) -> Result<i32, Error> {
        match val.parse::<i32>() {
            // [576, 65536) as per wireguard-tools.
            Ok(x) if (576..65536).contains(&x) => Ok(x),
            _ => Err(Error::InvalidArguments(format!(
                "'mtu' should be in range from 576 to 65535, but got '{}'",
                val
            ))),
        }
    }
}

fn make_dbus_connection() -> Result<Connection, Error> {
    Connection::new_system()
        .map_err(|err| Error::Internal(format!("Failed to get D-Bus connection: {}", err)))
}

fn make_service_proxy<'a>(
    connection: &'a Connection,
    service_path: &'a str,
) -> dbus::blocking::Proxy<'a, &'a Connection> {
    connection.with_proxy("org.chromium.flimflam", service_path, DEFAULT_DBUS_TIMEOUT)
}

// Queries Shill to get all configured WireGuard services.
fn get_wireguard_services(connection: &Connection) -> Result<Vec<WireGuardService>, Error> {
    let manager_proxy = connection.with_proxy("org.chromium.flimflam", "/", DEFAULT_DBUS_TIMEOUT);
    let manager_properties: InputPropMap =
        OrgChromiumFlimflamManager::get_properties(&manager_proxy)
            .map_err(|err| Error::Internal(format!("Failed to get Manager properties: {}", err)))?;

    let mut wg_services = Vec::new();

    // The "Services" property should contain a list of D-Bus paths.
    const PROPERTY_SERVICES: &str = "Services";
    let services = manager_properties.get_strings(PROPERTY_SERVICES)?;
    for path in services {
        let proxy = make_service_proxy(connection, &path);
        let service_properties = OrgChromiumFlimflamService::get_properties(&proxy)
            .map_err(|err| Error::Internal(format!("Failed to get service properties: {}", err)))?;
        match WireGuardService::parse_from_prop_map(&service_properties) {
            Ok(mut service) => {
                service.path = Some(path.to_string());
                wg_services.push(service)
            }
            Err(Error::ServiceNotFound(_)) => continue,
            Err(other_err) => return Err(other_err),
        };
    }

    Ok(wg_services)
}

fn get_wireguard_service_by_name(
    connection: &Connection,
    service_name: &str,
) -> Result<WireGuardService, Error> {
    let services: Vec<WireGuardService> = get_wireguard_services(connection)?
        .into_iter()
        .filter(|x| x.name == service_name)
        .collect();

    match services.len() {
        0 => Err(Error::ServiceNotFound(service_name.to_string())),
        1 => Ok(services.into_iter().next().unwrap()),
        _ => Err(Error::Internal(
            "Found duplicated WireGuard services".to_string(),
        )),
    }
}

fn wireguard_list() -> Result<(), Error> {
    let connection = make_dbus_connection()?;
    let mut services = get_wireguard_services(&connection)?;
    services.sort_by(|a, b| a.name.cmp(&b.name));
    for service in &services {
        service.print()
    }
    Ok(())
}

fn wireguard_show(service_name: &str) -> Result<(), Error> {
    let connection = make_dbus_connection()?;
    get_wireguard_service_by_name(&connection, service_name)?.print();
    Ok(())
}

fn wireguard_new(service_name: &str) -> Result<(), Error> {
    let connection = make_dbus_connection()?;

    // Checks if there is already a service with the given name.
    match get_wireguard_service_by_name(&connection, service_name) {
        Ok(_) => return Err(Error::ServiceAlreadyExists(service_name.to_string())),
        Err(Error::ServiceNotFound(_)) => {}
        Err(err) => return Err(err),
    };

    let manager_proxy = connection.with_proxy("org.chromium.flimflam", "/", DEFAULT_DBUS_TIMEOUT);
    let service = WireGuardService {
        path: None,
        name: service_name.to_string(),
        local_ip: None,
        public_key: "".to_string(),
        name_servers: None,
        mtu: None,
        peers: Vec::new(),
    };
    manager_proxy
        .configure_service(service.encode_into_prop_map())
        .map_err(|err| Error::Internal(format!("Failed to configure service: {}", err)))?;

    println!("Service {} created", service_name);
    Ok(())
}

fn wireguard_set(service_name: &str, args: &[&str]) -> Result<(), Error> {
    let connection = make_dbus_connection()?;
    let mut wg_service = get_wireguard_service_by_name(&connection, service_name)?;
    wg_service.update_from_args(args)?;
    let manager_proxy = connection.with_proxy("org.chromium.flimflam", "/", DEFAULT_DBUS_TIMEOUT);
    let properties = wg_service.encode_into_prop_map();
    manager_proxy.configure_service(properties).map_err(|err| {
        error!("ERROR: Failed to configure service: {}", err);
        Error::Internal("".to_string())
    })?;

    println!("Service {} updated", service_name);
    Ok(())
}

fn wireguard_connect(service_name: &str) -> Result<(), Error> {
    let connection = make_dbus_connection()?;
    let service = get_wireguard_service_by_name(&connection, service_name)?;
    // TODO(b/177877310): Need to check if the service has all fields configured.
    make_service_proxy(&connection, &service.path.unwrap())
        .connect()
        .map_err(|err| Error::Internal(format!("Failed to connect service: {}", err)))?;

    println!("Connecting to {}..", service_name);
    Ok(())
}

fn wireguard_disconnect(service_name: &str) -> Result<(), Error> {
    let connection = make_dbus_connection()?;
    let service = get_wireguard_service_by_name(&connection, service_name)?;
    make_service_proxy(&connection, &service.path.unwrap())
        .disconnect()
        .map_err(|err| Error::Internal(format!("Failed to disconnect service: {}", err)))?;

    println!("Disconnecting from {}..", service_name);
    Ok(())
}

fn wireguard_del(service_name: &str) -> Result<(), Error> {
    let connection = make_dbus_connection()?;
    let service = get_wireguard_service_by_name(&connection, service_name)?;
    make_service_proxy(&connection, &service.path.unwrap())
        .remove()
        .map_err(|err| Error::Internal(format!("Failed to delete service: {}", err)))?;

    println!("Service {} was deleted", service_name);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    // Length of base64-encoded keys for WireGuard (Curve25519).
    const WG_BASE64_KEYLEN: usize = 44;

    struct TestVars {
        service: WireGuardService,
    }

    impl TestVars {
        fn new() -> Self {
            let public_key_a: String = ['A'; WG_BASE64_KEYLEN].iter().collect();
            let public_key_b: String = ['B'; WG_BASE64_KEYLEN].iter().collect();
            let peer_a = WireGuardPeer {
                public_key: public_key_a.clone(),
                endpoint: "10.0.8.1:13579".to_string(),
                allowed_ips: "0.0.0.0/0".to_string(),
                persistent_keepalive: "".to_string(),
            };
            let peer_b = WireGuardPeer {
                public_key: public_key_b.clone(),
                endpoint: "10.0.10.1:24680".to_string(),
                allowed_ips: "10.8.0.0/16,192.168.100.0/24".to_string(),
                persistent_keepalive: "3".to_string(),
            };
            let service = WireGuardService {
                path: None,
                name: "wg_test".to_string(),
                local_ip: Some("192.168.1.2".to_string()),
                public_key: "".to_string(),
                name_servers: None,
                mtu: None,
                peers: vec![peer_a.clone(), peer_b.clone()],
            };
            TestVars { service }
        }
    }

    #[test]
    fn test_update_local_ip() {
        let vars = TestVars::new();
        let cases = ["192.168.1.1", "10.8.0.3"];
        for c in cases.iter() {
            let mut expected = vars.service.clone();
            expected.local_ip = Some(c.to_string());
            let mut actual = vars.service.clone();
            actual.update_from_args(&["local-ip", c]).unwrap();
            assert_eq!(expected, actual);
        }
    }

    #[test]
    fn test_update_local_ip_invalid() {
        let vars = TestVars::new();
        let cases = ["", "192.168.1", "abcdabcd", "fe80::1", "1.2.3.4,1.2.3.5"];
        for c in cases.iter() {
            let mut actual = vars.service.clone();
            actual.update_from_args(&["local-ip", c]).unwrap_err();
        }
    }

    #[test]
    fn test_update_dns() {
        let vars = TestVars::new();
        let cases = ["8.8.8.8,1.2.3.4", "8.8.8.8"];
        for c in cases.iter() {
            let mut expected = vars.service.clone();
            expected.name_servers = Some(c.split(',').map(|x| x.to_string()).collect());
            let mut actual = vars.service.clone();
            actual.update_from_args(&["dns", c]).unwrap();
            assert_eq!(expected, actual);
        }
    }

    #[test]
    fn test_update_dns_invalid() {
        let vars = TestVars::new();
        let cases = ["", "192.168.1", "abcdabcd", "fe80::1", "8.8.8.8,abc"];
        for c in cases.iter() {
            let mut actual = vars.service.clone();
            actual.update_from_args(&["dns", c]).unwrap_err();
        }
    }

    #[test]
    fn test_update_mtu() {
        let vars = TestVars::new();
        let cases = ["576", "1000", "65535"];
        for c in cases.iter() {
            let mut expected = vars.service.clone();
            expected.mtu = Some(c.parse::<i32>().unwrap());
            let mut actual = vars.service.clone();
            actual.update_from_args(&["mtu", c]).unwrap();
            assert_eq!(expected, actual);
        }
    }

    #[test]
    fn test_update_mtu_invalid() {
        let vars = TestVars::new();
        let cases = ["", "-1", "0", "575", "1000.0", "65536", "abcde"];
        for c in cases.iter() {
            let mut actual = vars.service.clone();
            actual.update_from_args(&["mtu", c]).unwrap_err();
        }
    }
}
