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

#include "TFGenericTCPClient.h"

#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <lwip/sockets.h>

#include "TFNetworkUtil.h"

static_assert(TF_GENERIC_TCP_CLIENT_MIN_RECONNECT_DELAY > 0, "TF_GENERIC_TCP_CLIENT_MIN_RECONNECT_DELAY must be positive");
static_assert(TF_GENERIC_TCP_CLIENT_MIN_RECONNECT_DELAY <= TF_GENERIC_TCP_CLIENT_MAX_RECONNECT_DELAY, "TF_GENERIC_TCP_CLIENT_MIN_RECONNECT_DELAY must not be bigger than TF_GENERIC_TCP_CLIENT_MAX_RECONNECT_DELAY");

static std::function<void(const char *host_name, std::function<void(uint32_t host_address, int error_number)> &&callback)> resolve_callback;

const char *get_tf_generic_tcp_client_event_name(TFGenericTCPClientEvent event)
{
    switch (event) {
    case TFGenericTCPClientEvent::InvalidArgument:
        return "InvalidArgument";

    case TFGenericTCPClientEvent::NoResolveCallback:
        return "NoResolveCallback";

    case TFGenericTCPClientEvent::NotDisconnected:
        return "NotDisconnected";

    case TFGenericTCPClientEvent::ResolveInProgress:
        return "ResolveInProgress";

    case TFGenericTCPClientEvent::ResolveFailed:
        return "ResolveFailed";

    case TFGenericTCPClientEvent::Resolved:
        return "Resolved";

    case TFGenericTCPClientEvent::SocketCreateFailed:
        return "SocketCreateFailed";

    case TFGenericTCPClientEvent::SocketGetFlagsFailed:
        return "SocketGetFlagsFailed";

    case TFGenericTCPClientEvent::SocketSetFlagsFailed:
        return "SocketSetFlagsFailed";

    case TFGenericTCPClientEvent::SocketConnectFailed:
        return "SocketConnectFailed";

    case TFGenericTCPClientEvent::SocketSelectFailed:
        return "SocketSelectFailed";

    case TFGenericTCPClientEvent::SocketGetOptionFailed:
        return "SocketGetOptionFailed";

    case TFGenericTCPClientEvent::SocketConnectAsyncFailed:
        return "SocketConnectAsyncFailed";

    case TFGenericTCPClientEvent::SocketReceiveFailed:
        return "SocketReceiveFailed";

    case TFGenericTCPClientEvent::SocketIoctlFailed:
        return "SocketIoctlFailed";

    case TFGenericTCPClientEvent::SocketSendFailed:
        return "SocketSendFailed";

    case TFGenericTCPClientEvent::ConnectInProgress:
        return "ConnectInProgress";

    case TFGenericTCPClientEvent::ConnectTimeout:
        return "ConnectTimeout";

    case TFGenericTCPClientEvent::Connected:
        return "Connected";

    case TFGenericTCPClientEvent::Disconnected:
        return "Disconnected";

    case TFGenericTCPClientEvent::DisconnectedByPeer:
        return "DisconnectedByPeer";

    case TFGenericTCPClientEvent::ProtocolError:
        return "ProtocolError";
    }

    return "Unknown";
}

const char *get_tf_generic_tcp_client_status_name(TFGenericTCPClientStatus status)
{
    switch (status) {
    case TFGenericTCPClientStatus::Disconnected:
        return "Disconnected";

    case TFGenericTCPClientStatus::InProgress:
        return "InProgress";

    case TFGenericTCPClientStatus::Connected:
        return "Connected";
    }

    return "Unknown";
}

void set_tf_generic_tcp_client_resolve_callback(std::function<void(const char *host_name, std::function<void(uint32_t host_address, int error_number)> &&callback)> &&callback)
{
    if (callback) {
        resolve_callback = callback;
    }
}

void TFGenericTCPClient::connect(const char *host_name, uint16_t port, std::function<void(TFGenericTCPClientEvent event, int error_number)> &&callback)
{
    if (host_name == nullptr || strlen(host_name) == 0 || port == 0) {
        callback(TFGenericTCPClientEvent::InvalidArgument, 0);
        return;
    }

    if (!resolve_callback) {
        callback(TFGenericTCPClientEvent::NoResolveCallback, 0);
        return;
    }

    if (this->host_name != nullptr) {
        callback(TFGenericTCPClientEvent::NotDisconnected, 0);
        return;
    }

    this->host_name = strdup(host_name);
    this->port = port;
    event_callback = callback;
}

void TFGenericTCPClient::disconnect()
{
    if (host_name == nullptr) {
        return;
    }

    if (pending_socket_fd >= 0) {
        close(pending_socket_fd);
        pending_socket_fd = -1;
    }

    if (socket_fd >= 0) {
        close(socket_fd);
        socket_fd = -1;
    }

    std::function<void(TFGenericTCPClientEvent event, int error_number)> callback = std::move(event_callback);

    free(host_name); host_name = nullptr;
    port = 0;
    event_callback = nullptr;
    reconnect_deadline = 0;
    reconnect_delay = 0;
    resolve_pending = false;
    pending_host_address = 0;

    disconnect_hook();

    if (host_name != nullptr) {
        return; // connect() was called from transaction callback
    }

    callback(TFGenericTCPClientEvent::Disconnected, 0);
}

TFGenericTCPClientStatus TFGenericTCPClient::get_status() const
{
    if (socket_fd >= 0) {
        return TFGenericTCPClientStatus::Connected;
    }

    if (host_name != nullptr) {
        return TFGenericTCPClientStatus::InProgress;
    }

    return TFGenericTCPClientStatus::Disconnected;
}

