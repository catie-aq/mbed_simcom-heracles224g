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
#include "SIMCOM_HERACLES224G_CellularStack.h"
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

#if !NSAPI_PPP_AVAILABLE
NetworkStack *SIMCOM_HERACLES224G_CellularContext::get_stack()
{
    if (_pdp_type == NON_IP_PDP_TYPE || (_nonip_req && _pdp_type != DEFAULT_PDP_TYPE)) {
        tr_error("Requesting stack for NON-IP context! Should request control plane netif: get_cp_netif()");
        return NULL;
    }

    if (!_stack) {
        _stack = new SIMCOM_HERACLES224G_CellularStack(_at, _cid, (nsapi_ip_stack_t)_pdp_type, *get_device());
    }

    return _stack;
}


void SIMCOM_HERACLES224G_CellularContext::do_connect()
{
	int gprs_service = 0;
	bool context = false;
	_cb_data.error = NSAPI_ERROR_NO_CONNECTION;

    if (!_is_context_active) {
        _cb_data.error = do_activate_context();
    } else {
        _cb_data.error = NSAPI_ERROR_OK;
    }

	if (_cb_data.error == NSAPI_ERROR_OK) {
		_at.lock();
		_cb_data.error = NSAPI_ERROR_NO_CONNECTION;
		_at.cmd_start_stop("+CGATT", "?");
		_at.resp_start("+CGATT: ");
		gprs_service = _at.read_int();
		_at.resp_stop();
		if (gprs_service == 1) {
			// gprs service is attached: define context
			_cb_data.error = context_authentication();
			if (_cb_data.error == NSAPI_ERROR_OK) {
				// bring up connection
				_at.set_at_timeout(60000);
				_at.cmd_start("AT+CIICR");
				_at.cmd_stop();
				_at.resp_start();
				_at.set_stop_tag("OK");
				_at.restore_at_timeout();

				_cb_data.error = _at.get_last_error();

				if (_cb_data.error == NSAPI_ERROR_OK) {
					// it's necessary to setup module for the future tcp/ip application
					// similar to AT+CGADDR command to get ip address
					_at.cmd_start("AT+CIFSR");
					_at.cmd_stop();
					//ignore response
					_cb_data.error = _at.get_last_error();
					_is_connected = true;
					_connect_status = NSAPI_STATUS_GLOBAL_UP;
				}

			}
		}
		_at.unlock();

		if (_status_cb) {
			_status_cb(NSAPI_EVENT_CONNECTION_STATUS_CHANGE, _connect_status);
		}
	}

//    if (_cb_data.error != NSAPI_ERROR_OK) {
//        // If new PSD context was created and failed to activate, delete it
//        if (_new_context_set) {
//            disconnect_modem_stack();
//        }
//        _connect_status = NSAPI_STATUS_DISCONNECTED;
//    } else {
//        _connect_status = NSAPI_STATUS_GLOBAL_UP;
//    }

}

nsapi_error_t SIMCOM_HERACLES224G_CellularContext::context_authentication()
{
    if (_pwd && _uname) {
        if (_at.at_cmd_discard("+CSTT", "=", "\"%s\",\"%s\",\"%s\"",
                               _apn, _uname, _pwd) != NSAPI_ERROR_OK) {
            return NSAPI_ERROR_AUTH_FAILURE;
        }
    } else {
        if (_at.at_cmd_discard("+CSTT", "=", "\"%s\"", _apn) != NSAPI_ERROR_OK) {
            return NSAPI_ERROR_AUTH_FAILURE;
        }
    }

    return NSAPI_ERROR_OK;
}

#endif // #if !NSAPI_PPP_AVAILABLE


nsapi_error_t SIMCOM_HERACLES224G_CellularContext::do_activate_context()
{
    if (_nonip_req && _cp_in_use) {
        return activate_non_ip_context();
    }

    // In IP case but also when Non-IP is requested and
    // control plane optimization is not established -> activate ip context
    _nonip_req = false;
    return activate_ip_context();
}

