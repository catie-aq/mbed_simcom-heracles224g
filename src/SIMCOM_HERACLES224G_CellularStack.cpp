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

#include <stdio.h>
#include <string.h>

#include "heracles224g/SIMCOM_HERACLES224G_CellularStack.h"
#include "CellularLog.h"
#include "netsocket/TLSSocket.h"

using namespace mbed;

SIMCOM_HERACLES224G_CellularStack::SIMCOM_HERACLES224G_CellularStack(ATHandler &atHandler, int cid, nsapi_ip_stack_t stack_type, AT_CellularDevice &device) :
    AT_CellularStack(atHandler, cid, stack_type, device),
	_tls_sec_level(0)
{
	_tcpip_mode = SINGLE_TCP;
	_data_transmitting_mode = NORMAL;
//    _at.set_urc_handler("+CIPSEND: ", mbed::Callback<void()>(this, &SIMCOM_HERACLES224G_CellularStack::urc_qiurc_recv));

    _at.clear_error();
}

SIMCOM_HERACLES224G_CellularStack::~SIMCOM_HERACLES224G_CellularStack() // @suppress("Member declaration not found")
{
}

/*****************************************************************************************
 *
 *
 * Network stack
 *
 *
 *******************************************************************************************/
nsapi_error_t SIMCOM_HERACLES224G_CellularStack::socket_listen(nsapi_socket_t handle, int backlog)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

nsapi_error_t SIMCOM_HERACLES224G_CellularStack::socket_accept(void *server, void **socket, SocketAddress *addr)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

nsapi_error_t SIMCOM_HERACLES224G_CellularStack::socket_connect(nsapi_socket_t handle, const SocketAddress &address)
{
    CellularSocket *socket = (CellularSocket *)handle;

    int modem_connect_id = -1;
    int err = NSAPI_ERROR_NO_CONNECTION;

    // assert here as its a programming error if the socket container doesn't contain
    // specified handle
    int request_connect_id = find_socket_index(socket);
    MBED_ASSERT(request_connect_id != -1);

    _at.lock();

	tr_info("socket info: ip: %s, port: %d", address.get_ip_address(), address.get_port());

	if (_tcpip_mode ==  SINGLE_TCP) {
	    if (socket->proto == NSAPI_TCP) {
	    	tr_info("socket info: ip: %s, port: %d", address.get_ip_address(), address.get_port());
			err = _at.at_cmd_discard("+CIPSTART", "=", "\"%s\",\"%s\",\"%d\"", "TCP",
					address.get_ip_address(), address.get_port());

			handle_open_socket_response(err);

		} else {
			err =_at.at_cmd_discard("+CIPSTART", "=", "\"%s\",\"%s\",\"%d\"", "UDP",
					address.get_ip_address(), address.get_port());

			handle_open_socket_response(err);

		}

	    if (err == NSAPI_ERROR_OK) {
	        socket->remoteAddress = address;
	        socket->connected = true;
	        return NSAPI_ERROR_OK;
	    }

	} else {
		int request_connect_id = find_socket_index(socket);

		char ipdot[NSAPI_IP_SIZE];
		ip2dot(address, ipdot);

	    if (socket->proto == NSAPI_TCP) {
			_at.at_cmd_discard("+CIPSTART", "=", "\"%d\",\"%s\",\"%s\",\"%d\"", request_connect_id, "TCP",
									address.get_ip_address(), address.get_port());

			handle_open_socket_response(err);

			if ((_at.get_last_error() == NSAPI_ERROR_OK) && err) {
				if (err == HERACLES224G_SOCKET_BIND_FAIL) {
					socket->id = -1;
					_at.unlock();
					return NSAPI_ERROR_PARAMETER;
				}
			}
	    } else {
	    	_at.at_cmd_discard("+CIPSTART", "=", "\"%d\",\"%s\",\"%s\",\"%d\"", request_connect_id, "UDP",
	    			address.get_ip_address(), address.get_port());

			handle_open_socket_response(err);

			if ((_at.get_last_error() == NSAPI_ERROR_OK) && err) {
				if (err == HERACLES224G_SOCKET_BIND_FAIL) {
					socket->id = -1;
					_at.unlock();
					return NSAPI_ERROR_PARAMETER;
				}
			}
	    }

	    // If opened successfully BUT not requested one, close it
	    if (!err && (modem_connect_id != request_connect_id)) {
	        _at.at_cmd_discard("+CIPCLOSE", "=", "%d", modem_connect_id);
	    }

	    nsapi_error_t ret_val = _at.get_last_error();
	    _at.unlock();

	    if ((!err) && (ret_val == NSAPI_ERROR_OK) && (modem_connect_id == request_connect_id)) {
	        socket->id = request_connect_id;
	        socket->remoteAddress = address;
	        socket->connected = true;
	        return NSAPI_ERROR_OK;
	    }
	}

    return NSAPI_ERROR_NO_CONNECTION;
}

