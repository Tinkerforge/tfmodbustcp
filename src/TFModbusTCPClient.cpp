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

#include <errno.h>
#include <stddef.h>
#include <lwip/sockets.h>

#include "TFNetworkUtil.h"

union TFModbusTCPClientRegisterRequestPayload
{
    struct [[gnu::packed]] {
        uint8_t function_code;
        uint16_t start_address;
        uint16_t register_count;
    };
    uint8_t bytes[5];
};

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

static inline uint16_t swap_16(uint16_t value)
{
    return (value >> 8) | (value << 8);
}

const char *get_tf_modbus_tcp_client_result_name(TFModbusTCPClientResult result)
{
    switch (result) {
    case TFModbusTCPClientResult::Success:
        return "Success";

    case TFModbusTCPClientResult::ModbusIllegalFunction:
        return "ModbusIllegalFunction";

    case TFModbusTCPClientResult::ModbusIllegalDataAddress:
        return "ModbusIllegalDataAddress";

    case TFModbusTCPClientResult::ModbusIllegalDataValue:
        return "ModbusIllegalDataValue";

    case TFModbusTCPClientResult::ModbusServerDeviceFailure:
        return "ModbusServerDeviceFailure";

    case TFModbusTCPClientResult::ModbusAcknowledge:
        return "ModbusAcknowledge";

    case TFModbusTCPClientResult::ModbusServerDeviceBusy:
        return "ModbusServerDeviceBusy";

    case TFModbusTCPClientResult::ModbusMemoryParityError:
        return "ModbusMemoryParityError";

    case TFModbusTCPClientResult::ModbusGatewayPathUnvailable:
        return "ModbusGatewayPathUnvailable";

    case TFModbusTCPClientResult::ModbusGatewayTargetDeviceFailedToRespond:
        return "ModbusGatewayTargetDeviceFailedToRespond";

    case TFModbusTCPClientResult::InvalidArgument:
        return "InvalidArgument";

    case TFModbusTCPClientResult::AbortedByDisconnect:
        return "AbortedByDisconnect";

    case TFModbusTCPClientResult::AbortedByOtherError:
        return "AbortedByOtherError";

    case TFModbusTCPClientResult::AllTransactionsPending:
        return "AllTransactionsPending";

    case TFModbusTCPClientResult::NotConnected:
        return "NotConnected";

    case TFModbusTCPClientResult::DisconnectedByPeer:
        return "DisconnectedByPeer";

    case TFModbusTCPClientResult::RequestSendFailed:
        return "RequestSendFailed";

    case TFModbusTCPClientResult::RequestTimeout:
        return "RequestTimeout";

    case TFModbusTCPClientResult::ResponseReceiveFailed:
        return "ResponseReceiveFailed";

    case TFModbusTCPClientResult::ResponseTimeout:
        return "ResponseTimeout";

    case TFModbusTCPClientResult::ResponseShorterThanMinimum:
        return "ResponseShorterThanMinimum";

    case TFModbusTCPClientResult::ResponseLongerThanMaximum:
        return "ResponseLongerThanMaximum";

    case TFModbusTCPClientResult::ResponseUnitIDMismatch:
        return "ResponseUnitIDMismatch";

    case TFModbusTCPClientResult::ResponseFunctionCodeMismatch:
        return "ResponseFunctionCodeMismatch";

    case TFModbusTCPClientResult::ResponseByteCountInvalid:
        return "ResponseByteCountInvalid";

    case TFModbusTCPClientResult::ResponseRegisterCountMismatch:
        return "ResponseRegisterCountMismatch";

    case TFModbusTCPClientResult::ResponseTooShort:
        return "ResponseTooShort";
    }

    return "Unknown";
}

