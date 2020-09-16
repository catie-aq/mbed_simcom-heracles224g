/*
 * Copyright (c) 2017, Arm Limited and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mbed.h"

#include <AT_CellularNetwork.h>
#include "SIMCOM_HERACLES224G.h"
#include "SIMCOM_HERACLES224G_CellularContext.h"
#include "SIMCOM_HERACLES224G_CellularNetwork.h"
#include <CellularLog.h>

using namespace mbed;
using namespace rtos;
using namespace events;

#define DEVICE_READY_URC "CPIN:"

#if !defined(MBED_CONF_SIMCOM_HERACLES224G_TX)
#define MBED_CONF_SIMCOM_HERACLES224G_TX    NC
#endif

#if !defined(MBED_CONF_SIMCOM_HERACLES224G_RX)
#define MBED_CONF_SIMCOM_HERACLES224G_RX    NC
#endif

#if !defined(MBED_CONF_SIMCOM_HERACLES224G_POLARITY)
#define MBED_CONF_SIMCOM_HERACLES224G_POLARITY    1 // active high
#endif

#if !defined(MBED_CONF_QUECTEL_BG96_RST)
#define MBED_CONF_QUECTEL_BG96_RST    NC
#endif

#if !defined(MBED_CONF_SIMCOM_HERACLES224G_PWR)
#define MBED_CONF_SIMCOM_HERACLES224G_PWR    NC
#endif

namespace {
#define AUTOBAUD 0 // if the result of AT+IPR? is different than 0 (default value = 0)
}

static const intptr_t cellular_properties[AT_CellularDevice::PROPERTY_MAX] = {
    AT_CellularNetwork::RegistrationModeLAC,    // C_EREG
    AT_CellularNetwork::RegistrationModeLAC,    // C_GREG
    AT_CellularNetwork::RegistrationModeLAC,    // C_REG
    0,  // AT_CGSN_WITH_TYPE
    0,  // AT_CGDATA
    1,  // AT_CGAUTH
    1,  // AT_CNMI
    1,  // AT_CSMP
    1,  // AT_CMGF
    1,  // AT_CSDH
    1,  // PROPERTY_IPV4_STACK
    1,  // PROPERTY_IPV6_STACK
    0,  // PROPERTY_IPV4V6_STACK
    1,  // PROPERTY_NON_IP_PDP_TYPE
    1,  // PROPERTY_AT_CGEREP,
    1,  // PROPERTY_AT_COPS_FALLBACK_AUTO
    12, // PROPERTY_SOCKET_COUNT
    1,  // PROPERTY_IP_TCP
    1,  // PROPERTY_IP_UDP
    20,  // PROPERTY_AT_SEND_DELAY
};

SIMCOM_HERACLES224G::SIMCOM_HERACLES224G(FileHandle *fh, PinName pwr_key, bool active_high, PinName rst, PinName status)
    : AT_CellularDevice(fh),
      _active_high(active_high),
      _pwr_key(pwr_key, !_active_high),
	  _rst(rst, !_active_high),
	  _status(status)
{
    set_cellular_properties(cellular_properties);
}

#if MBED_CONF_SIMCOM_HERACLES224G_PROVIDE_DEFAULT
#include "drivers/BufferedSerial.h"
CellularDevice *CellularDevice::get_default_instance()
{
    static BufferedSerial serial(MBED_CONF_SIMCOM_HERACLES224G_TX, MBED_CONF_SIMCOM_HERACLES224G_RX, MBED_CONF_SIMCOM_HERACLES224G_BAUDRATE);
#if defined (MBED_CONF_SIMCOM_HERACLES224G_RTS) && defined (MBED_CONF_SIMCOM_HERACLES224G_CTS)
    serial.set_flow_control(SerialBase::RTSCTS, MBED_CONF_SIMCOM_HERACLES224G_RTS, MBED_CONF_SIMCOM_HERACLES224G_CTS);
#endif
    static SIMCOM_HERACLES224G device(&serial,
                              MBED_CONF_SIMCOM_HERACLES224G_PWR,
                              MBED_CONF_SIMCOM_HERACLES224G_POLARITY);

    return &device;
}
#endif

AT_CellularNetwork *SIMCOM_HERACLES224G::open_network_impl(ATHandler &at)
{
    return new SIMCOM_HERACLES224G_CellularNetwork(at, *this);
}

AT_CellularContext *SIMCOM_HERACLES224G::create_context_impl(ATHandler &at, const char *apn, bool cp_req, bool nonip_req)
{
    return new SIMCOM_HERACLES224G_CellularContext(at, this, apn, cp_req, nonip_req);
}

void SIMCOM_HERACLES224G::set_ready_cb(Callback<void()> callback)
{
    _at.set_urc_handler(DEVICE_READY_URC, callback);
}

nsapi_error_t SIMCOM_HERACLES224G::is_ready()
{
	//TODO: send an AT command to check if the module is ready
	// or read the hardware status pin;
	if (_status.is_connected()) {
		if (_status == 1) {
			return NSAPI_ERROR_OK;
		}
		return NSAPI_ERROR_DEVICE_ERROR;
	}

	_at.lock();
	_at.at_cmd_discard("", "");
    // we need to do this twice because for example after data mode the first 'AT' command will give modem a
	// stimulus that we are back to command mode.
	_at.clear_error();
	_at.flush();
	_at.set_at_timeout(100);
	_at.cmd_start("AT");
	_at.cmd_stop_read_resp();
	_at.restore_at_timeout();

	return _at.unlock_return_error();
}

nsapi_error_t SIMCOM_HERACLES224G::hard_power_off()
{
    _pwr_key = !_active_high;
    ThisThread::sleep_for(10s);

    return NSAPI_ERROR_OK;
}

nsapi_error_t SIMCOM_HERACLES224G::hard_power_on()
{
	if (_status.is_connected()) {
		// check if the module is already ready
		if (_status == 1) {
			return NSAPI_ERROR_OK;
		}
	}
	// need to turn the module on
	if (_pwr_key.is_connected()) {
		tr_info("SIMCOM_HERACLES224G::hard power on");
		press_button(_pwr_key, 1000);
		ThisThread::sleep_for(10s);
		return NSAPI_ERROR_OK;
	}

    return NSAPI_ERROR_DEVICE_ERROR;
}

nsapi_error_t SIMCOM_HERACLES224G::soft_power_on()
{
    /*if (_pwr_key.is_connected()) {
	   tr_info("SIMCOM_HERACLES224G::soft_power_on");
	   // check if modem was powered on already
	   if (!wake_up()) {
		   if (!wake_up(false)) {
			   tr_error("Modem not responding");
			   soft_power_off();
			   return NSAPI_ERROR_DEVICE_ERROR;
		   }
	   }
   }*/

    return NSAPI_ERROR_OK;
}