nsapi_error_t SIMCOM_HERACLES224G_CellularStack::socket_close(nsapi_socket_t handle)
{
	CellularSocket *socket = (CellularSocket *)handle;
	nsapi_error_t err;

	_at.lock();

	_at.set_at_timeout(HERACLES224G_CLOSE_SOCKET_TIMEOUT);

	if (_tcpip_mode == SINGLE_TCP) {
		_at.cmd_start("+CIPCLOSE");
		_at.cmd_stop();
	} else {
		int request_connect_id = find_socket_index(socket);
		err = _at.at_cmd_discard("+CIPCLOSE", "=", "%d", request_connect_id);
	}
	_at.resp_start();
	_at.set_stop_tag("CLOSE OK");
	_at.restore_at_timeout();

	err = _at.get_last_error();

	_at.unlock();

	return err;
}

nsapi_size_or_error_t SIMCOM_HERACLES224G_CellularStack::socket_send(nsapi_socket_t handle,
                                              const void *data, nsapi_size_t size)
{
	 CellularSocket *socket = (CellularSocket *)handle;

	 if (size > HERACLES224G_MAX_SEND_SIZE) {
	 		return NSAPI_ERROR_PARAMETER;
	 	}

	if (_ip_ver_sendto != socket->localAddress.get_ip_version()) {
		_ip_ver_sendto =  socket->localAddress.get_ip_version();
		socket_close_impl(socket->id);
		create_socket_impl(socket);
	}

	_at.lock();

	if (_tcpip_mode == SINGLE_TCP) {
		// Send
		_at.cmd_start_stop("+CIPSEND", "=", "%d", size);
		_at.resp_start(">");
		_at.write_bytes((uint8_t *)data, size);
	} else {
		// Send
		_at.cmd_start_stop("+CIPSEND", "=", "%d%d", socket->id, size);
		_at.resp_start(">");
		_at.write_bytes((uint8_t *)data, size);

	}

	// Possible responses are SEND OK, DATA ACCEPT or CME ERROR.
	char response[16];
	response[0] = '\0';
	_at.resp_start();
	_at.read_string(response, sizeof(response));
	_at.resp_stop();

	if (_data_transmitting_mode == NORMAL) {
		if (strstr(response, "SEND OK") == 0) {
			return NSAPI_ERROR_DEVICE_ERROR;
		}
	} else {
		if (strstr(response, "DATA ACCEPT") == 0) {
			return NSAPI_ERROR_DEVICE_ERROR;
		}
	}

	_at.unlock();

	return NSAPI_ERROR_OK;
}

nsapi_size_or_error_t SIMCOM_HERACLES224G_CellularStack::socket_sendto(nsapi_socket_t handle, const SocketAddress &address,
                                            const void *data, nsapi_size_t size)
{
	CellularSocket *socket = (CellularSocket *)handle;

	 if (size > HERACLES224G_MAX_SEND_SIZE) {
		return NSAPI_ERROR_PARAMETER;
	}

	if (_ip_ver_sendto != address.get_ip_version()) {
		_ip_ver_sendto =  address.get_ip_version();
		socket_close_impl(socket->id);
		create_socket_impl(socket);
	}

	int sent_len = 0;
	int sent_len_before = 0;
	int sent_len_after = 0;

	_at.lock();

	if (_tcpip_mode == SINGLE_TCP) {
		// Send
		_at.cmd_start_stop("+CIPSEND", "=", "%d", size);
		_at.resp_start(">");
		_at.write_bytes((uint8_t *)data, size);
		_at.resp_start();
	} else {
		// Send
		_at.cmd_start_stop("+CIPSEND", "=", "%d%d", socket->id, size);
		_at.resp_start(">");
		_at.write_bytes((uint8_t *)data, size);
		_at.resp_start();
	}

	// Possible responses are SEND OK, DATA ACCEPT or CME ERROR.
	char response[16];
	response[0] = '\0';
	_at.read_string(response, sizeof(response));
	_at.resp_stop();

	if (_data_transmitting_mode == NORMAL) {
		if (strstr(response, "SEND OK") == 0) {
			return NSAPI_ERROR_DEVICE_ERROR;
		}
	} else {
		if (strstr(response, "DATA ACCEPT") == 0) {
			return NSAPI_ERROR_DEVICE_ERROR;
		}
	}

	_at.unlock();

	return NSAPI_ERROR_OK;
}

