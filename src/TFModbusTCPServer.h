/* TFModbusTCP
 * Copyright (C) 2024 Matthias Bolte <matthias@tinkerforge.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#pragma once

#include <stddef.h>
#include <functional>

#include "TFModbusTCPCommon.h"

// configuration
#define TF_MODBUS_TCP_SERVER_MAX_CLIENT_COUNT       8
#define TF_MODBUS_TCP_SERVER_MIN_DISPLACE_DELAY_US  (30 * 1000 * 1000) // 30 seconds
#define TF_MODBUS_TCP_SERVER_MAX_IDLE_DURATION_US   ((int64_t)120 * 60 * 1000 * 1000) // 120 minutes
#define TF_MODBUS_TCP_SERVER_IDLE_CHECK_INTERVAL_US 1000000 // 1 second
#define TF_MODBUS_TCP_SERVER_MAX_SEND_TRIES         10

enum class TFModbusTCPServerDisconnectReason
{
    NoFreeClient,
    SocketReceiveFailed, // errno
    SocketSendFailed,    // errno
    DisconnectedByPeer,
    ProtocolError,
    Displaced,
    Idle,
    ServerStopped,
};

const char *get_tf_modbus_tcp_server_client_disconnect_reason_name(TFModbusTCPServerDisconnectReason reason);

typedef std::function<void(uint32_t peer_address, uint16_t port)> TFModbusTCPServerConnectCallback;

typedef std::function<void(uint32_t peer_address, uint16_t port, TFModbusTCPServerDisconnectReason reason, int error_number)> TFModbusTCPServerDisconnectCallback;

typedef std::function<TFModbusTCPExceptionCode(uint8_t unit_id,
                                               TFModbusTCPFunctionCode function_code,
                                               uint16_t start_address,
                                               uint16_t data_count,
                                               void *data_values)> TFModbusTCPServerRequestCallback;

struct TFModbusTCPServerClientNode
{
    TFModbusTCPServerClientNode *next = nullptr;
};

struct TFModbusTCPServerClient : public TFModbusTCPServerClientNode
{
    int socket_fd;
    int64_t last_alive_us;
    TFModbusTCPRequest pending_request;
    size_t pending_request_header_used;
    bool pending_request_header_checked;
    size_t pending_request_payload_used;
    TFModbusTCPResponse response;
};

class TFModbusTCPServer
{
public:
    TFModbusTCPServer() {}
    virtual ~TFModbusTCPServer() {}

    TFModbusTCPServer(TFModbusTCPServer const &other) = delete;
    TFModbusTCPServer &operator=(TFModbusTCPServer const &other) = delete;

    bool start(uint32_t bind_address, uint16_t port,
               TFModbusTCPServerConnectCallback &&connect_callback,
               TFModbusTCPServerDisconnectCallback &&disconnect_callback,
               TFModbusTCPServerRequestCallback &&request_callback); // non-reentrant
    bool stop(); // non-reentrant
    void tick(); // non-reentrant

private:
    void disconnect(TFModbusTCPServerClient *client, TFModbusTCPServerDisconnectReason reason, int error_number);
    bool send_response(TFModbusTCPServerClient *client);

    bool non_reentrant         = false;
    int server_fd              = -1;
    int64_t last_idle_check_us = 0;
    TFModbusTCPServerConnectCallback connect_callback;
    TFModbusTCPServerDisconnectCallback disconnect_callback;
    TFModbusTCPServerRequestCallback request_callback;
    TFModbusTCPServerClientNode client_sentinel;
};
