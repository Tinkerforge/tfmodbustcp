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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <functional>

// configuration
#define TF_GENERIC_TCP_CLIENT_MAX_TICK_DURATION_US 10000
#define TF_GENERIC_TCP_CLIENT_CONNECT_TIMEOUT_US 3000000
#define TF_GENERIC_TCP_CLIENT_MAX_SEND_TRIES 10

enum class TFGenericTCPClientConnectResult
{
    InvalidArgument,
    NoFreePoolSlot,
    NoFreePoolHandle,
    NestedConnect,
    AbortRequested,
    ResolveFailed,            // errno as received from resolve callback
    SocketCreateFailed,       // errno
    SocketGetFlagsFailed,     // errno
    SocketSetFlagsFailed,     // errno
    SocketConnectFailed,      // errno
    SocketSelectFailed,       // errno
    SocketGetOptionFailed,    // errno
    SocketConnectAsyncFailed, // errno
    Timeout,
    Connected,
};

const char *get_tf_generic_tcp_client_connect_result_name(TFGenericTCPClientConnectResult result);

enum class TFGenericTCPClientDisconnectReason
{
    Requested,
    SocketSelectFailed,  // errno
    SocketReceiveFailed, // errno
    SocketIoctlFailed,   // errno
    SocketSendFailed,    // errno
    DisconnectedByPeer,
    ProtocolError,
};

const char *get_tf_generic_tcp_client_disconnect_reason_name(TFGenericTCPClientDisconnectReason reason);

enum class TFGenericTCPClientConnectionStatus
{
    Disconnected,
    InProgress,
    Connected,
};

const char *get_tf_generic_tcp_client_connection_status_name(TFGenericTCPClientConnectionStatus status);

typedef std::function<void(TFGenericTCPClientConnectResult result, int error_number)> TFGenericTCPClientConnectCallback;
typedef std::function<void(TFGenericTCPClientDisconnectReason reason, int error_number)> TFGenericTCPClientDisconnectCallback;

class TFGenericTCPClient
{
public:
    TFGenericTCPClient() {};
    virtual ~TFGenericTCPClient() {};

    void connect(const char *host_name, uint16_t port, TFGenericTCPClientConnectCallback &&connect_callback, TFGenericTCPClientDisconnectCallback &&disconnect_callback);
    void disconnect();
    const char *get_host_name() const { return host_name; }
    uint16_t get_port() const { return port; }
    TFGenericTCPClientConnectionStatus get_connection_status() const;
    void tick();

protected:
    virtual void close_hook()   = 0;
    virtual void tick_hook()    = 0;
    virtual bool receive_hook() = 0;

    void close();
    bool send(const uint8_t *buffer, size_t length);
    void abort_connect(TFGenericTCPClientConnectResult result, int error_number);
    void disconnect(TFGenericTCPClientDisconnectReason reason, int error_number);

    char *host_name = nullptr;
    uint16_t port   = 0;
    TFGenericTCPClientConnectCallback connect_callback;
    TFGenericTCPClientDisconnectCallback pending_disconnect_callback;
    TFGenericTCPClientDisconnectCallback disconnect_callback;
    uint32_t connect_id           = 0;
    bool resolve_pending          = false;
    uint32_t resolve_id           = 0;
    uint32_t pending_host_address = 0; // IPv4 only
    int pending_socket_fd         = -1;
    int64_t connect_deadline_us   = 0;
    int socket_fd                 = -1;
};
