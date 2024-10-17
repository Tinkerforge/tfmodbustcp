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
#include <sys/types.h>
#include <lwip/sockets.h>

#include "TFNetworkUtil.h"

const char *get_tf_generic_tcp_client_connect_result_name(TFGenericTCPClientConnectResult result)
{
    switch (result) {
    case TFGenericTCPClientConnectResult::InvalidArgument:
        return "InvalidArgument";

    case TFGenericTCPClientConnectResult::NoFreePoolSlot:
        return "NoFreePoolSlot";

    case TFGenericTCPClientConnectResult::NoFreePoolHandle:
        return "NoFreePoolHandle";

    case TFGenericTCPClientConnectResult::NestedConnect:
        return "NestedConnect";

    case TFGenericTCPClientConnectResult::AbortRequested:
        return "AbortRequested";

    case TFGenericTCPClientConnectResult::ResolveFailed:
        return "ResolveFailed";

    case TFGenericTCPClientConnectResult::SocketCreateFailed:
        return "SocketCreateFailed";

    case TFGenericTCPClientConnectResult::SocketGetFlagsFailed:
        return "SocketGetFlagsFailed";

    case TFGenericTCPClientConnectResult::SocketSetFlagsFailed:
        return "SocketSetFlagsFailed";

    case TFGenericTCPClientConnectResult::SocketConnectFailed:
        return "SocketConnectFailed";

    case TFGenericTCPClientConnectResult::SocketSelectFailed:
        return "SocketSelectFailed";

    case TFGenericTCPClientConnectResult::SocketGetOptionFailed:
        return "SocketGetOptionFailed";

    case TFGenericTCPClientConnectResult::SocketConnectAsyncFailed:
        return "SocketConnectAsyncFailed";

    case TFGenericTCPClientConnectResult::Timeout:
        return "Timeout";

    case TFGenericTCPClientConnectResult::Connected:
        return "Connected";
    }

    return "Unknown";
}

const char *get_tf_generic_tcp_client_disconnect_reason_name(TFGenericTCPClientDisconnectReason reason)
{
    switch (reason) {
    case TFGenericTCPClientDisconnectReason::Requested:
        return "Requested";

    case TFGenericTCPClientDisconnectReason::SocketSelectFailed:
        return "SocketSelectFailed";

    case TFGenericTCPClientDisconnectReason::SocketReceiveFailed:
        return "SocketReceiveFailed";

    case TFGenericTCPClientDisconnectReason::SocketIoctlFailed:
        return "SocketIoctlFailed";

    case TFGenericTCPClientDisconnectReason::SocketSendFailed:
        return "SocketSendFailed";

    case TFGenericTCPClientDisconnectReason::DisconnectedByPeer:
        return "DisconnectedByPeer";

    case TFGenericTCPClientDisconnectReason::ProtocolError:
        return "ProtocolError";
    }

    return "Unknown";
}

const char *get_tf_generic_tcp_client_connection_status_name(TFGenericTCPClientConnectionStatus status)
{
    switch (status) {
    case TFGenericTCPClientConnectionStatus::Disconnected:
        return "Disconnected";

    case TFGenericTCPClientConnectionStatus::InProgress:
        return "InProgress";

    case TFGenericTCPClientConnectionStatus::Connected:
        return "Connected";
    }

    return "Unknown";
}

void TFGenericTCPClient::connect(const char *host_name, uint16_t port,
                                 TFGenericTCPClientConnectCallback &&connect_callback,
                                 TFGenericTCPClientDisconnectCallback &&disconnect_callback)
{
    if (host_name == nullptr || strlen(host_name) == 0 || port == 0 || !connect_callback || !disconnect_callback) {
        connect_callback(TFGenericTCPClientConnectResult::InvalidArgument, -1);
        return;
    }

    uint32_t current_connect_id = ++connect_id;

    disconnect();

    if (current_connect_id != connect_id) {
        connect_callback(TFGenericTCPClientConnectResult::NestedConnect, -1);
        return;
    }

    this->host_name                   = strdup(host_name);
    this->port                        = port;
    this->connect_callback            = std::move(connect_callback);
    this->pending_disconnect_callback = std::move(disconnect_callback);
}

