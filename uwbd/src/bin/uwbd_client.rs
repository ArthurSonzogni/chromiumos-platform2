// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provide a simple uwbd client, used to verify the uwbd service end-to-end.

use std::error::Error;
use std::time::Duration;

use clap::{arg, Command, ArgGroup};
use dbus::blocking::{Connection, Proxy};
use protobuf::{Message, RepeatedField};
use uwb_core::proto::bindings::{
    EnableResponse, InitSessionRequest, SessionType, FiraAppConfigParams, DeviceType,
    RangingRoundUsage, StsConfig, MultiNodeMode, UwbChannel, MacFcsType, RangingRoundControl,
    AoaResultRequest, RangeDataNtfConfig, DeviceRole, RframeConfig, PsduDataRate, RangingTimeStruct,
    TxAdaptivePayloadPower, PrfMode, ScheduledMode, KeyRotation, MacAddressMode, HoppingMode,
    ResultReportConfig, BprfPhrDataRate, StsLength, InitSessionResponse, PreambleDuration,
    StartRangingRequest, StartRangingResponse, StopRangingRequest, StopRangingResponse,
    DeinitSessionRequest, DeinitSessionResponse,
};
use uwb_core::proto::utils::write_to_bytes;

use uwbd::common::{DBUS_OBJECT_PATH, DBUS_SERVICE_NAME};
use uwbd::dbus_bindings::client::OrgChromiumUwbd;

// Returns the clap Command that this CLI will use
fn cli() -> Command {
    Command::new("uwbd_client")
        .about("CLI connecting to uwbd to start and stop UWB ranging sessions")
        .subcommand_required(true)
        .arg_required_else_help(true)
        .subcommand(
            Command::new("start")
                .about("Enables UWB, initializes and starts a ranging session.")
                .args_conflicts_with_subcommands(true)
                .arg(arg!(-i --initiator "Uses the initiator + controller parameter profile"))
                .arg(arg!(-r --responder "Uses the responder + controlee parameter profile"))
                .group(ArgGroup::new("vers")
                    .args(["initiator", "responder"])
                    .required(true))
            )
        .subcommand(
            Command::new("stop")
                .about("stops and deinits the default UWB ranging session")
        )
}


// Returns a FiraAppConfigParams that has been initialized with a default set of parameters.
// The profile specific parameters i.e. Device Type, Device Role, Mac Address and Destination
// Mac Addresses are not set.
fn create_app_config_params() -> FiraAppConfigParams {
    let mut app_config_params = FiraAppConfigParams::new();
    app_config_params.set_device_type(DeviceType::CONTROLEE);
    app_config_params.set_ranging_round_usage(RangingRoundUsage::DS_TWR);
    app_config_params.set_sts_config(StsConfig::STATIC);
    app_config_params.set_multi_node_mode(MultiNodeMode::UNICAST);
    app_config_params.set_channel_number(UwbChannel::CHANNEL_9);

    app_config_params.set_slot_duration_rstu(2400);
    app_config_params.set_ranging_interval_ms(200);
    app_config_params.set_mac_fcs_type(MacFcsType::CRC_16);

    let mut ranging_round_control = RangingRoundControl::new();
    ranging_round_control.set_ranging_result_report_message(true);
    ranging_round_control.set_control_message(true);
    ranging_round_control.set_measurement_report_message(false);
    app_config_params.set_ranging_round_control(ranging_round_control);
    app_config_params.set_aoa_result_request(AoaResultRequest::REQ_AOA_RESULTS);
    app_config_params.set_range_data_ntf_config(RangeDataNtfConfig::RANGE_DATA_NTF_CONFIG_ENABLE);
    app_config_params.set_range_data_ntf_proximity_near_cm(0);
    app_config_params.set_range_data_ntf_proximity_far_cm(20000);
    app_config_params.set_device_role(DeviceRole::RESPONDER);
    app_config_params.set_rframe_config(RframeConfig::SP3);
    app_config_params.set_preamble_code_index(9);
    app_config_params.set_sfd_id(2);
    app_config_params.set_psdu_data_rate(PsduDataRate::RATE_6M_81);
    app_config_params.set_preamble_duration(PreambleDuration::T64_SYMBOLS);
    app_config_params.set_ranging_time_struct(RangingTimeStruct::BLOCK_BASED_SCHEDULING);
    app_config_params.set_slots_per_rr(25);
    app_config_params.set_tx_adaptive_payload_power(TxAdaptivePayloadPower::
        TX_ADAPTIVE_PAYLOAD_POWER_DISABLE);
    app_config_params.set_responder_slot_index(1);
    app_config_params.set_prf_mode(PrfMode::BPRF);
    app_config_params.set_scheduled_mode(ScheduledMode::TIME_SCHEDULED_RANGING);
    app_config_params.set_key_rotation(KeyRotation::KEY_ROTATION_DISABLE);
    app_config_params.set_key_rotation_rate(0);
    app_config_params.set_session_priority(70);
    app_config_params.set_mac_address_mode(MacAddressMode::MAC_ADDRESS_2_BYTES);
    app_config_params.set_vendor_id(vec![0x0,0x0]);
    app_config_params.set_static_sts_iv(vec![0x0,0x0,0x0,0x0,0x0,0x0]);
    app_config_params.set_number_of_sts_segments(1);
    app_config_params.set_max_rr_retry(0);
    app_config_params.set_uwb_initiation_time_ms(0);
    app_config_params.set_hopping_mode(HoppingMode::HOPPING_MODE_DISABLE);
    app_config_params.set_block_stride_length(0);
    let mut result_report_config = ResultReportConfig::new();
    result_report_config.set_tof(true);
    result_report_config.set_aoa_azimuth(false);
    result_report_config.set_aoa_elevation(false);
    result_report_config.set_aoa_fom(false);
    app_config_params.set_result_report_config(result_report_config);
    app_config_params.set_in_band_termination_attempt_count(1);
    app_config_params.set_sub_session_id(0);
    app_config_params.set_bprf_phr_data_rate(BprfPhrDataRate::BPRF_PHR_DATA_RATE_850K);
    app_config_params.set_max_number_of_measurements(0);
    app_config_params.set_sts_length(StsLength::LENGTH_64);
    app_config_params.set_number_of_range_measurements(0); //dropped by HAL
    app_config_params.set_number_of_aoa_azimuth_measurements(0); //dropped by HAL
    app_config_params.set_number_of_aoa_elevation_measurements(0); //dropped by HAL

    app_config_params
}


