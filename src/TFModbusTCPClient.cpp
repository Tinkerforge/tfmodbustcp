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

#include "TFModbusTCPClient.h"

#include <algorithm>
#include <errno.h>
#include <stddef.h>
#include <lwip/sockets.h>

union TFModbusTCPClientRegisterRequestPayload
{
    struct [[gnu::packed]] {
        uint8_t function_code;
        uint16_t start_address;
        uint16_t register_count;
    };
    uint8_t bytes[5];
};

static_assert(TF_MODBUS_TCP_CLIENT_MIN_RECONNECT_DELAY > 0, "TF_MODBUS_TCP_CLIENT_MIN_RECONNECT_DELAY must be positive");
static_assert(TF_MODBUS_TCP_CLIENT_MIN_RECONNECT_DELAY <= TF_MODBUS_TCP_CLIENT_MAX_RECONNECT_DELAY, "TF_MODBUS_TCP_CLIENT_MIN_RECONNECT_DELAY must not be bigger than TF_MODBUS_TCP_CLIENT_MAX_RECONNECT_DELAY");

static_assert(sizeof(TFModbusTCPClientHeader) == TF_MODBUS_TCP_CLIENT_HEADER_LENGTH, "TFModbusTCPClientHeader has unexpected size");
static_assert(offsetof(TFModbusTCPClientHeader, transaction_id) == 0, "TFModbusTCPClientHeader::transaction_id has unexpected offset");
static_assert(offsetof(TFModbusTCPClientHeader, protocol_id) == 2, "TFModbusTCPClientHeader::protocol_id has unexpected offset");
static_assert(offsetof(TFModbusTCPClientHeader, frame_length) == 4, "TFModbusTCPClientHeader::frame_length has unexpected offset");
static_assert(offsetof(TFModbusTCPClientHeader, unit_id) == 6, "TFModbusTCPClientHeader::unit_id has unexpected offset");

static_assert(sizeof(TFModbusTCPClientRegisterRequestPayload) == 5, "TFModbusTCPClientRegisterRequestPayload has unexpected size");
static_assert(offsetof(TFModbusTCPClientRegisterRequestPayload, function_code) == 0, "TFModbusTCPClientRegisterRequestPayload::function_code has unexpected offset");
static_assert(offsetof(TFModbusTCPClientRegisterRequestPayload, start_address) == 1, "TFModbusTCPClientRegisterRequestPayload::start_address has unexpected offset");
static_assert(offsetof(TFModbusTCPClientRegisterRequestPayload, register_count) == 3, "TFModbusTCPClientRegisterRequestPayload::register_count has unexpected offset");
static_assert(offsetof(TFModbusTCPClientRegisterRequestPayload, bytes) == 0, "TFModbusTCPClientRegisterRequestPayload::header has unexpected offset");

static_assert(sizeof(TFModbusTCPClientRegisterResponsePayload) == TF_MODBUS_TCP_CLIENT_MAX_PAYLOAD_LENGTH, "TFModbusTCPClientRegisterResponsePayload has unexpected size");
static_assert(offsetof(TFModbusTCPClientRegisterResponsePayload, function_code) == 0, "TFModbusTCPClientRegisterResponsePayload::function_code has unexpected offset");
static_assert(offsetof(TFModbusTCPClientRegisterResponsePayload, exception_code) == 1, "TFModbusTCPClientRegisterResponsePayload::exception_code has unexpected offset");
static_assert(offsetof(TFModbusTCPClientRegisterResponsePayload, byte_count) == 1, "TFModbusTCPClientRegisterResponsePayload::byte_count has unexpected offset");
static_assert(offsetof(TFModbusTCPClientRegisterResponsePayload, register_values) == 2, "TFModbusTCPClientRegisterResponsePayload::register_values has unexpected offset");
static_assert(offsetof(TFModbusTCPClientRegisterResponsePayload, bytes) == 0, "TFModbusTCPClientRegisterResponsePayload::bytes has unexpected offset");

static std::function<void(String host_name, std::function<void(IPAddress host_address, int error_number)> &&callback)> resolve_callback;

static inline uint16_t swap_16(uint16_t value)
{
    return (value >> 8) | (value << 8);
}

