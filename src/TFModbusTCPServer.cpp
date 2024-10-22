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

#include "TFModbusTCPServer.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <lwip/sockets.h>
#include <algorithm>

#include "TFNetworkUtil.h"

const char *get_tf_modbus_tcp_server_client_disconnect_reason_name(TFModbusTCPServerDisconnectReason reason)
{
    switch (reason) {
    case TFModbusTCPServerDisconnectReason::NoFreeClient:
        return "NoFreeClient";

    case TFModbusTCPServerDisconnectReason::SocketReceiveFailed:
        return "SocketReceiveFailed";

    case TFModbusTCPServerDisconnectReason::SocketSendFailed:
        return "SocketSendFailed";

    case TFModbusTCPServerDisconnectReason::DisconnectedByPeer:
        return "DisconnectedByPeer";

    case TFModbusTCPServerDisconnectReason::ProtocolError:
        return "ProtocolError";

    case TFModbusTCPServerDisconnectReason::ServerStopped:
        return "ServerStopped";
    }

    return "Unknown";
}

bool TFModbusTCPServer::start(uint32_t bind_address, uint16_t port,
                              TFModbusTCPServerConnectCallback &&connect_callback,
                              TFModbusTCPServerDisconnectCallback &&disconnect_callback,
                              TFModbusTCPServerRequestCallback &&request_callback)
{
    tf_network_util_debugfln("TFGenericTCPClient[%p]::start(bind_address=%u port=%u)",
                             static_cast<void *>(this), bind_address, port);

    if (port == 0 || !connect_callback || !disconnect_callback || !request_callback) {
        tf_network_util_debugfln("TFModbusTCPServer[%p]::start(bind_address=%u port=%u) invalid argument",
                                 static_cast<void *>(this), bind_address, port);

        errno = EINVAL;
        return false;
    }

    if (server_fd >= 0) {
        tf_network_util_debugfln("TFModbusTCPServer[%p]::start(bind_address=%u port=%u) already running",
                                 static_cast<void *>(this), bind_address, port);

        errno = EBUSY;
        return false;
    }

    int pending_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (pending_fd < 0) {
        int saved_errno = errno;

        tf_network_util_debugfln("TFModbusTCPServer[%p]::start(bind_address=%u port=%u) socket() failed: %s (%d)",
                                 static_cast<void *>(this), bind_address, port, strerror(saved_errno), saved_errno);

        errno = saved_errno;
        return false;
    }

    int reuse_addr = 1;

    if (setsockopt(pending_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) < 0) {
        int saved_errno = errno;

        tf_network_util_debugfln("TFModbusTCPServer[%p]::start(bind_address=%u port=%u) setsockopt(SO_REUSEADDR) failed: %s (%d)",
                                 static_cast<void *>(this), bind_address, port, strerror(saved_errno), saved_errno);

        errno = saved_errno;
        return false;
    }

    int flags = fcntl(pending_fd, F_GETFL, 0);

    if (flags < 0) {
        int saved_errno = errno;

        tf_network_util_debugfln("TFModbusTCPServer[%p]::start(bind_address=%u port=%u) fcntl(F_GETFL) failed: %s (%d)",
                                 static_cast<void *>(this), bind_address, port, strerror(saved_errno), saved_errno);

        errno = saved_errno;
        return false;
    }

    if (fcntl(pending_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        int saved_errno = errno;

        tf_network_util_debugfln("TFModbusTCPServer[%p]::start(bind_address=%u port=%u) fcntl(F_SETFL) failed: %s (%d)",
                                 static_cast<void *>(this), bind_address, port, strerror(saved_errno), saved_errno);

        errno = saved_errno;
        return false;
    }

    struct sockaddr_in addr_in;

    memset(&addr_in, 0, sizeof(addr_in));
    memcpy(&addr_in.sin_addr.s_addr, &bind_address, sizeof(bind_address));

    addr_in.sin_family = AF_INET;
    addr_in.sin_port   = htons(port);

    if (bind(pending_fd, (struct sockaddr *)&addr_in, sizeof(addr_in)) < 0) {
        int saved_errno = errno;

        tf_network_util_debugfln("TFModbusTCPServer[%p]::start(bind_address=%u port=%u) bind() failed: %s (%d)",
                                 static_cast<void *>(this), bind_address, port, strerror(saved_errno), saved_errno);

        errno = saved_errno;
        return false;
    }

    if (listen(pending_fd, 5) < 0) {
        int saved_errno = errno;

        tf_network_util_debugfln("TFModbusTCPServer[%p]::start(bind_address=%u port=%u) listen() failed: %s (%d)",
                                 static_cast<void *>(this), bind_address, port, strerror(saved_errno), saved_errno);

        errno = saved_errno;
        return false;
    }

    server_fd                 = pending_fd;
    this->connect_callback    = std::move(connect_callback);
    this->disconnect_callback = std::move(disconnect_callback);
    this->request_callback    = std::move(request_callback);

    return true;
}

void TFModbusTCPServer::stop()
{
    if (server_fd < 0) {
        return;
    }

    tf_network_util_debugfln("TFModbusTCPServer[%p]::stop()",
                             static_cast<void *>(this));

    shutdown(server_fd, SHUT_RDWR);
    close(server_fd);
    server_fd = -1;

    TFModbusTCPServerClient *client = client_sentinel.next;
    client_sentinel.next            = nullptr;

    while (client != nullptr) {
        TFModbusTCPServerClient *client_next = client->next;

        disconnect(client, TFModbusTCPServerDisconnectReason::ServerStopped, -1);
        client = client_next;
    }

    connect_callback    = nullptr;
    disconnect_callback = nullptr;
    request_callback    = nullptr;
}

void TFModbusTCPServer::tick()
{
    if (server_fd < 0) {
        return;
    }

    fd_set fdset;
    int fd_max = server_fd;

    FD_ZERO(&fdset);
    FD_SET(server_fd, &fdset);

    for (TFModbusTCPServerClient *client = client_sentinel.next; client != nullptr; client = client->next) {
        FD_SET(client->socket_fd, &fdset);

        fd_max = std::max(fd_max, client->socket_fd);
    }

    struct timeval tv;
    tv.tv_sec  = 0;
    tv.tv_usec = 0;

    int result = select(fd_max + 1, &fdset, nullptr, nullptr, &tv);

    if (result < 0) {
        tf_network_util_debugfln("TFModbusTCPServer[%p]::tick() select() failed: %s (%d)",
                                 static_cast<void *>(this), strerror(errno), errno);
        return;
    }

    if (result == 0) {
        return; // timeout, nothing to do
    }

    if (FD_ISSET(server_fd, &fdset)) {
        struct sockaddr_in addr_in;
        socklen_t addr_in_length = sizeof(addr_in);
        int socket_fd = accept(server_fd, reinterpret_cast<struct sockaddr *>(&addr_in), &addr_in_length);

        if (socket_fd < 0) {
            tf_network_util_debugfln("TFModbusTCPServer[%p]::tick() accept() failed: %s (%d)",
                                    static_cast<void *>(this), strerror(errno), errno);
            return;
        }

        uint32_t peer_address = addr_in.sin_addr.s_addr;
        uint16_t port         = ntohs(addr_in.sin_port);

        tf_network_util_debugfln("TFModbusTCPClient[%p]::tick() accepting connection (socket_fd=%d peer_address=%u port=%u)",
                                 static_cast<void *>(this), socket_fd, peer_address, port);

        TFModbusTCPServerClient **tail_ptr = &client_sentinel.next;
        size_t client_count = 0;

        while (*tail_ptr != nullptr) {
            tail_ptr = &(*tail_ptr)->next;
            ++client_count;
        }

        if (client_count >= TF_MODBUS_TCP_SERVER_MAX_CLIENT_COUNT) {
            tf_network_util_debugfln("TFModbusTCPClient[%p]::tick() no free client for connection (socket_fd=%d peer_address=%u port=%u)",
                                    static_cast<void *>(this), socket_fd, peer_address, port);

            shutdown(socket_fd, SHUT_RDWR);
            close(socket_fd);
            disconnect_callback(peer_address, port, TFModbusTCPServerDisconnectReason::NoFreeClient, -1);
        }
        else {
            TFModbusTCPServerClient *client = new TFModbusTCPServerClient;

            tf_network_util_debugfln("TFModbusTCPClient[%p]::tick() allocating client for connection (client=%p socket_fd=%d peer_address=%u port=%u)",
                                    static_cast<void *>(this), static_cast<void *>(client), socket_fd, peer_address, port);

            client->socket_fd                      = socket_fd;
            client->pending_request_header_used    = 0;
            client->pending_request_header_checked = false;
            client->pending_request_payload_used   = 0;
            client->next                           = nullptr;

            *tail_ptr = client;

            connect_callback(peer_address, port);
        }
    }

    TFModbusTCPServerClientSentinel *client_prev = &client_sentinel;
    TFModbusTCPServerClient *client = nullptr;
    TFModbusTCPServerClient *client_next;

#define disconnect_and_unlink(reason, error_number) \
    do { \
        client_prev->next = client_next; \
        disconnect(client, reason, error_number); \
        client = nullptr; \
    } while (0)

    while (true) {
        if (client != nullptr) {
            client_prev = client;
        }

        client = client_prev->next;

        if (client == nullptr) {
            break;
        }

        client_next = client->next;

        if (!FD_ISSET(client->socket_fd, &fdset)) {
            continue;
        }

        size_t pending_request_header_missing = sizeof(client->pending_request.header) - client->pending_request_header_used;

        if (pending_request_header_missing > 0) {
            ssize_t result = recv(client->socket_fd,
                                  client->pending_request.header.bytes + client->pending_request_header_used,
                                  pending_request_header_missing,
                                  0);

            if (result < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    int saved_errno = errno;

                    tf_network_util_debugfln("TFModbusTCPClient[%p]::tick() disconnecting client due to receive error (client=%p errno=%d)",
                                             static_cast<void *>(this), static_cast<void *>(client), saved_errno);

                    disconnect_and_unlink(TFModbusTCPServerDisconnectReason::SocketReceiveFailed, saved_errno);
                }

                continue;
            }

            if (result == 0) {
                tf_network_util_debugfln("TFModbusTCPClient[%p]::tick() client disconnected by peer (client=%p)",
                                         static_cast<void *>(this), static_cast<void *>(client));

                disconnect_and_unlink(TFModbusTCPServerDisconnectReason::DisconnectedByPeer, -1);
                continue;
            }

            client->pending_request_header_used += result;
            pending_request_header_missing -= result;

            if (pending_request_header_missing > 0) {
                continue;
            }
        }

        if (!client->pending_request_header_checked) {
            client->pending_request.header.transaction_id = ntohs(client->pending_request.header.transaction_id);
            client->pending_request.header.protocol_id    = ntohs(client->pending_request.header.protocol_id);
            client->pending_request.header.frame_length   = ntohs(client->pending_request.header.frame_length);

            if (client->pending_request.header.protocol_id != 0) {
                tf_network_util_debugfln("TFModbusTCPClient[%p]::tick() disconnecting client due to protocol error (client=%p protocol_id=%u)",
                                         static_cast<void *>(this), static_cast<void *>(client), client->pending_request.header.protocol_id);

                disconnect_and_unlink(TFModbusTCPServerDisconnectReason::ProtocolError, -1);
                continue;
            }

            if (client->pending_request.header.frame_length < TF_MODBUS_TCP_MIN_FRAME_LENGTH) {
                tf_network_util_debugfln("TFModbusTCPClient[%p]::tick() disconnecting client due to protocol error (client=%p frame_length=%u)",
                                         static_cast<void *>(this), static_cast<void *>(client), client->pending_request.header.frame_length);

                disconnect_and_unlink(TFModbusTCPServerDisconnectReason::ProtocolError, -1);
                continue;
            }

            if (client->pending_request.header.frame_length > TF_MODBUS_TCP_MAX_FRAME_LENGTH) {
                tf_network_util_debugfln("TFModbusTCPClient[%p]::tick() disconnecting client due to protocol error (client=%p frame_length=%u)",
                                         static_cast<void *>(this), static_cast<void *>(client), client->pending_request.header.frame_length);

                disconnect_and_unlink(TFModbusTCPServerDisconnectReason::ProtocolError, -1);
                continue;
            }

            client->pending_request_header_checked = true;
        }

        size_t pending_request_payload_missing = client->pending_request.header.frame_length
                                               - TF_MODBUS_TCP_FRAME_IN_HEADER_LENGTH
                                               - client->pending_request_payload_used;

        if (pending_request_payload_missing > 0) {
            ssize_t result = recv(client->socket_fd,
                                  client->pending_request.payload.bytes + client->pending_request_payload_used,
                                  pending_request_payload_missing,
                                  0);

            if (result < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    int saved_errno = errno;

                    tf_network_util_debugfln("TFModbusTCPClient[%p]::tick() disconnecting client due to receive error (client=%p errno=%d)",
                                             static_cast<void *>(this), static_cast<void *>(client), saved_errno);

                    disconnect_and_unlink(TFModbusTCPServerDisconnectReason::SocketReceiveFailed, saved_errno);
                }

                continue;
            }

            if (result == 0) {
                tf_network_util_debugfln("TFModbusTCPClient[%p]::tick() client disconnected by peer (client=%p)",
                                         static_cast<void *>(this), static_cast<void *>(client));

                disconnect_and_unlink(TFModbusTCPServerDisconnectReason::DisconnectedByPeer, -1);
                continue;
            }

            client->pending_request_payload_used += result;
            pending_request_payload_missing -= result;

            if (pending_request_payload_missing > 0) {
                continue;
            }
        }

        TFModbusTCPExceptionCode exception_code = TFModbusTCPExceptionCode::Success;

        switch (static_cast<TFModbusTCPFunctionCode>(client->pending_request.payload.function_code)) {
        case TFModbusTCPFunctionCode::ReadCoils:
        case TFModbusTCPFunctionCode::ReadDiscreteInputs:
            client->pending_request.payload.start_address = ntohs(client->pending_request.payload.start_address);
            client->pending_request.payload.data_count    = ntohs(client->pending_request.payload.data_count);

            if (client->pending_request.payload.data_count < TF_MODBUS_TCP_MIN_READ_COIL_COUNT
             || client->pending_request.payload.data_count > TF_MODBUS_TCP_MAX_READ_COIL_COUNT) {
                exception_code = TFModbusTCPExceptionCode::IllegalDataValue;
            }
            else {
                client->response.payload.byte_count  = (client->pending_request.payload.data_count + 7) / 8;
                client->response.header.frame_length = TF_MODBUS_TCP_FRAME_IN_HEADER_LENGTH
                                                     + offsetof(TFModbusTCPResponsePayload, coil_values)
                                                     + client->response.payload.byte_count;

                memset(client->response.payload.coil_values, 0, client->response.payload.byte_count);

                exception_code = request_callback(client->pending_request.header.unit_id,
                                                  static_cast<TFModbusTCPFunctionCode>(client->pending_request.payload.function_code),
                                                  client->pending_request.payload.start_address,
                                                  client->pending_request.payload.data_count,
                                                  client->response.payload.coil_values);
            }

            break;

        case TFModbusTCPFunctionCode::ReadHoldingRegisters:
        case TFModbusTCPFunctionCode::ReadInputRegisters:
            client->pending_request.payload.start_address = ntohs(client->pending_request.payload.start_address);
            client->pending_request.payload.data_count    = ntohs(client->pending_request.payload.data_count);

            if (client->pending_request.payload.data_count < TF_MODBUS_TCP_MIN_READ_REGISTER_COUNT
             || client->pending_request.payload.data_count > TF_MODBUS_TCP_MAX_READ_REGISTER_COUNT) {
                exception_code = TFModbusTCPExceptionCode::IllegalDataValue;
            }
            else {
                client->response.payload.byte_count  = client->pending_request.payload.data_count * 2;
                client->response.header.frame_length = TF_MODBUS_TCP_FRAME_IN_HEADER_LENGTH
                                                     + offsetof(TFModbusTCPResponsePayload, register_values)
                                                     + client->response.payload.byte_count;

                memset(client->response.payload.register_values, 0, client->response.payload.byte_count);

                exception_code = request_callback(client->pending_request.header.unit_id,
                                                  static_cast<TFModbusTCPFunctionCode>(client->pending_request.payload.function_code),
                                                  client->pending_request.payload.start_address,
                                                  client->pending_request.payload.data_count,
                                                  client->response.payload.register_values);

                for (size_t i = 0; i < client->pending_request.payload.data_count; ++i) {
                    client->response.payload.register_values[i] = htons(client->response.payload.register_values[i]);
                }
            }

            break;

        case TFModbusTCPFunctionCode::WriteSingleCoil:
            client->pending_request.payload.start_address = ntohs(client->pending_request.payload.start_address);
            client->pending_request.payload.data_value    = ntohs(client->pending_request.payload.data_value);

            if (client->pending_request.payload.data_value != 0x0000
             && client->pending_request.payload.data_value != 0xFF00) {
                exception_code = TFModbusTCPExceptionCode::IllegalDataValue;
            }
            else {
                client->response.header.frame_length   = TF_MODBUS_TCP_FRAME_IN_HEADER_LENGTH
                                                       + offsetof(TFModbusTCPResponsePayload, write_sentinel);
                client->response.payload.start_address = htons(client->pending_request.payload.start_address);
                client->response.payload.data_value    = htons(client->pending_request.payload.data_value);

                uint8_t coil_values[1] = {static_cast<uint8_t>(client->pending_request.payload.data_value == 0xFF00 ? 1 : 0)};

                exception_code = request_callback(client->pending_request.header.unit_id,
                                                  TFModbusTCPFunctionCode::WriteMultipleCoils,
                                                  client->pending_request.payload.start_address,
                                                  1,
                                                  coil_values);
            }

            break;

        case TFModbusTCPFunctionCode::WriteSingleRegister:
            client->pending_request.payload.start_address = ntohs(client->pending_request.payload.start_address);
            client->pending_request.payload.data_value    = ntohs(client->pending_request.payload.data_value);

            {
                client->response.header.frame_length   = TF_MODBUS_TCP_FRAME_IN_HEADER_LENGTH
                                                       + offsetof(TFModbusTCPResponsePayload, write_sentinel);
                client->response.payload.start_address = htons(client->pending_request.payload.start_address);
                client->response.payload.data_value    = htons(client->pending_request.payload.data_value);

                uint16_t register_values[1] = {client->pending_request.payload.data_value};

                exception_code = request_callback(client->pending_request.header.unit_id,
                                                  TFModbusTCPFunctionCode::WriteMultipleRegisters,
                                                  client->pending_request.payload.start_address,
                                                  1,
                                                  register_values);
            }

            break;

        case TFModbusTCPFunctionCode::WriteMultipleCoils:
            client->pending_request.payload.start_address = ntohs(client->pending_request.payload.start_address);
            client->pending_request.payload.data_count    = ntohs(client->pending_request.payload.data_count);

            if (client->pending_request.payload.data_count < TF_MODBUS_TCP_MIN_WRITE_COIL_COUNT
             || client->pending_request.payload.data_count > TF_MODBUS_TCP_MAX_WRITE_COIL_COUNT
             || client->pending_request.payload.byte_count != (client->pending_request.payload.data_count + 7) / 8) {
                exception_code = TFModbusTCPExceptionCode::IllegalDataValue;
            }
            else {
                client->response.header.frame_length   = TF_MODBUS_TCP_FRAME_IN_HEADER_LENGTH
                                                       + offsetof(TFModbusTCPResponsePayload, write_sentinel);
                client->response.payload.start_address = htons(client->pending_request.payload.start_address);
                client->response.payload.data_count    = htons(client->pending_request.payload.data_count);

                exception_code = request_callback(client->pending_request.header.unit_id,
                                                  static_cast<TFModbusTCPFunctionCode>(client->pending_request.payload.function_code),
                                                  client->pending_request.payload.start_address,
                                                  client->pending_request.payload.data_count,
                                                  client->pending_request.payload.coil_values);
            }

            break;

        case TFModbusTCPFunctionCode::WriteMultipleRegisters:
            client->pending_request.payload.start_address = ntohs(client->pending_request.payload.start_address);
            client->pending_request.payload.data_count    = ntohs(client->pending_request.payload.data_count);

            if (client->pending_request.payload.data_count < TF_MODBUS_TCP_MIN_WRITE_COIL_COUNT
             || client->pending_request.payload.data_count > TF_MODBUS_TCP_MAX_WRITE_COIL_COUNT
             || client->pending_request.payload.byte_count != client->pending_request.payload.data_count * 2) {
                exception_code = TFModbusTCPExceptionCode::IllegalDataValue;
            }
            else {
                client->response.header.frame_length   = TF_MODBUS_TCP_FRAME_IN_HEADER_LENGTH
                                                       + offsetof(TFModbusTCPResponsePayload, write_sentinel);
                client->response.payload.start_address = htons(client->pending_request.payload.start_address);
                client->response.payload.data_count    = htons(client->pending_request.payload.data_count);

                exception_code = request_callback(client->pending_request.header.unit_id,
                                                  static_cast<TFModbusTCPFunctionCode>(client->pending_request.payload.function_code),
                                                  client->pending_request.payload.start_address,
                                                  client->pending_request.payload.data_count,
                                                  client->pending_request.payload.register_values);
            }

            break;

        default:
            exception_code = TFModbusTCPExceptionCode::IllegalFunction;
            break;
        }

        client->response.payload.function_code  = client->pending_request.payload.function_code;

        if (exception_code != TFModbusTCPExceptionCode::Success) {
            client->response.header.frame_length     = TF_MODBUS_TCP_FRAME_IN_HEADER_LENGTH
                                                     + offsetof(TFModbusTCPResponsePayload, exception_sentinel);
            client->response.payload.function_code  |= 0x80;
            client->response.payload.exception_code  = static_cast<uint8_t>(exception_code);
        }

        client->response.header.transaction_id = htons(client->pending_request.header.transaction_id);
        client->response.header.protocol_id    = htons(client->pending_request.header.protocol_id);
        client->response.header.frame_length   = htons(client->response.header.frame_length);
        client->response.header.unit_id        = client->pending_request.header.unit_id;

        if (!send_response(client)) {
            int saved_errno = errno;

            tf_network_util_debugfln("TFModbusTCPClient[%p]::tick() disconnecting client due to send error (client=%p errno=%d)",
                                     static_cast<void *>(this), static_cast<void *>(client), saved_errno);

            disconnect_and_unlink(TFModbusTCPServerDisconnectReason::SocketSendFailed, saved_errno);
            continue;
        }

        client->pending_request_header_used    = 0;
        client->pending_request_header_checked = false;
        client->pending_request_payload_used   = 0;
    }