nsapi_size_or_error_t SIMCOM_HERACLES224G_CellularStack::socket_recv(nsapi_socket_t handle,
                                              void *data, nsapi_size_t size)
{
	_at.lock();
	_at.resp_start();
	_at.read_string((char *)data, size);
	_at.resp_stop();
	_at.unlock();
}

void SIMCOM_HERACLES224G_CellularStack::urc_qiurc_recv()
{
    urc_qiurc(URC_RECV);
}

void SIMCOM_HERACLES224G_CellularStack::urc_qiurc_closed()
{
    urc_qiurc(URC_CLOSED);
}



void SIMCOM_HERACLES224G_CellularStack::urc_qiurc(urc_type_t urc_type)
{
    _at.lock();
    _at.skip_param();
    const int sock_id = _at.read_int();
    const nsapi_error_t err = _at.unlock_return_error();

    if (err != NSAPI_ERROR_OK) {
        return;
    }

    CellularSocket *sock = find_socket(sock_id);
    if (sock) {
        if (urc_type == URC_CLOSED) {
            tr_info("Socket closed %d", sock_id);
            sock->closed = true;
        }
        if (sock->_cb) {
            sock->_cb(sock->_data);
        }
    }
}




/*****************************************************************************************
 *
 *
 * AT cellular stack
 *
 *
 *******************************************************************************************/

nsapi_error_t SIMCOM_HERACLES224G_CellularStack::socket_close_impl(int sock_id)
{
	nsapi_error_t err;
	_at.set_at_timeout(HERACLES224G_CLOSE_SOCKET_TIMEOUT);

    if (_tcpip_mode == SINGLE_TCP) {
    	_at.cmd_start("+CIPCLOSE");

    } else {
        CellularSocket *socket = find_socket(sock_id);
    	err = _at.at_cmd_discard("+CIPCLOSE", "=", "%d", sock_id);
    }
//    _at.resp_start("CLOSE OK");
//    _at.resp_stop();

    _at.restore_at_timeout();

    err = _at.get_last_error();

    return err;
}

void SIMCOM_HERACLES224G_CellularStack::handle_open_socket_response(int &err)
{
	nsapi_error_t error;

	// OK
    // CIPSTART -> should be handled as URC?
    _at.set_at_timeout(10000); // HERACLES224G_CREATE_SOCKET_TIMEOUT
    _at.set_stop_tag("CONNECT OK");
    _at.resp_start();
	if (_at.consume_to_stop_tag()) {
		tr_info("CONNECT OK");
		err = NSAPI_ERROR_OK;
	} else {
		err = NSAPI_ERROR_DEVICE_ERROR;
	}
	_at.resp_stop();
	_at.restore_at_timeout();

}