static bool a_after_b(uint32_t a, uint32_t b)
{
    return ((uint32_t)(a - b)) < (UINT32_MAX / 2);
}

static bool deadline_elapsed(uint32_t deadline)
{
    return a_after_b(millis(), deadline);
}

static uint32_t calculate_deadline(uint32_t delay)
{
    uint32_t deadline = millis() + delay;

    if (deadline == 0) {
        deadline = 1;
    }

    return deadline;
}

const char *get_tf_modbus_tcp_client_transaction_result_name(TFModbusTCPClientTransactionResult result)
{
    switch (result) {
    case TFModbusTCPClientTransactionResult::Success:
        return "Success";

    case TFModbusTCPClientTransactionResult::ModbusIllegalFunction:
        return "ModbusIllegalFunction";

    case TFModbusTCPClientTransactionResult::ModbusIllegalDataAddress:
        return "ModbusIllegalDataAddress";

    case TFModbusTCPClientTransactionResult::ModbusIllegalDataValue:
        return "ModbusIllegalDataValue";

    case TFModbusTCPClientTransactionResult::ModbusServerDeviceFailure:
        return "ModbusServerDeviceFailure";

    case TFModbusTCPClientTransactionResult::ModbusAcknowledge:
        return "ModbusAcknowledge";

    case TFModbusTCPClientTransactionResult::ModbusServerDeviceBusy:
        return "ModbusServerDeviceBusy";

    case TFModbusTCPClientTransactionResult::ModbusMemoryParityError:
        return "ModbusMemoryParityError";

    case TFModbusTCPClientTransactionResult::ModbusGatewayPathUnvailable:
        return "ModbusGatewayPathUnvailable";

    case TFModbusTCPClientTransactionResult::ModbusGatewayTargetDeviceFailedToRespond:
        return "ModbusGatewayTargetDeviceFailedToRespond";

    case TFModbusTCPClientTransactionResult::InvalidArgument:
        return "InvalidArgument";

    case TFModbusTCPClientTransactionResult::AbortedByDisconnect:
        return "AbortedByDisconnect";

    case TFModbusTCPClientTransactionResult::AbortedByOtherError:
        return "AbortedByOtherError";

    case TFModbusTCPClientTransactionResult::AllTransactionsPending:
        return "AllTransactionsPending";

    case TFModbusTCPClientTransactionResult::NotConnected:
        return "NotConnected";

    case TFModbusTCPClientTransactionResult::DisconnectedByPeer:
        return "DisconnectedByPeer";

    case TFModbusTCPClientTransactionResult::RequestSendFailed:
        return "RequestSendFailed";

    case TFModbusTCPClientTransactionResult::RequestTimeout:
        return "RequestTimeout";

    case TFModbusTCPClientTransactionResult::ResponseReceiveFailed:
        return "ResponseReceiveFailed";

    case TFModbusTCPClientTransactionResult::ResponseTimeout:
        return "ResponseTimeout";

    case TFModbusTCPClientTransactionResult::ResponseShorterThanMinimum:
        return "ResponseShorterThanMinimum";

    case TFModbusTCPClientTransactionResult::ResponseLongerThanMaximum:
        return "ResponseLongerThanMaximum";

    case TFModbusTCPClientTransactionResult::ResponseUnitIDMismatch:
        return "ResponseUnitIDMismatch";

    case TFModbusTCPClientTransactionResult::ResponseFunctionCodeMismatch:
        return "ResponseFunctionCodeMismatch";

    case TFModbusTCPClientTransactionResult::ResponseByteCountInvalid:
        return "ResponseByteCountInvalid";

    case TFModbusTCPClientTransactionResult::ResponseRegisterCountMismatch:
        return "ResponseRegisterCountMismatch";

    case TFModbusTCPClientTransactionResult::ResponseTooShort:
        return "ResponseTooShort";
    }

    return "Unknown";
}