#undef disconnect_and_unlink
}

void TFModbusTCPServer::disconnect(TFModbusTCPServerClient *client, TFModbusTCPServerDisconnectReason reason, int error_number)
{
    struct sockaddr_in addr_in;
    socklen_t addr_in_length = sizeof(addr_in);
    uint32_t peer_address;
    uint16_t port;

    if (getpeername(client->socket_fd, (struct sockaddr *)&addr_in, &addr_in_length) < 0) {
        peer_address = 0;
        port         = 0;
    }
    else {
        peer_address = addr_in.sin_addr.s_addr;
        port         = ntohs(addr_in.sin_port);
    }

    shutdown(client->socket_fd, SHUT_RDWR);
    close(client->socket_fd);
    disconnect_callback(peer_address, port, reason, error_number);
    delete client;
}

bool TFModbusTCPServer::send_response(TFModbusTCPServerClient *client)
{
    uint8_t *buffer        = client->response.bytes;
    size_t length          = sizeof(client->response.header) - TF_MODBUS_TCP_FRAME_IN_HEADER_LENGTH + ntohs(client->response.header.frame_length);
    size_t buffer_send     = 0;
    size_t tries_remaining = TF_MODBUS_TCP_SERVER_MAX_SEND_TRIES;

    while (tries_remaining > 0 && buffer_send < length) {
        --tries_remaining;

        ssize_t result = send(client->socket_fd, buffer + buffer_send, length - buffer_send, 0);

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