void TFGenericTCPClient::disconnect()
{
    TFGenericTCPClientConnectCallback connect_callback       = std::move(this->connect_callback);
    TFGenericTCPClientDisconnectCallback disconnect_callback = std::move(this->disconnect_callback);

    this->connect_callback    = nullptr;
    this->disconnect_callback = nullptr;

    close();

    if (connect_callback) {
        connect_callback(TFGenericTCPClientConnectResult::AbortRequested, -1);
    }

    if (disconnect_callback) {
        disconnect_callback(TFGenericTCPClientDisconnectReason::Requested, -1);
    }
}

TFGenericTCPClientConnectionStatus TFGenericTCPClient::get_connection_status() const
{
    if (socket_fd >= 0) {
        return TFGenericTCPClientConnectionStatus::Connected;
    }

    if (host_name != nullptr) {
        return TFGenericTCPClientConnectionStatus::InProgress;
    }

    return TFGenericTCPClientConnectionStatus::Disconnected;
}

void TFGenericTCPClient::tick()
{
    tick_hook();

    if (host_name != nullptr && socket_fd < 0) {
        if (!resolve_pending && pending_host_address == 0 && pending_socket_fd < 0) {
            resolve_pending             = true;
            uint32_t current_resolve_id = ++resolve_id;

            tf_network_util_debugfln("TFGenericTCPClient[%p]::tick() resolving (host_name=%s current_resolve_id=%u)",
                                     static_cast<void *>(this), host_name, current_resolve_id);

            TFNetworkUtil::resolve(host_name, [this, current_resolve_id](uint32_t host_address, int error_number) {
                tf_network_util_debugfln("TFGenericTCPClient[%p]::tick() resolved (resolve_pending=%d current_resolve_id=%u resolve_id=%u host_address=%u error_number=%d)",
                                         static_cast<void *>(this), static_cast<int>(resolve_pending), current_resolve_id, resolve_id, host_address, error_number);

                if (!resolve_pending || current_resolve_id != resolve_id) {
                    return;
                }

                if (host_address == 0) {
                    abort_connect(TFGenericTCPClientConnectResult::ResolveFailed, error_number);
                    return;
                }

                resolve_pending      = false;
                pending_host_address = host_address;
            });
        }

        if (pending_socket_fd < 0) {
            if (pending_host_address == 0) {
                return; // Waiting for resolve callback
            }

            tf_network_util_debugfln("TFGenericTCPClient[%p]::tick() connecting (host_name=%s pending_host_address=%u)",
                                     static_cast<void *>(this), host_name, pending_host_address);

            pending_socket_fd = socket(AF_INET, SOCK_STREAM, 0);

            if (pending_socket_fd < 0) {
                abort_connect(TFGenericTCPClientConnectResult::SocketCreateFailed, errno);
                return;
            }

            int flags = fcntl(pending_socket_fd, F_GETFL, 0);

            if (flags < 0) {
                abort_connect(TFGenericTCPClientConnectResult::SocketGetFlagsFailed, errno);
                return;
            }

            if (fcntl(pending_socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                abort_connect(TFGenericTCPClientConnectResult::SocketSetFlagsFailed, errno);
                return;
            }

            struct sockaddr_in addr_in;

            memset(&addr_in, 0, sizeof(addr_in));
            memcpy(&addr_in.sin_addr.s_addr, &pending_host_address, sizeof(pending_host_address));

            addr_in.sin_family = AF_INET;
            addr_in.sin_port   = htons(port);

            pending_host_address = 0;

            if (::connect(pending_socket_fd, reinterpret_cast<struct sockaddr *>(&addr_in), sizeof(addr_in)) < 0 && errno != EINPROGRESS) {
                abort_connect(TFGenericTCPClientConnectResult::SocketConnectFailed, errno);
                return;
            }

            connect_deadline_us = TFNetworkUtil::calculate_deadline(TF_GENERIC_TCP_CLIENT_CONNECT_TIMEOUT_US);
        }

        if (TFNetworkUtil::deadline_elapsed(connect_deadline_us)) {
            abort_connect(TFGenericTCPClientConnectResult::Timeout, -1);
            return;
        }

        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(pending_socket_fd, &fdset);

        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 0;

        int result = select(pending_socket_fd + 1, nullptr, &fdset, nullptr, &tv);

        if (result < 0) {
            abort_connect(TFGenericTCPClientConnectResult::SocketSelectFailed, errno);
            return;
        }

        if (result == 0) {
            return; // connect() in progress
        }

        if (!FD_ISSET(pending_socket_fd, &fdset)) {
            return; // connect() in progress
        }

        int socket_errno;
        socklen_t socket_errno_length = sizeof(socket_errno);

        if (getsockopt(pending_socket_fd, SOL_SOCKET, SO_ERROR, &socket_errno, &socket_errno_length) < 0) {
            abort_connect(TFGenericTCPClientConnectResult::SocketGetOptionFailed, errno);
            return;
        }

        if (socket_errno != 0) {
            abort_connect(TFGenericTCPClientConnectResult::SocketConnectAsyncFailed, socket_errno);
            return;
        }

        TFGenericTCPClientConnectCallback callback = std::move(connect_callback);

        socket_fd                   = pending_socket_fd;
        pending_socket_fd           = -1;
        connect_callback            = nullptr;
        disconnect_callback         = std::move(pending_disconnect_callback);
        pending_disconnect_callback = nullptr;

        callback(TFGenericTCPClientConnectResult::Connected, -1);
    }

    int64_t tick_deadline_us = TFNetworkUtil::calculate_deadline(TF_GENERIC_TCP_CLIENT_MAX_TICK_DURATION_US);
    bool first = true;

    while (socket_fd >= 0 && (!TFNetworkUtil::deadline_elapsed(tick_deadline_us) || first)) {
        first = false;

        if (!receive_hook()) {
            return;
        }
    }
}

void TFGenericTCPClient::close()
{
    if (pending_socket_fd >= 0) {
        ::close(pending_socket_fd);
        pending_socket_fd = -1;
    }

    if (socket_fd >= 0) {
        ::close(socket_fd);
        socket_fd = -1;
    }

    free(host_name); host_name = nullptr;
    port = 0;
    connect_callback = nullptr;
    pending_disconnect_callback = nullptr;
    disconnect_callback = nullptr;
    resolve_pending = false;
    pending_host_address = 0;

    close_hook();
}

bool TFGenericTCPClient::send(const uint8_t *buffer, size_t length)
{
    size_t buffer_send = 0;
    size_t tries_remaining = TF_GENERIC_TCP_CLIENT_MAX_SEND_TRIES;

    while (tries_remaining > 0 && buffer_send < length) {
        --tries_remaining;

        ssize_t result = ::send(socket_fd, buffer + buffer_send, length - buffer_send, 0);

        if (result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }

            return false;
        }

        buffer_send += result;
    }

    return true;
}

void TFGenericTCPClient::abort_connect(TFGenericTCPClientConnectResult result, int error_number)
{
    TFGenericTCPClientConnectCallback callback = std::move(connect_callback);

    connect_callback = nullptr;

    close();

    if (callback) {
        callback(result, error_number);
    }
}

void TFGenericTCPClient::disconnect(TFGenericTCPClientDisconnectReason reason, int error_number)
{
    TFGenericTCPClientDisconnectCallback callback = std::move(disconnect_callback);

    disconnect_callback = nullptr;

    close();

    if (callback) {
        callback(reason, error_number);
    }
}