void TFModbusTCPClient::read_register(TFModbusTCPClientRegisterType register_type,
                                      uint8_t unit_id,
                                      uint16_t start_address,
                                      uint16_t register_count,
                                      uint16_t *buffer,
                                      std::function<void(TFModbusTCPClientResult result)> &&callback)
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
        callback(TFModbusTCPClientResult::InvalidArgument);
        return;
    }

    if (register_count < TF_MODBUS_TCP_CLIENT_MIN_REGISTER_COUNT || register_count > TF_MODBUS_TCP_CLIENT_MAX_REGISTER_COUNT) {
        callback(TFModbusTCPClientResult::InvalidArgument);
        return;
    }

    if (socket_fd < 0) {
        callback(TFModbusTCPClientResult::NotConnected);
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
        callback(TFModbusTCPClientResult::AllTransactionsPending);
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

    uint32_t send_start = TFNetworkUtil::milliseconds();
    size_t request_length = sizeof(header) + sizeof(payload);
    size_t request_send = 0;

    while (request_send < request_length) {
        uint32_t duration = TFNetworkUtil::milliseconds() - send_start;
        uint32_t timeout = TF_MODBUS_TCP_CLIENT_REQUEST_TIMEOUT - duration;

        if (TFNetworkUtil::deadline_elapsed(send_start + TF_MODBUS_TCP_CLIENT_REQUEST_TIMEOUT)) {
            callback(TFModbusTCPClientResult::RequestTimeout);
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
            callback(TFModbusTCPClientResult::RequestSendFailed);
            abort_connection(TFGenericTCPClientEvent::SocketSelectFailed, saved_errno);
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
            callback(TFModbusTCPClientResult::RequestSendFailed);
            abort_connection(TFGenericTCPClientEvent::SocketSendFailed, saved_errno);
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
    transaction->request_sent = TFNetworkUtil::milliseconds();
    transaction->callback = callback;

    transactions[transaction_index] = transaction;
}

void TFModbusTCPClient::disconnect_hook()
{
    reset_pending_response();
    finish_all_transactions(TFModbusTCPClientResult::AbortedByDisconnect);
}

void TFModbusTCPClient::tick_hook()
{
    check_transaction_timeout();
}

bool TFModbusTCPClient::receive_hook()
{
    check_transaction_timeout();

    size_t pending_header_missing = sizeof(pending_header) - pending_header_used;

    if (pending_header_missing > 0) {
        ssize_t result = recv(socket_fd, pending_header.bytes + pending_header_used, pending_header_missing, 0);

        if (result < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                abort_connection(TFGenericTCPClientEvent::SocketReceiveFailed, errno);
            }

            return false;
        }

        if (result == 0) {
            abort_connection(TFGenericTCPClientEvent::DisconnectedByPeer, 0);
            return false;
        }

        pending_header_used += result;
        return true;
    }

    if (!pending_header_checked) {
        pending_header.transaction_id = swap_16(pending_header.transaction_id);
        pending_header.protocol_id = swap_16(pending_header.protocol_id);
        pending_header.frame_length = swap_16(pending_header.frame_length);

        if (pending_header.protocol_id != 0) {
            abort_connection(TFGenericTCPClientEvent::ProtocolError, 0);
            return false;
        }

        if (pending_header.frame_length < TF_MODBUS_TCP_CLIENT_MIN_FRAME_LENGTH) {
            finish_transaction(pending_header.transaction_id, TFModbusTCPClientResult::ResponseShorterThanMinimum);
            abort_connection(TFGenericTCPClientEvent::ProtocolError, 0);
            return false;
        }

        if (pending_header.frame_length > TF_MODBUS_TCP_CLIENT_MAX_FRAME_LENGTH) {
            finish_transaction(pending_header.transaction_id, TFModbusTCPClientResult::ResponseLongerThanMaximum);
            abort_connection(TFGenericTCPClientEvent::ProtocolError, 0);
            return false;
        }

        pending_header_checked = true;
    }

    size_t pending_payload_missing = pending_header.frame_length - TF_MODBUS_TCP_CLIENT_FRAME_IN_HEADER_LENGTH - pending_payload_used;

    if (pending_payload_missing > 0) {
        if (receive_payload(pending_payload_missing) <= 0) {
            return false;
        }

        return true;
    }

    // Check if data is remaining after indicated frame length has been read.
    // A full header and longer can be another Modbus response. Anything shorter
    // than a full header is either garbage or the header indicated fewer bytes
    // than were actually present. If there is a possible trailing fragment
    // append it to the payload.
    int readable;

    if (ioctl(socket_fd, FIONREAD, &readable) < 0) {
        int saved_errno = errno;
        finish_transaction(pending_header.transaction_id, TFModbusTCPClientResult::ResponseReceiveFailed);
        abort_connection(TFGenericTCPClientEvent::SocketIoctlFailed, saved_errno);
        return false;
    }

    if (readable > 0 && static_cast<size_t>(readable) < sizeof(TFModbusTCPClientHeader) && pending_payload_used + readable <= TF_MODBUS_TCP_CLIENT_MAX_PAYLOAD_LENGTH) {
        ssize_t result = receive_payload(readable);

        if (result <= 0) {
            return false;
        }

        pending_header.frame_length += result;
    }

    TFModbusTCPClientTransaction *transaction = take_transaction(pending_header.transaction_id);

    if (transaction == nullptr) {
        reset_pending_response();
        return true;
    }

    if (transaction->unit_id != pending_header.unit_id) {
        finish_transaction(transaction, TFModbusTCPClientResult::ResponseUnitIDMismatch);
        reset_pending_response();
        return true;
    }

    if (pending_payload.function_code != (pending_payload.function_code & 0x7f)) {
        finish_transaction(transaction, TFModbusTCPClientResult::ResponseFunctionCodeMismatch);
        reset_pending_response();
        return true;
    }

    if ((pending_payload.function_code & 0x80) != 0) {
        finish_transaction(transaction, static_cast<TFModbusTCPClientResult>(pending_payload.exception_code));
        reset_pending_response();
        return true;
    }

    if ((pending_payload.byte_count & 1) != 0) {
        finish_transaction(transaction, TFModbusTCPClientResult::ResponseByteCountInvalid);
        reset_pending_response();
        return true;
    }

    uint8_t register_count = pending_payload.byte_count / 2;

    if (transaction->register_count != register_count) {
        finish_transaction(transaction, TFModbusTCPClientResult::ResponseRegisterCountMismatch);
        reset_pending_response();
        return true;
    }

    if (pending_payload_used < 2 + register_count * 2u) {
        // Intentionally accept too long responses
        finish_transaction(transaction, TFModbusTCPClientResult::ResponseTooShort);
        reset_pending_response();
        return true;
    }

    if (transaction->buffer != nullptr) {
        uint16_t *src = pending_payload.register_values;
        uint16_t *dst = transaction->buffer;
        uint8_t register_remaining = register_count;

        while (register_remaining-- > 0) {
            *dst++ = swap_16(*src++);
        }
    }

    finish_transaction(transaction, TFModbusTCPClientResult::Success);
    reset_pending_response();

    return true;
}

void TFModbusTCPClient::abort_connection_hook()
{
    reset_pending_response();
    finish_all_transactions(TFModbusTCPClientResult::AbortedByOtherError);
}

ssize_t TFModbusTCPClient::receive_payload(size_t length)
{
    ssize_t result = recv(socket_fd, pending_payload.bytes + pending_payload_used, length, 0);

    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }

        int saved_errno = errno;
        finish_transaction(pending_header.transaction_id, TFModbusTCPClientResult::ResponseReceiveFailed);
        abort_connection(TFGenericTCPClientEvent::SocketReceiveFailed, saved_errno);
        return -1;
    }

    if (result == 0) {
        finish_transaction(pending_header.transaction_id, TFModbusTCPClientResult::DisconnectedByPeer);
        abort_connection(TFGenericTCPClientEvent::DisconnectedByPeer, 0);
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

void TFModbusTCPClient::finish_transaction(uint16_t transaction_id, TFModbusTCPClientResult result)
{
    TFModbusTCPClientTransaction *transaction = take_transaction(transaction_id);

    if (transaction != nullptr) {
        finish_transaction(transaction, result);
    }
}

void TFModbusTCPClient::finish_transaction(TFModbusTCPClientTransaction *transaction, TFModbusTCPClientResult result)
{
    transaction->callback(result);
    delete transaction;
}

void TFModbusTCPClient::finish_all_transactions(TFModbusTCPClientResult result)
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

        if (TFNetworkUtil::deadline_elapsed(transaction->request_sent + TF_MODBUS_TCP_CLIENT_RESPONSE_TIMEOUT)) {
            transactions[i] = nullptr;
            finish_transaction(transaction, TFModbusTCPClientResult::ResponseTimeout);
        }
    }
}

void TFModbusTCPClient::reset_pending_response()
{
    pending_header_used = 0;
    pending_header_checked = false;
    pending_payload_used = 0;
}