nsapi_error_t SIMCOM_HERACLES224G_CellularContext::activate_ip_context()
{
    nsapi_error_t ret = find_and_activate_context();
#if !NSAPI_PPP_AVAILABLE
    if (ret == NSAPI_ERROR_OK) {
        pdpContextList_t params_list;
        if (get_pdpcontext_params(params_list) == NSAPI_ERROR_OK) {
            pdpcontext_params_t *pdp = params_list.get_head();
            while (pdp) {
                SocketAddress addr;
                if (addr.set_ip_address(pdp->dns_secondary_addr)) {
                    nsapi_addr_t taddr = addr.get_addr();
                    for (int i = 0; i < ((taddr.version == NSAPI_IPv6) ? NSAPI_IPv6_BYTES : NSAPI_IPv4_BYTES); i++) {
                        if (taddr.bytes[i] != 0) { // check the address is not all zero
                            tr_info("DNS secondary %s", pdp->dns_secondary_addr);
                            char ifn[5]; // "ce" + two digit _cid + zero
                            add_dns_server(addr, get_interface_name(ifn));
                            break;
                        }
                    }
                }
                if (addr.set_ip_address(pdp->dns_primary_addr)) {
                    nsapi_addr_t taddr = addr.get_addr();
                    for (int i = 0; i < ((taddr.version == NSAPI_IPv6) ? NSAPI_IPv6_BYTES : NSAPI_IPv4_BYTES); i++) {
                        if (taddr.bytes[i] != 0) { // check the address is not all zero
                            tr_info("DNS primary %s", pdp->dns_primary_addr);
                            char ifn[5]; // "ce" + two digit _cid + zero
                            add_dns_server(addr, get_interface_name(ifn));
                            break;
                        }
                    }
                }
                pdp = pdp->next;
            }
        }
    }
#endif
    return ret;
}

void SIMCOM_HERACLES224G_CellularContext::activate_context()
{
    tr_info("Activate PDP context %d", _cid);
    _at.at_cmd_discard("+CGACT", "=1,", "%d", _cid);
    if (_at.get_last_error() == NSAPI_ERROR_OK) {
        _is_context_activated = true;
    }
}

nsapi_error_t SIMCOM_HERACLES224G_CellularContext::find_and_activate_context()
{
    _at.lock();

    nsapi_error_t err = NSAPI_ERROR_OK;

    // try to find or create context of suitable type
    if (get_context()) {
#if NSAPI_PPP_AVAILABLE
        _at.unlock();
        // in PPP we don't activate any context but leave it to PPP stack
        return err;
#else
        // try to authenticate user before activating or modifying context
        err = do_user_authentication();
#endif // NSAPI_PPP_AVAILABLE
    } else {
        err = NSAPI_ERROR_NO_CONNECTION;
    }

    if (err != NSAPI_ERROR_OK) {
        tr_error("Failed to activate network context! (%d)", err);
    } else if (!(_nonip_req && _cp_in_use) && !get_stack()) {
        // do check for stack to validate that we have support for stack
        tr_error("No cellular stack!");
        err = NSAPI_ERROR_UNSUPPORTED;
    }

    _is_context_active = false;
    _is_context_activated = false;

    if (err == NSAPI_ERROR_OK) {
        _is_context_active = _nw->is_active_context(NULL, _cid);

        if (!_is_context_active) {
            activate_context();
        }

        err = (_at.get_last_error() == NSAPI_ERROR_OK) ? NSAPI_ERROR_OK : NSAPI_ERROR_NO_CONNECTION;
    }

    // If new PDP context was created and failed to activate, delete it
    if (err != NSAPI_ERROR_OK && _new_context_set) {
        delete_current_context();
    } else if (err == NSAPI_ERROR_OK) {
        _is_context_active = true;
    }

    _at.unlock();

    return err;
}

void SIMCOM_HERACLES224G_CellularContext::set_cid(int cid)
{
    _cid = cid;
    if (_stack) {
        static_cast<AT_CellularStack *>(_stack)->set_cid(_cid);
    }
}

// PDP Context handling
void SIMCOM_HERACLES224G_CellularContext::delete_current_context()
{
    if (_cid <= 0) {
        return;
    }
    tr_info("Delete context %d", _cid);
    _at.clear_error();

    _at.at_cmd_discard("+CGDCONT", "=", "%d", _cid);

    if (_at.get_last_error() == NSAPI_ERROR_OK) {
        set_cid(-1);

        _new_context_set = false;
    }

    // there is nothing we can do if deleting of context fails. No point reporting an error (for example disconnect).
    _at.clear_error();
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