fn main() -> Result<(), Box<dyn Error>> {
    let matches = cli().get_matches();

    let session_id = 1;
    const TIMEOUT: Duration = Duration::from_millis(5000);

    // Connect to D-Bus and create uwbd proxy.
    let dbus_connection = Connection::new_system()?;
    let uwbd_proxy = Proxy::new(
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        TIMEOUT,
        &dbus_connection,
    );

    match matches.subcommand(){
        Some(("start", sub_matches)) => {
            // Create params
            let mut app_config_params = create_app_config_params();

            // Set initiator profile in params
            if sub_matches.get_flag("initiator"){
                app_config_params.set_device_type(DeviceType::CONTROLLER);
                app_config_params.set_device_role(DeviceRole::INITIATOR);
                app_config_params.set_device_mac_address(vec![0x08,0x00]);
                let mut dst_mac_addresses: RepeatedField<Vec<u8>> = RepeatedField::new();
                dst_mac_addresses.push(vec![0x04,0x00]);
                app_config_params.set_dst_mac_address(dst_mac_addresses);
                println!("Using initiator profile");
            }
            // Set responder profile in params
            if sub_matches.get_flag("responder"){
                app_config_params.set_device_type(DeviceType::CONTROLEE);
                app_config_params.set_device_role(DeviceRole::RESPONDER);
                app_config_params.set_device_mac_address(vec![0x04,0x00]);
                let mut dst_mac_addresses: RepeatedField<Vec<u8>> = RepeatedField::new();
                dst_mac_addresses.push(vec![0x08,0x00]);
                app_config_params.set_dst_mac_address(dst_mac_addresses);
                println!("Using responder profile");
            }

            // Enable UwB
            let result_bytes = uwbd_proxy.enable()?;
            let result = EnableResponse::parse_from_bytes(&result_bytes)?;
            println!("Enable() returns: {:?}", result.status);

            // Initialize the ranging session
            let mut request = InitSessionRequest::new();
            request.set_session_id(session_id);
            request.set_session_type(SessionType::FIRA_RANGING_SESSION);
            request.set_params(app_config_params);
            let request_bytes = write_to_bytes(&request)?;
            let result_bytes = uwbd_proxy.init_session(request_bytes)?;
            let result = InitSessionResponse::parse_from_bytes(&result_bytes)?;
            println!("InitSessionRequest returns: {:?}", result.status);

            // Start the ranging session
            let mut request = StartRangingRequest::new();
            request.set_session_id(session_id);
            let request_bytes = write_to_bytes(&request)?;
            let result_bytes = uwbd_proxy.start_ranging(request_bytes)?;
            let result = StartRangingResponse::parse_from_bytes(&result_bytes)?;
            println!("StartRangingResponse returns: {:?}", result.status);
        }
        Some(("stop", _)) => {
            // Stop the ranging session
            let mut request = StopRangingRequest::new();
            request.set_session_id(session_id);
            let request_bytes = write_to_bytes(&request)?;
            let result_bytes = uwbd_proxy.stop_ranging(request_bytes)?;
            let result = StopRangingResponse::parse_from_bytes(&result_bytes)?;
            println!("StopRangingResponse returns: {:?}", result.status);

            // Deinit the ranging session
            let mut request = DeinitSessionRequest::new();
            request.set_session_id(session_id);
            let request_bytes = write_to_bytes(&request)?;
            let result_bytes = uwbd_proxy.stop_ranging(request_bytes)?;
            let result = DeinitSessionResponse::parse_from_bytes(&result_bytes)?;
            println!("DeinitSessionResponse returns: {:?}", result.status);
        }
        _ => unreachable!(),
    }

    Ok(())
}