nsapi_error_t SIMCOM_HERACLES224G_CellularStack::create_socket_impl(CellularSocket *socket)
{
    int modem_connect_id = -1;
    int remote_port = 0;
    int err = -1;

    int request_connect_id = find_socket_index(socket);
    // assert here as its a programming error if the socket container doesn't contain
    // specified handle
    MBED_ASSERT(request_connect_id != -1);

    if (_tcpip_mode == SINGLE_TCP) {
    	// single tcpip connection mode
    	// UDP type
		if (socket->proto == NSAPI_UDP) {
			if (!socket->connected) {
				// UDP type: format AT+CIPSTART="UDP","ip_address","port"
				if (!socket->connected) {
					_at.at_cmd_discard("+CIPSTART", "=", "'\"%s\",\"%s\",\"%d\"", "UDP",
									   (_ip_ver_sendto == NSAPI_IPv4) ? "127.0.0.1" : "0:0:0:0:0:0:0:1",
									   socket->localAddress.get_port());

					handle_open_socket_response(err);

					if ((_at.get_last_error() == NSAPI_ERROR_OK) && err) {
						if (err == HERACLES224G_SOCKET_BIND_FAIL) {
							socket->id = -1;
							return NSAPI_ERROR_PARAMETER;
						}
					}
				}

			}
		} else if (socket->proto == NSAPI_TCP) {
			// TCP type: format AT+CIPSTART="TCP","ip_address","port"
			if (!socket->connected) {
				_at.at_cmd_discard("+CIPSTART", "=", "'\"%s\",\"%s\",\"%d\"", "TCP",
								   (_ip_ver_sendto == NSAPI_IPv4) ? "127.0.0.1" : "0:0:0:0:0:0:0:1",
								   socket->localAddress.get_port());

				handle_open_socket_response(err);

				if ((_at.get_last_error() == NSAPI_ERROR_OK) && err) {
					if (err == HERACLES224G_SOCKET_BIND_FAIL) {
						socket->id = -1;
						return NSAPI_ERROR_PARAMETER;
					}
				}
			}
		}
    } else {
    	// multiple tcpip connection mode
    	// UDP type
		if (socket->proto == NSAPI_UDP) {
			if (!socket->connected) {
				// UDP type: single
				if (!socket->connected) {
					_at.at_cmd_discard("+CIPSTART", "=", "\"%d\",\"%s\",\"%s\",\"%d\"", request_connect_id, "UDP",
									   (_ip_ver_sendto == NSAPI_IPv4) ? "127.0.0.1" : "0:0:0:0:0:0:0:1",
									   socket->localAddress.get_port());

					handle_open_socket_response(err);

					if ((_at.get_last_error() == NSAPI_ERROR_OK) && err) {
						if (err == HERACLES224G_SOCKET_BIND_FAIL) {
							socket->id = -1;
							return NSAPI_ERROR_PARAMETER;
						}
					}
				}

			}
		} else if (socket->proto == NSAPI_TCP) {
			// TCP type
			if (!socket->connected) {
				_at.at_cmd_discard("+CIPSTART", "=", "\"%d\",\"%s\",\"%s\",\"%d\"", request_connect_id, "TCP",
								   (_ip_ver_sendto == NSAPI_IPv4) ? "127.0.0.1" : "0:0:0:0:0:0:0:1",
								   socket->localAddress.get_port());


				handle_open_socket_response(err);

				if ((_at.get_last_error() == NSAPI_ERROR_OK) && err) {
					if (err == HERACLES224G_SOCKET_BIND_FAIL) {
						socket->id = -1;
						return NSAPI_ERROR_PARAMETER;
					}
				}
			}
		}
    }

    // If opened successfully BUT not requested one, close it
    if (!err && (modem_connect_id != request_connect_id)) {
        socket_close_impl(modem_connect_id);
    }

    nsapi_error_t ret_val = _at.get_last_error();

    if (ret_val == NSAPI_ERROR_OK && (modem_connect_id == request_connect_id)) {
        socket->id = request_connect_id;
    }

    return ret_val;
}

nsapi_size_or_error_t SIMCOM_HERACLES224G_CellularStack::socket_sendto_impl(CellularSocket *socket, const SocketAddress &address,
                                                                     const void *data, nsapi_size_t size)
{
    if (size > HERACLES224G_MAX_SEND_SIZE) {
        return NSAPI_ERROR_PARAMETER;
    }

    if (_ip_ver_sendto != address.get_ip_version()) {
        _ip_ver_sendto =  address.get_ip_version();
        socket_close_impl(socket->id);
        create_socket_impl(socket);
    }

    int sent_len = 0;
    int sent_len_before = 0;
    int sent_len_after = 0;


    if (_tcpip_mode == SINGLE_TCP) {
    	// Send
		_at.cmd_start_stop("+CIPSEND", "=", "%d", size);
		_at.resp_start(">");
		_at.resp_stop();
		_at.write_bytes((uint8_t *)data, size);
    } else {
		// Send
    	_at.cmd_start_stop("+CIPSEND", "=", "%d%d", socket->id, size);
		_at.resp_start(">");
		_at.resp_stop();
		_at.write_bytes((uint8_t *)data, size);
    }

    // Possible responses are SEND OK, DATA ACCEPT or CME ERROR.
    char response[16];
    response[0] = '\0';
    _at.set_at_timeout(1000);

	if (_data_transmitting_mode == NORMAL) {
		_at.resp_start("SEND OK");
		_at.read_string(response, sizeof(response));
		_at.resp_stop();
		if (strstr(response, "SEND OK") == 0) {
			return NSAPI_ERROR_DEVICE_ERROR;
		}
	} else {
		_at.resp_start("DATA ACCEPT");
		_at.read_string(response, sizeof(response));
		_at.resp_stop();
		if (strstr(response, "DATA ACCEPT") == 0) {
			return NSAPI_ERROR_DEVICE_ERROR;
		}
	}

    return NSAPI_ERROR_OK;
}