const char *get_tf_modbus_tcp_client_connection_status_name(TFModbusTCPClientConnectionStatus status)
{
    switch (status) {
    case TFModbusTCPClientConnectionStatus::InvalidArgument:
        return "InvalidArgument";

    case TFModbusTCPClientConnectionStatus::ConcurrentConnect:
        return "ConcurrentConnect";

    case TFModbusTCPClientConnectionStatus::NoResolveCallback:
        return "NoResolveCallback";

    case TFModbusTCPClientConnectionStatus::ResolveInProgress:
        return "ResolveInProgress";

    case TFModbusTCPClientConnectionStatus::ResolveFailed:
        return "ResolveFailed";

    case TFModbusTCPClientConnectionStatus::Resolved:
        return "Resolved";

    case TFModbusTCPClientConnectionStatus::SocketCreateFailed:
        return "SocketCreateFailed";

    case TFModbusTCPClientConnectionStatus::SocketGetFlagsFailed:
        return "SocketGetFlagsFailed";

    case TFModbusTCPClientConnectionStatus::SocketSetFlagsFailed:
        return "SocketSetFlagsFailed";

    case TFModbusTCPClientConnectionStatus::SocketConnectFailed:
        return "SocketConnectFailed";

    case TFModbusTCPClientConnectionStatus::SocketSelectFailed:
        return "SocketSelectFailed";

    case TFModbusTCPClientConnectionStatus::SocketGetOptionFailed:
        return "SocketGetOptionFailed";

    case TFModbusTCPClientConnectionStatus::SocketConnectAsyncFailed:
        return "SocketConnectAsyncFailed";

    case TFModbusTCPClientConnectionStatus::SocketReceiveFailed:
        return "SocketReceiveFailed";

    case TFModbusTCPClientConnectionStatus::SocketIoctlFailed:
        return "SocketIoctlFailed";

    case TFModbusTCPClientConnectionStatus::SocketSendFailed:
        return "SocketSendFailed";

    case TFModbusTCPClientConnectionStatus::ConnectInProgress:
        return "ConnectInProgress";

    case TFModbusTCPClientConnectionStatus::ConnectTimeout:
        return "ConnectTimeout";

    case TFModbusTCPClientConnectionStatus::Connected:
        return "Connected";

    case TFModbusTCPClientConnectionStatus::Disconnected:
        return "Disconnected";

    case TFModbusTCPClientConnectionStatus::DisconnectedByPeer:
        return "DisconnectedByPeer";

    case TFModbusTCPClientConnectionStatus::ModbusProtocolError:
        return "ModbusProtocolError";
    }

    return "Unknown";
}

void set_tf_modbus_tcp_client_resolve_callback(std::function<void(String host_name, std::function<void(IPAddress host_address, int error_number)> &&callback)> &&callback)
{
    if (callback) {
        resolve_callback = callback;
    }
}

void TFModbusTCPClient::connect(String host_name, uint16_t port, std::function<void(TFModbusTCPClientConnectionStatus status, int error_number)> &&callback)
{
    if (host_name.length() == 0 || port == 0) {
        callback(TFModbusTCPClientConnectionStatus::InvalidArgument, 0);
        return;
    }

    if (!resolve_callback) {
        callback(TFModbusTCPClientConnectionStatus::NoResolveCallback, 0);
        return;
    }

    uint32_t current_connect_id = ++connect_id;

    disconnect();

    if (current_connect_id != connect_id) {
        // connect() was called from status or transaction callback from inside disconnect()
        callback(TFModbusTCPClientConnectionStatus::ConcurrentConnect, 0);
        return;
    }

    this->host_name = host_name;
    this->port = port;
    status_callback = callback;
    reconnect_deadline = 0;
    reconnect_delay = 0;

    reset_pending_response();
}