void TFGenericTCPClient::tick()
{
    tick_hook();

    if (host_name != nullptr && socket_fd < 0) {
        if (reconnect_deadline != 0 && !TFNetworkUtil::deadline_elapsed(reconnect_deadline)) {
            return;
        }

        reconnect_deadline = 0;

        if (!resolve_pending && pending_host_address == 0 && pending_socket_fd < 0) {
            event_callback(TFGenericTCPClientEvent::ResolveInProgress, 0);

            if (host_name == nullptr) {
                return; // disconnect() was called from the event callback
            }

            resolve_pending = true;
            uint32_t current_resolve_id = ++resolve_id;

            resolve_callback(host_name, [this, current_resolve_id](uint32_t host_address, int error_number) {
                if (!resolve_pending || current_resolve_id != resolve_id) {
                    return;
                }

                if (host_address == 0) {
                    abort_connection(TFGenericTCPClientEvent::ResolveFailed, error_number);
                    return;
                }

                resolve_pending = false;
                pending_host_address = host_address;

                event_callback(TFGenericTCPClientEvent::Resolved, 0);
            });
        }

        if (pending_socket_fd < 0) {
            if (pending_host_address == 0) {
                return; // Waiting for resolve callback
            }

            event_callback(TFGenericTCPClientEvent::ConnectInProgress, 0);

            if (host_name == nullptr) {
                return; // disconnect() was called from the event callback
            }

            pending_socket_fd = socket(AF_INET, SOCK_STREAM, 0);

            if (pending_socket_fd < 0) {
                abort_connection(TFGenericTCPClientEvent::SocketCreateFailed, errno);
                return;
            }

            int flags = fcntl(pending_socket_fd, F_GETFL, 0);

            if (flags < 0) {
                abort_connection(TFGenericTCPClientEvent::SocketGetFlagsFailed, errno);
                return;
            }

            if (fcntl(pending_socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                abort_connection(TFGenericTCPClientEvent::SocketSetFlagsFailed, errno);
                return;
            }

            struct sockaddr_in addr_in;

            memset(&addr_in, 0, sizeof(addr_in));
            addr_in.sin_family = AF_INET;
            memcpy(&addr_in.sin_addr.s_addr, &pending_host_address, sizeof(pending_host_address));
            addr_in.sin_port = htons(port);

            pending_host_address = 0;

            if (::connect(pending_socket_fd, (struct sockaddr *)&addr_in, sizeof(addr_in)) < 0 && errno != EINPROGRESS) {
                abort_connection(TFGenericTCPClientEvent::SocketConnectFailed, errno);
                return;
            }

            connect_timeout_deadline = TFNetworkUtil::calculate_deadline(TF_GENERIC_TCP_CLIENT_CONNECT_TIMEOUT);
        }

        if (TFNetworkUtil::deadline_elapsed(connect_timeout_deadline)) {
            abort_connection(TFGenericTCPClientEvent::ConnectTimeout, 0);
            return;
        }

        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(pending_socket_fd, &fdset);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 0;

        int result = select(pending_socket_fd + 1, nullptr, &fdset, nullptr, &tv);

        if (result < 0) {
            abort_connection(TFGenericTCPClientEvent::SocketSelectFailed, errno);
            return;
        }

        if (result == 0) {
            return; // connect() in progress
        }

        if (!FD_ISSET(pending_socket_fd, &fdset)) {
            return; // connect() in progress
        }

        int socket_errno;
        socklen_t socket_errno_length = (socklen_t)sizeof(socket_errno);

        if (getsockopt(pending_socket_fd, SOL_SOCKET, SO_ERROR, &socket_errno, &socket_errno_length) < 0) {
            abort_connection(TFGenericTCPClientEvent::SocketGetOptionFailed, errno);
            return;
        }

        if (socket_errno != 0) {
            abort_connection(TFGenericTCPClientEvent::SocketConnectAsyncFailed, socket_errno);
            return;
        }

        reconnect_delay = 0;
        socket_fd = pending_socket_fd;
        pending_socket_fd = -1;
        event_callback(TFGenericTCPClientEvent::Connected, 0);
    }

    uint32_t tick_deadline = TFNetworkUtil::calculate_deadline(TF_GENERIC_TCP_CLIENT_MAX_TICK_DURATION);
    bool first = true;

    while (socket_fd >= 0 && (TFNetworkUtil::deadline_elapsed(tick_deadline) || first)) {
        first = false;

        if (!receive_hook()) {
            return;
        }
    }
}

void TFGenericTCPClient::abort_connection(TFGenericTCPClientEvent event, int error_number)
{
    if (pending_socket_fd >= 0) {
        close(pending_socket_fd);
        pending_socket_fd = -1;
    }

    if (socket_fd >= 0) {
        close(socket_fd);
        socket_fd = -1;
    }

    if (reconnect_delay == 0) {
        reconnect_delay = TF_GENERIC_TCP_CLIENT_MIN_RECONNECT_DELAY;
    }
    else {
        reconnect_delay *= 2;

        if (reconnect_delay > TF_GENERIC_TCP_CLIENT_MAX_RECONNECT_DELAY) {
            reconnect_delay = TF_GENERIC_TCP_CLIENT_MAX_RECONNECT_DELAY;
        }
    }

    reconnect_deadline = TFNetworkUtil::calculate_deadline(reconnect_delay);
    resolve_pending = false;
    pending_host_address = 0;

    abort_connection_hook();
    event_callback(event, error_number);
}