nsapi_size_or_error_t SIMCOM_HERACLES224G_CellularStack::socket_recvfrom_impl(CellularSocket *socket, SocketAddress *address,
                                                                       void *buffer, nsapi_size_t size)
{
    nsapi_size_or_error_t recv_len = 0;
    int port = -1;
    char ip_address[NSAPI_IP_SIZE + 1];

//    if (socket->proto == NSAPI_TCP) {
//        // do not read more than max size
//        size = size > HERACLES224G_MAX_RECV_SIZE ? HERACLES224G_MAX_RECV_SIZE : size;
//		_at.cmd_start_stop("+QIRD", "=", "%d%d", socket->id, size);
//
//    } else {
//        _at.cmd_start_stop("+QIRD", "=", "%d", socket->id);
//    }
//
//	_at.resp_start("+QIRD:");
//
//
//    recv_len = _at.read_int();
//    if (recv_len > 0) {
//        // UDP has remote_IP and remote_port parameters
//        if (socket->proto == NSAPI_UDP) {
//            _at.read_string(ip_address, sizeof(ip_address));
//            port = _at.read_int();
//        }
//        // do not read more than buffer size
//        recv_len = recv_len > (nsapi_size_or_error_t)size ? size : recv_len;
//        _at.read_bytes((uint8_t *)buffer, recv_len);
//    }
//    _at.resp_stop();
//
//    // We block only if 0 recv length really means no data.
//    // If 0 is followed by ip address and port can be an UDP 0 length packet
//    if (!recv_len && port < 0) {
//        return NSAPI_ERROR_WOULD_BLOCK;
//    }
//
//    if (address) {
//        address->set_ip_address(ip_address);
//        address->set_port(port);
//    }

    return recv_len;
}

void SIMCOM_HERACLES224G_CellularStack::ip2dot(const SocketAddress &ip, char *dot)
{
    if (ip.get_ip_version() == NSAPI_IPv6) {
        const uint8_t *bytes = (uint8_t *)ip.get_ip_bytes();
        for (int i = 0; i < NSAPI_IPv6_BYTES; i += 2) {
            if (i != 0) {
                *dot++ = ':';
            }
            dot += sprintf(dot, "%x", (*(bytes + i) << 8 | *(bytes + i + 1)));
        }
    } else if (ip.get_ip_version() == NSAPI_IPv4) {
        strcpy(dot, ip.get_ip_address());
    } else {
        *dot = '\0';
    }
}

nsapi_error_t SIMCOM_HERACLES224G_CellularStack::gethostbyname(const char *host, SocketAddress *address,
                                                        nsapi_version_t version, const char *interface_name)
{
    nsapi_error_t err = NSAPI_ERROR_DNS_FAILURE;

    _at.lock();

    if (address->set_ip_address(host)) {
    	err = NSAPI_ERROR_OK;
    } else {
		_at.set_at_timeout(10000); // not defined in the datasheet.
        _at.at_cmd_discard("+CDNSGIP", "=", "\"%s\"", host);
        _at.resp_start("+CDNSGIP: ");
		if (_at.info_resp()) {
			if (read_dnsgip(*address, version)) {
				err = NSAPI_ERROR_OK;
			}
		}

	}

    _at.resp_stop();
	_at.restore_at_timeout();
    _at.unlock();

    return err;
}

bool SIMCOM_HERACLES224G_CellularStack::read_dnsgip(SocketAddress &address, nsapi_version_t _dns_version)
{
	char ipAddress[NSAPI_IP_SIZE];
    if (_at.read_int() == 1) {
		//_at.resp_start("+CDNSGIP: 1,");
		_at.skip_param();
		_at.read_string(ipAddress, sizeof(ipAddress));
		if (address.set_ip_address(ipAddress)) {
			if (_dns_version == NSAPI_UNSPEC || _dns_version == address.get_ip_version()) {
				tr_info("IP host address: %s", ipAddress);
				return true;
			}
		}
    }
    tr_info("error IP host address");
    return false;
}