void TFModbusTCPClient::disconnect()
{
    if (host_name.length() == 0) {
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

    std::function<void(TFModbusTCPClientConnectionStatus status, int error_number)> callback = std::move(status_callback);

    host_name = "";
    port = 0;
    status_callback = nullptr;
    reconnect_deadline = 0;
    reconnect_delay = 0;

    reset_pending_response();
    finish_all_transactions(TFModbusTCPClientTransactionResult::AbortedByDisconnect);

    if (host_name.length() > 0) {
        return; // connect() was called from transaction callback
    }

    callback(TFModbusTCPClientConnectionStatus::Disconnected, 0);
}

void TFModbusTCPClient::tick()
{
    check_transaction_timeout();

    if (host_name.length() > 0 && socket_fd < 0) {
        if (reconnect_deadline != 0 && !deadline_elapsed(reconnect_deadline)) {
            return;
        }

        reconnect_deadline = 0;

        if (!resolve_pending && pending_host_address == IPAddress() && pending_socket_fd < 0) {
            status_callback(TFModbusTCPClientConnectionStatus::ResolveInProgress, 0);

            if (host_name.length() == 0) {
                return; // disconnect() was called from status callback
            }

            resolve_pending = true;
            uint32_t current_resolve_id = ++resolve_id;

            resolve_callback(host_name, [this, current_resolve_id](IPAddress host_address, int error_number) {
                if (!resolve_pending || current_resolve_id != resolve_id) {
                    return;
                }

                if (host_address == IPAddress()) {
                    abort_connection(TFModbusTCPClientConnectionStatus::ResolveFailed, error_number);
                    return;
                }

                resolve_pending = false;
                pending_host_address = host_address;

                status_callback(TFModbusTCPClientConnectionStatus::Resolved, 0);
            });
        }

        if (pending_socket_fd < 0) {
            if (pending_host_address == IPAddress()) {
                return; // Waiting for resolve callback
            }

            status_callback(TFModbusTCPClientConnectionStatus::ConnectInProgress, 0);

            if (host_name.length() == 0) {
                return; // disconnect() was called from status callback
            }

            pending_socket_fd = socket(AF_INET, SOCK_STREAM, 0);

            if (pending_socket_fd < 0) {
                abort_connection(TFModbusTCPClientConnectionStatus::SocketCreateFailed, errno);
                return;
            }

            int flags = fcntl(pending_socket_fd, F_GETFL, 0);

            if (flags < 0) {
                abort_connection(TFModbusTCPClientConnectionStatus::SocketGetFlagsFailed, errno);
                return;
            }

            if (fcntl(pending_socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                abort_connection(TFModbusTCPClientConnectionStatus::SocketSetFlagsFailed, errno);
                return;
            }

            uint32_t ip_addr = pending_host_address;
            struct sockaddr_in addr_in;

            pending_host_address = IPAddress();

            memset(&addr_in, 0, sizeof(addr_in));
            addr_in.sin_family = AF_INET;

            memcpy(&addr_in.sin_addr.s_addr, &ip_addr, sizeof(ip_addr));
            addr_in.sin_port = htons(port);

            if (::connect(pending_socket_fd, (struct sockaddr *)&addr_in, sizeof(addr_in)) < 0 && errno != EINPROGRESS) {
                abort_connection(TFModbusTCPClientConnectionStatus::SocketConnectFailed, errno);
                return;
            }

            connect_timeout_deadline = calculate_deadline(TF_MODBUS_TCP_CLIENT_CONNECT_TIMEOUT);
        }

        if (deadline_elapsed(connect_timeout_deadline)) {
            abort_connection(TFModbusTCPClientConnectionStatus::ConnectTimeout, 0);
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
            abort_connection(TFModbusTCPClientConnectionStatus::SocketSelectFailed, errno);
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
            abort_connection(TFModbusTCPClientConnectionStatus::SocketGetOptionFailed, errno);
            return;
        }

        if (socket_errno != 0) {
            abort_connection(TFModbusTCPClientConnectionStatus::SocketConnectAsyncFailed, socket_errno);
            return;
        }

        reconnect_delay = 0;
        socket_fd = pending_socket_fd;
        pending_socket_fd = -1;
        status_callback(TFModbusTCPClientConnectionStatus::Connected, 0);
    }

    uint32_t tick_start = millis();
    bool first = true;

    while (socket_fd >= 0 && (deadline_elapsed(tick_start + TF_MODBUS_TCP_CLIENT_MAX_TICK_DURATION) || first)) {
        first = false;

        check_transaction_timeout();

        size_t pending_header_missing = sizeof(pending_header) - pending_header_used;

        if (pending_header_missing > 0) {
            ssize_t result = recv(socket_fd, pending_header.bytes + pending_header_used, pending_header_missing, 0);

            if (result < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    abort_connection(TFModbusTCPClientConnectionStatus::SocketReceiveFailed, errno);
                }

                return;
            }

            if (result == 0) {
                abort_connection(TFModbusTCPClientConnectionStatus::DisconnectedByPeer, 0);
                return;
            }

            pending_header_used += result;
            continue;
        }

        if (!pending_header_checked) {
            pending_header.transaction_id = swap_16(pending_header.transaction_id);
            pending_header.protocol_id = swap_16(pending_header.protocol_id);
            pending_header.frame_length = swap_16(pending_header.frame_length);

            if (pending_header.protocol_id != 0) {
                abort_connection(TFModbusTCPClientConnectionStatus::ModbusProtocolError, 0);
                return;
            }

            if (pending_header.frame_length < TF_MODBUS_TCP_CLIENT_MIN_FRAME_LENGTH) {
                finish_transaction(pending_header.transaction_id, TFModbusTCPClientTransactionResult::ResponseShorterThanMinimum);
                abort_connection(TFModbusTCPClientConnectionStatus::ModbusProtocolError, 0);
                return;
            }

            if (pending_header.frame_length > TF_MODBUS_TCP_CLIENT_MAX_FRAME_LENGTH) {
                finish_transaction(pending_header.transaction_id, TFModbusTCPClientTransactionResult::ResponseLongerThanMaximum);
                abort_connection(TFModbusTCPClientConnectionStatus::ModbusProtocolError, 0);
                return;
            }

            pending_header_checked = true;
        }

        size_t pending_payload_missing = pending_header.frame_length - TF_MODBUS_TCP_CLIENT_FRAME_IN_HEADER_LENGTH - pending_payload_used;

        if (pending_payload_missing > 0) {
            if (receive_payload(pending_payload_missing) <= 0) {
                return;
            }

            continue;
        }

        // Check if data is remaining after indicated frame length has been read.
        // A full header and longer can be another Modbus response. Anything shorter
        // than a full header is either garbage or the header indicated fewer bytes
        // than were actually present. If there is a possible trailing fragment
        // append it to the payload.
        int readable;

        if (ioctl(socket_fd, FIONREAD, &readable) < 0) {
            int saved_errno = errno;
            finish_transaction(pending_header.transaction_id, TFModbusTCPClientTransactionResult::ResponseReceiveFailed);
            abort_connection(TFModbusTCPClientConnectionStatus::SocketIoctlFailed, saved_errno);
            return;
        }

        if (readable > 0 && static_cast<size_t>(readable) < sizeof(TFModbusTCPClientHeader) && pending_payload_used + readable <= TF_MODBUS_TCP_CLIENT_MAX_PAYLOAD_LENGTH) {
            ssize_t result = receive_payload(readable);

            if (result <= 0) {
                return;
            }

            pending_header.frame_length += result;
        }

        TFModbusTCPClientTransaction *transaction = take_transaction(pending_header.transaction_id);

        if (transaction == nullptr) {
            reset_pending_response();
            continue;
        }

        if (transaction->unit_id != pending_header.unit_id) {
            finish_transaction(transaction, TFModbusTCPClientTransactionResult::ResponseUnitIDMismatch);
            reset_pending_response();
            continue;
        }

        if (pending_payload.function_code != (pending_payload.function_code & 0x7f)) {
            finish_transaction(transaction, TFModbusTCPClientTransactionResult::ResponseFunctionCodeMismatch);
            reset_pending_response();
            continue;
        }

        if ((pending_payload.function_code & 0x80) != 0) {
            finish_transaction(transaction, static_cast<TFModbusTCPClientTransactionResult>(pending_payload.exception_code));
            reset_pending_response();
            continue;
        }

        if ((pending_payload.byte_count & 1) != 0) {
            finish_transaction(transaction, TFModbusTCPClientTransactionResult::ResponseByteCountInvalid);
            reset_pending_response();
            continue;
        }

        uint8_t register_count = pending_payload.byte_count / 2;

        if (transaction->register_count != register_count) {
            finish_transaction(transaction, TFModbusTCPClientTransactionResult::ResponseRegisterCountMismatch);
            reset_pending_response();
            continue;
        }

        if (pending_payload_used < 2 + register_count * 2u) {
            // Intentionally accept too long responses
            finish_transaction(transaction, TFModbusTCPClientTransactionResult::ResponseTooShort);
            reset_pending_response();
            continue;
        }

        if (transaction->buffer != nullptr) {
            uint16_t *src = pending_payload.register_values;
            uint16_t *dst = transaction->buffer;
            uint8_t register_remaining = register_count;

            while (register_remaining-- > 0) {
                *dst++ = swap_16(*src++);
            }
        }

        finish_transaction(transaction, TFModbusTCPClientTransactionResult::Success);
        reset_pending_response();
    }
}

void TFModbusTCPClient::read_register(TFModbusTCPClientRegisterType register_type,
                                      uint8_t unit_id,
                                      uint16_t start_address,
                                      uint16_t register_count,
                                      uint16_t *buffer,
                                      std::function<void(TFModbusTCPClientTransactionResult result)> &&callback)
{
    uint8_t function_code;

    switch (register_type) {
    case TFModbusTCPClientRegisterType::HoldingRegister:
        function_code = 3;
        break;

    case TFModbusTCPClientRegisterType::InputRegister:
        function_code = 4;
        break;

    default:
        callback(TFModbusTCPClientTransactionResult::InvalidArgument);
        return;
    }

    if (register_count < TF_MODBUS_TCP_CLIENT_MIN_REGISTER_COUNT || register_count > TF_MODBUS_TCP_CLIENT_MAX_REGISTER_COUNT) {
        callback(TFModbusTCPClientTransactionResult::InvalidArgument);
        return;
    }

    if (socket_fd < 0) {
        callback(TFModbusTCPClientTransactionResult::NotConnected);
        return;
    }

    ssize_t transaction_index = -1;

    for (ssize_t i = 0; i < TF_MODBUS_TCP_CLIENT_MAX_TRANSACTION_COUNT; ++i) {
        if (transactions[i] == nullptr) {
            transaction_index = i;
            break;
        }
    }

    if (transaction_index < 0) {
        callback(TFModbusTCPClientTransactionResult::AllTransactionsPending);
        return;
    }

    TFModbusTCPClientHeader header;
    TFModbusTCPClientRegisterRequestPayload payload;
    uint16_t transaction_id = next_transaction_id++;

    header.transaction_id = swap_16(transaction_id);
    header.protocol_id = swap_16(0);
    header.frame_length = swap_16(6);
    header.unit_id = unit_id;

    payload.function_code = function_code;
    payload.start_address = swap_16(start_address);
    payload.register_count = swap_16(register_count);

    uint8_t request[sizeof(header) + sizeof(payload)];

    memcpy(request, header.bytes, sizeof(header));
    memcpy(request + sizeof(header), payload.bytes, sizeof(payload));

    uint32_t send_start = millis();
    size_t request_length = sizeof(header) + sizeof(payload);
    size_t request_send = 0;

    while (request_send < request_length) {
        uint32_t duration = millis() - send_start;
        uint32_t timeout = TF_MODBUS_TCP_CLIENT_REQUEST_TIMEOUT - duration;

        if (deadline_elapsed(send_start + TF_MODBUS_TCP_CLIENT_REQUEST_TIMEOUT)) {
            callback(TFModbusTCPClientTransactionResult::RequestTimeout);
            return;
        }

        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(socket_fd, &fdset);

        struct timeval tv;
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;

        int select_result = select(socket_fd + 1, nullptr, &fdset, nullptr, &tv);

        if (select_result < 0) {
            int saved_errno = errno;
            callback(TFModbusTCPClientTransactionResult::RequestSendFailed);
            abort_connection(TFModbusTCPClientConnectionStatus::SocketSelectFailed, saved_errno);
            return;
        }

        if (select_result == 0) {
            continue; // Not ready to send
        }

        if (!FD_ISSET(socket_fd, &fdset)) {
            continue; // Not ready to send
        }

        ssize_t send_result = send(socket_fd, request + request_send, request_length - request_send, 0);

        if (send_result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }

            int saved_errno = errno;
            callback(TFModbusTCPClientTransactionResult::RequestSendFailed);
            abort_connection(TFModbusTCPClientConnectionStatus::SocketSendFailed, saved_errno);
            return;
        }

        request_send += send_result;
    }

    TFModbusTCPClientTransaction *transaction = new TFModbusTCPClientTransaction;

    transaction->transaction_id = transaction_id;
    transaction->unit_id = unit_id;
    transaction->function_code = function_code;
    transaction->register_count = register_count;
    transaction->buffer = buffer;
    transaction->request_sent = millis();
    transaction->callback = callback;

    transactions[transaction_index] = transaction;
}

