/*
 * Copyright (c) 2018, Arm Limited and affiliates.
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
#include "SIMCOM_HERACLES224G_CellularContext.h"
#include "CellularLog.h"

#include "Semaphore.h"

using namespace mbed_cellular_util;

namespace mbed {

SIMCOM_HERACLES224G_CellularContext::SIMCOM_HERACLES224G_CellularContext(ATHandler &at, CellularDevice *device, const char *apn, bool cp_req, bool nonip_req) :
    AT_CellularContext(at, device, apn, cp_req, nonip_req)
{
}

SIMCOM_HERACLES224G_CellularContext::~SIMCOM_HERACLES224G_CellularContext()
{
}

void SIMCOM_HERACLES224G_CellularContext::activate_context()
{
    tr_info("Activate PDP context %d", _cid);

    if (_at.at_cmd_discard("+CGACT", "=", "%d,%d", 1, _cid) == NSAPI_ERROR_OK) {
        _is_context_activated = true;
    }
}

void SIMCOM_HERACLES224G_CellularContext::deactivate_context()
{
    _at.at_cmd_discard("+CGACT", "=", "%d,%d", 0, _cid);
}

bool SIMCOM_HERACLES224G_CellularContext::get_context()
{
    bool modem_supports_ipv6 = get_device()->get_property(AT_CellularDevice::PROPERTY_IPV6_PDP_TYPE);
    bool modem_supports_ipv4 = get_device()->get_property(AT_CellularDevice::PROPERTY_IPV4_PDP_TYPE);

    _at.cmd_start_stop("+CGDCONT", "?");
    _at.resp_start("+CGDCONT:");
    _cid = -1;
    int cid_max = 0; // needed when creating new context
    char apn[MAX_ACCESSPOINT_NAME_LENGTH];
    int apn_len = 0;

    while (_at.info_resp()) {
        int cid = _at.read_int();
        if (cid > cid_max) {
            cid_max = cid;
        }
        char pdp_type_from_context[10];
        int pdp_type_len = _at.read_string(pdp_type_from_context, sizeof(pdp_type_from_context) - 1);
        if (pdp_type_len > 0) {
            apn_len = _at.read_string(apn, sizeof(apn) - 1);
            if (apn_len >= 0) {
                if (_apn && apn_len > 0 && (strcmp(apn, _apn) != 0)) {
                    continue;
                }

                // APN matched -> Check PDP type
                pdp_type_t pdp_type = string_to_pdp_type(pdp_type_from_context);

                // Accept exact matching PDP context type or dual PDP context for IPv4/IPv6 only modems
                if (get_device()->get_property(pdp_type_t_to_cellular_property(pdp_type)) ||
                        ((pdp_type == IPV4V6_PDP_TYPE && (modem_supports_ipv4 || modem_supports_ipv6)) && !_nonip_req)) {
                    _pdp_type = pdp_type;
                    _cid = cid;
                    break;
                }
            }
        }
    }

    _at.resp_stop();
    if (_cid == -1) { // no suitable context was found so create a new one
        if (!set_new_context(1)) {
            return false;
        }
    }

    // save the apn
    if (apn_len > 0 && !_apn) {
        memcpy(_found_apn, apn, apn_len + 1);
    }

    tr_info("Found PDP context %d", _cid);
    return true;
}

} /* namespace mbed */
