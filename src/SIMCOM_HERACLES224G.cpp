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

using namespace mbed;
using namespace rtos;
using namespace events;

#if !defined(MBED_CONF_SIMCOM_HERACLES224G_PWR)
#define MBED_CONF_SIMCOM_HERACLES224G_PWR    NC
#endif

#if !defined(MBED_CONF_SIMCOM_HERACLES224G_TX)
#define MBED_CONF_SIMCOM_HERACLES224G_TX    NC
#endif

#if !defined(MBED_CONF_SIMCOM_HERACLES224G_RX)
#define MBED_CONF_SIMCOM_HERACLES224G_RX    NC
#endif

#if !defined(MBED_CONF_SIMCOM_HERACLES224G_POLARITY)
#define MBED_CONF_SIMCOM_HERACLES224G_POLARITY    1 // active high
#endif

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
    2, // PROPERTY_SOCKET_COUNT
    1,  // PROPERTY_IP_TCP
    1,  // PROPERTY_IP_UDP
    100,  // PROPERTY_AT_SEND_DELAY
};

SIMCOM_HERACLES224G::SIMCOM_HERACLES224G(FileHandle *fh, PinName pwr, bool active_high)
    : AT_CellularDevice(fh),
      _active_high(active_high),
      _pwr_key(pwr, !_active_high)
{
    set_cellular_properties(cellular_properties);
}

nsapi_error_t SIMCOM_HERACLES224G::init()
{
    nsapi_error_t err = AT_CellularDevice::init();
    if (err != NSAPI_ERROR_OK) {
        return err;
    }
    _at.lock();

    // AT+CMEE=2
    // Set command disables the use of result code +CME ERROR: <err> as an indication of an
    // error relating to the +Cxxx command issued. When enabled, device related errors cause the +CME
    // ERROR: <err> final result code instead of the default ERROR final result code. ERROR is returned
    // normally when the error message is related to syntax, invalid parameters or DTE functionality.
    // Current setting: enable and use verbose <err> values
    _at.at_cmd_discard("+CMEE", "=2");


    return _at.unlock_return_error();
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

nsapi_error_t SIMCOM_HERACLES224G::hard_power_on()
{
    soft_power_on();

    return NSAPI_ERROR_OK;
}

nsapi_error_t SIMCOM_HERACLES224G::soft_power_on()
{
    _pwr_key = _active_high;
    ThisThread::sleep_for(500);
    _pwr_key = !_active_high;
    ThisThread::sleep_for(5000);
    _pwr_key = _active_high;
    ThisThread::sleep_for(5000);

    return NSAPI_ERROR_OK;
}

nsapi_error_t SIMCOM_HERACLES224G::hard_power_off()
{
    _pwr_key = !_active_high;
    ThisThread::sleep_for(10000);

    return NSAPI_ERROR_OK;
}

nsapi_error_t SIMCOM_HERACLES224G::soft_power_off()
{
    return AT_CellularDevice::soft_power_off();
}

AT_CellularNetwork *SIMCOM_HERACLES224G::open_network_impl(ATHandler &at)
{
    return new SIMCOM_HERACLES224G_CellularNetwork(at, *this);
}

AT_CellularContext *SIMCOM_HERACLES224G::create_context_impl(ATHandler &at, const char *apn, bool cp_req, bool nonip_req)
{
    return new SIMCOM_HERACLES224G_CellularContext(at, this, apn, cp_req, nonip_req);
}