ssize_t TFModbusTCPClient::receive_payload(size_t length)
{
    ssize_t result = recv(socket_fd, pending_payload.bytes + pending_payload_used, length, 0);

    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }

        int saved_errno = errno;
        finish_transaction(pending_header.transaction_id, TFModbusTCPClientTransactionResult::ResponseReceiveFailed);
        abort_connection(TFModbusTCPClientConnectionStatus::SocketReceiveFailed, saved_errno);
        return -1;
    }

    if (result == 0) {
        finish_transaction(pending_header.transaction_id, TFModbusTCPClientTransactionResult::DisconnectedByPeer);
        abort_connection(TFModbusTCPClientConnectionStatus::DisconnectedByPeer, 0);
        return -1;
    }

    pending_payload_used += result;
    return result;
}

TFModbusTCPClientTransaction *TFModbusTCPClient::take_transaction(uint16_t transaction_id)
{
    for (ssize_t i = 0; i < TF_MODBUS_TCP_CLIENT_MAX_TRANSACTION_COUNT; ++i) {
        TFModbusTCPClientTransaction *transaction = transactions[i];

        if (transaction != nullptr && transaction->transaction_id == transaction_id) {
            transactions[i] = nullptr;
            return transaction;
        }
    }

    return nullptr;
}

void TFModbusTCPClient::finish_transaction(uint16_t transaction_id, TFModbusTCPClientTransactionResult result)
{
    TFModbusTCPClientTransaction *transaction = take_transaction(transaction_id);

    if (transaction != nullptr) {
        finish_transaction(transaction, result);
    }
}