nsapi_error_t SIMCOM_HERACLES224G::soft_power_off()
{
    _at.lock();
    _at.set_at_timeout(2000);
    _at.cmd_start("AT+CPOWD=1");
    _at.resp_start();
    _at.set_stop_tag("NORMAL POWER DOWN");
    bool pwr = _at.consume_to_stop_tag();
    _at.restore_at_timeout();
    _at.unlock();
    if (!pwr) {
        tr_warn("Force modem off");
        if (_pwr_key.is_connected()) {
            press_button(_pwr_key, 1500); // Heracles_Hardware_Design_V1.02: Power off signal at least 1500 ms
            return NSAPI_ERROR_OK;
        }
    }
    return _at.unlock_return_error();
}

void SIMCOM_HERACLES224G::press_button(DigitalOut &button, uint32_t timeout)
{
    if (!button.is_connected()) {
        return;
    }
    button = _active_high;
    ThisThread::sleep_for(timeout);
    button = !_active_high;
}

bool SIMCOM_HERACLES224G::wake_up(bool reset)
{
    // check if modem is already ready
    _at.lock();
    _at.flush();
    _at.set_at_timeout(100);
    _at.cmd_start("AT");
    _at.cmd_stop_read_resp();
    nsapi_error_t err = _at.get_last_error();
    _at.restore_at_timeout();
    _at.unlock();

    // modem is not responding, power it on
     if (err != NSAPI_ERROR_OK) {
         if (!reset) {
             tr_info("Power on modem");
             press_button(_pwr_key, 1000);
         } else {
             tr_warn("Reset modem");
             // TODO: implement rst pin in the HW design
             if (_rst.is_connected()) {
            	 // According to Heracles_Hardware_Design_V1.02: t >= 150ms
                 press_button(_rst, 150);
             }
         }

#if !AUTOBAUD
         // default value: AT+IPR: 0 (autobaud)
         // According to Heracles_Hardware_Design_V1.02: t >= 3s
         ThisThread::sleep_for(10s);
         // try again to send an AT command
         _at.lock();
         _at.flush();
         _at.set_at_timeout(30);
         _at.cmd_start("AT");
         _at.cmd_stop_read_resp();
         err = _at.get_last_error();
         _at.restore_at_timeout();
         _at.unlock();
         if (err != NSAPI_ERROR_OK) {
        	 return false;
         }
#else
         // According to Heracles_Hardware_Design_V1.02, serial_port is active after 3s, but it seems to take over 5s
         // This URC does not appear when autobauding function is active. (AT+IPR=x)
         _at.lock();
         _at.set_at_timeout(5000);
         _at.resp_start();
         _at.set_stop_tag("RDY");
         bool rdy = _at.consume_to_stop_tag();
         _at.restore_at_timeout();
         _at.unlock();
         if (!rdy) {
             return false;
         }
#endif

    }
    // sync to check that AT is really responsive and to clear garbage
    return _at.sync(500);
}
