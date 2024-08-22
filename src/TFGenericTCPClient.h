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
#define TF_GENERIC_TCP_CLIENT_MAX_TICK_DURATION 10 // milliseconds
#define TF_GENERIC_TCP_CLIENT_MIN_RECONNECT_DELAY 1000 // milliseconds
#define TF_GENERIC_TCP_CLIENT_MAX_RECONNECT_DELAY 16000 // milliseconds
#define TF_GENERIC_TCP_CLIENT_CONNECT_TIMEOUT 3000 // milliseconds

enum class TFGenericTCPClientEvent
{
    InvalidArgument, // final
    NoResolveCallback, // final
    NotDisconnected, // final
    ResolveInProgress,
    ResolveFailed, // errno as received from resolve callback
    Resolved,
    SocketCreateFailed, // errno
    SocketGetFlagsFailed, // errno
    SocketSetFlagsFailed, // errno
    SocketConnectFailed, // errno
    SocketSelectFailed, // errno
    SocketGetOptionFailed, // errno
    SocketConnectAsyncFailed, // errno
    SocketReceiveFailed, // errno
    SocketIoctlFailed, // errno
    SocketSendFailed, // errno
    ConnectInProgress,
    ConnectTimeout,
    Connected,
    Disconnected, // final
    DisconnectedByPeer,
    ProtocolError,
};

const char *get_tf_generic_tcp_client_event_name(TFGenericTCPClientEvent event);

enum class TFGenericTCPClientStatus
{
    Disconnected,
    InProgress,
    Connected,
};

const char *get_tf_generic_tcp_client_status_name(TFGenericTCPClientStatus status);

void set_tf_generic_tcp_client_resolve_callback(std::function<void(const char *host_name, std::function<void(uint32_t host_address, int error_number)> &&callback)> &&callback);

class TFGenericTCPClient
{
public:
    TFGenericTCPClient() { }

    void connect(const char *host_name, uint16_t port, std::function<void(TFGenericTCPClientEvent event, int error_number)> &&callback);
    void disconnect();
    TFGenericTCPClientStatus get_status() const;
    void tick();

protected:
    virtual void disconnect_hook() = 0;
    virtual void tick_hook() = 0;
    virtual bool receive_hook() = 0;
    virtual void abort_connection_hook() = 0;

    void abort_connection(TFGenericTCPClientEvent event, int error_number);

    char *host_name = nullptr;
    uint16_t port = 0;
    std::function<void(TFGenericTCPClientEvent event, int error_number)> event_callback;
    uint32_t connect_id = 0;
    uint32_t reconnect_deadline = 0;
    uint32_t reconnect_delay = 0;
    bool resolve_pending = false;
    uint32_t resolve_id = 0;
    uint32_t pending_host_address = 0; // IPv4 only
    int pending_socket_fd = -1;
    uint32_t connect_timeout_deadline = 0;
    int socket_fd = -1;
};