void TFModbusTCPClient::finish_transaction(TFModbusTCPClientTransaction *transaction, TFModbusTCPClientTransactionResult result)
{
    transaction->callback(result);
    delete transaction;
}

void TFModbusTCPClient::finish_all_transactions(TFModbusTCPClientTransactionResult result)
{
    for (ssize_t i = 0; i < TF_MODBUS_TCP_CLIENT_MAX_TRANSACTION_COUNT; ++i) {
        TFModbusTCPClientTransaction *transaction = transactions[i];

        if (transactions[i] != nullptr) {
            transactions[i] = nullptr;
            finish_transaction(transaction, result);
        }
    }
}

void TFModbusTCPClient::check_transaction_timeout()
{
    for (ssize_t i = 0; i < TF_MODBUS_TCP_CLIENT_MAX_TRANSACTION_COUNT; ++i) {
        TFModbusTCPClientTransaction *transaction = transactions[i];

        if (transaction == nullptr) {
            continue;
        }

        if (deadline_elapsed(transaction->request_sent + TF_MODBUS_TCP_CLIENT_RESPONSE_TIMEOUT)) {
            transactions[i] = nullptr;
            finish_transaction(transaction, TFModbusTCPClientTransactionResult::ResponseTimeout);
        }
    }
}

void TFModbusTCPClient::reset_pending_response()
{
    pending_header_used = 0;
    pending_header_checked = false;
    pending_payload_used = 0;
}

void TFModbusTCPClient::abort_connection(TFModbusTCPClientConnectionStatus status, int error_number)
{
    if (reconnect_delay == 0) {
        reconnect_delay = TF_MODBUS_TCP_CLIENT_MIN_RECONNECT_DELAY;
    }
    else {
        reconnect_delay *= 2;

        if (reconnect_delay > TF_MODBUS_TCP_CLIENT_MAX_RECONNECT_DELAY) {
            reconnect_delay = TF_MODBUS_TCP_CLIENT_MAX_RECONNECT_DELAY;
        }
    }

    reconnect_deadline = calculate_deadline(reconnect_delay);
    resolve_pending = false;
    pending_host_address = IPAddress();

    if (pending_socket_fd >= 0) {
        close(pending_socket_fd);
        pending_socket_fd = -1;
    }

    if (socket_fd >= 0) {
        close(socket_fd);
        socket_fd = -1;
    }

    reset_pending_response();
    finish_all_transactions(TFModbusTCPClientTransactionResult::AbortedByOtherError);
    status_callback(status, error_number);
}
