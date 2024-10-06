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
#include <stdio.h>

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

    case TFModbusTCPClientTransactionResult::Aborted:
        return "Aborted";

    case TFModbusTCPClientTransactionResult::NoTransactionAvailable:
        return "NoTransactionAvailable";

    case TFModbusTCPClientTransactionResult::NotConnected:
        return "NotConnected";

    case TFModbusTCPClientTransactionResult::DisconnectedByPeer:
        return "DisconnectedByPeer";

    case TFModbusTCPClientTransactionResult::SendFailed:
        return "SendFailed";

    case TFModbusTCPClientTransactionResult::ReceiveFailed:
        return "ReceiveFailed";

    case TFModbusTCPClientTransactionResult::Timeout:
        return "Timeout";

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

void TFModbusTCPClient::read_register(TFModbusTCPClientRegisterType register_type,
                                      uint8_t unit_id,
                                      uint16_t start_address,
                                      uint16_t register_count,
                                      uint16_t *buffer,
                                      uint32_t timeout, // milliseconds
                                      TFModbusTCPClientTransactionCallback &&callback)
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

    if (register_count < TF_MODBUS_TCP_CLIENT_MIN_REGISTER_COUNT || register_count > TF_MODBUS_TCP_CLIENT_MAX_REGISTER_COUNT || buffer == nullptr || !callback) {
        callback(TFModbusTCPClientTransactionResult::InvalidArgument);
        return;
    }

    if (socket_fd < 0) {
        callback(TFModbusTCPClientTransactionResult::NotConnected);
        return;
    }

    TFModbusTCPClientTransaction **tail_ptr = &scheduled_transaction_head;
    size_t count = 0;

    while (*tail_ptr != nullptr) {
        tail_ptr = &(*tail_ptr)->next;
        ++count;
    }

    if (count >= TF_MODBUS_TCP_CLIENT_MAX_SCHEDULED_TRANSACTION_COUNT) {
        callback(TFModbusTCPClientTransactionResult::NoTransactionAvailable);
        return;
    }

    TFModbusTCPClientTransaction *transaction = new TFModbusTCPClientTransaction;

    transaction->function_code = function_code;
    transaction->unit_id = unit_id;
    transaction->start_address = start_address;
    transaction->register_count = register_count;
    transaction->buffer = buffer;
    transaction->timeout = timeout;
    transaction->callback = callback;
    transaction->next = nullptr;

    *tail_ptr = transaction;
}

void TFModbusTCPClient::close_hook()
{
    reset_pending_response();
    finish_all_transactions(TFModbusTCPClientTransactionResult::Aborted);
}

void TFModbusTCPClient::tick_hook()
{
    check_pending_transaction_timeout();

    if (pending_transaction == nullptr && scheduled_transaction_head != nullptr) {
        pending_transaction = scheduled_transaction_head;
        scheduled_transaction_head = scheduled_transaction_head->next;
        pending_transaction->next = nullptr;

        pending_transaction_id = next_transaction_id++;
        pending_transaction_deadline = TFNetworkUtil::calculate_deadline(pending_transaction->timeout);

        TFModbusTCPClientHeader header;
        TFModbusTCPClientRegisterRequestPayload payload;

        header.transaction_id = swap_16(pending_transaction_id);
        header.protocol_id = swap_16(0);
        header.frame_length = swap_16(6);
        header.unit_id = pending_transaction->unit_id;

        payload.function_code = pending_transaction->function_code;
        payload.start_address = swap_16(pending_transaction->start_address);
        payload.register_count = swap_16(pending_transaction->register_count);

        uint8_t request[sizeof(header) + sizeof(payload)];

        memcpy(request, header.bytes, sizeof(header));
        memcpy(request + sizeof(header), payload.bytes, sizeof(payload));

        if (!send(request, sizeof(header) + sizeof(payload))) {
            int saved_errno = errno;
            finish_pending_transaction(TFModbusTCPClientTransactionResult::SendFailed);
            disconnect(TFGenericTCPClientDisconnectReason::SocketSendFailed, saved_errno);
        }
    }
}

bool TFModbusTCPClient::receive_hook()
{
    check_pending_transaction_timeout();

    size_t pending_header_missing = sizeof(pending_header) - pending_header_used;

    if (pending_header_missing > 0) {
        ssize_t result = recv(socket_fd, pending_header.bytes + pending_header_used, pending_header_missing, 0);

        if (result < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                disconnect(TFGenericTCPClientDisconnectReason::SocketReceiveFailed, errno);
            }

            return false;
        }

        if (result == 0) {
            disconnect(TFGenericTCPClientDisconnectReason::DisconnectedByPeer, -1);
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
            disconnect(TFGenericTCPClientDisconnectReason::ProtocolError, -1);
            return false;
        }

        if (pending_header.frame_length < TF_MODBUS_TCP_CLIENT_MIN_FRAME_LENGTH) {
            finish_pending_transaction(pending_header.transaction_id, TFModbusTCPClientTransactionResult::ResponseShorterThanMinimum);
            disconnect(TFGenericTCPClientDisconnectReason::ProtocolError, -1);
            return false;
        }

        if (pending_header.frame_length > TF_MODBUS_TCP_CLIENT_MAX_FRAME_LENGTH) {
            finish_pending_transaction(pending_header.transaction_id, TFModbusTCPClientTransactionResult::ResponseLongerThanMaximum);
            disconnect(TFGenericTCPClientDisconnectReason::ProtocolError, -1);
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
        finish_pending_transaction(pending_header.transaction_id, TFModbusTCPClientTransactionResult::ReceiveFailed);
        disconnect(TFGenericTCPClientDisconnectReason::SocketIoctlFailed, saved_errno);
        return false;
    }

    if (readable > 0 && static_cast<size_t>(readable) < sizeof(TFModbusTCPClientHeader) && pending_payload_used + readable <= TF_MODBUS_TCP_CLIENT_MAX_PAYLOAD_LENGTH) {
        ssize_t result = receive_payload(readable);

        if (result <= 0) {
            return false;
        }

        pending_header.frame_length += result;
    }

    if (pending_transaction == nullptr || pending_transaction_id != pending_header.transaction_id) {
        reset_pending_response();
        return true;
    }

    if (pending_transaction->unit_id != pending_header.unit_id) {
        reset_pending_response();
        finish_pending_transaction(TFModbusTCPClientTransactionResult::ResponseUnitIDMismatch);
        return true;
    }

    if (pending_transaction->function_code != (pending_payload.function_code & 0x7f)) {
        reset_pending_response();
        finish_pending_transaction(TFModbusTCPClientTransactionResult::ResponseFunctionCodeMismatch);
        return true;
    }

    if ((pending_payload.function_code & 0x80) != 0) {
        reset_pending_response();
        finish_pending_transaction(static_cast<TFModbusTCPClientTransactionResult>(pending_payload.exception_code));
        return true;
    }

    if ((pending_payload.byte_count & 1) != 0) {
        reset_pending_response();
        finish_pending_transaction(TFModbusTCPClientTransactionResult::ResponseByteCountInvalid);
        return true;
    }

    uint8_t register_count = pending_payload.byte_count / 2;

    if (pending_transaction->register_count != register_count) {
        tf_network_util_debugfln("TFModbusTCPClient[%p]::receive_hook() register count mismatch (pending_transaction->register_count=%u register_count=%u pending_header.frame_length=%u)",
                                 (void *)this, pending_transaction->register_count, register_count, pending_header.frame_length);

        reset_pending_response();
        finish_pending_transaction(TFModbusTCPClientTransactionResult::ResponseRegisterCountMismatch);
        return true;
    }

    if (pending_payload_used < 2 + register_count * 2u) {
        // Intentionally accept too long responses
        reset_pending_response();
        finish_pending_transaction(TFModbusTCPClientTransactionResult::ResponseTooShort);
        return true;
    }

    if (pending_payload_used > 2 + register_count * 2u) {
        tf_network_util_debugfln("TFModbusTCPClient[%p]::receive_hook() accepting excess payload (excess_payload_length=%zu)",
                                 (void *)this, pending_payload_used - (2 + register_count * 2u));
    }

    if (pending_transaction->buffer != nullptr) {
        uint16_t *src = pending_payload.register_values;
        uint16_t *dst = pending_transaction->buffer;
        uint8_t register_remaining = register_count;

        while (register_remaining-- > 0) {
            *dst++ = swap_16(*src++);
        }
    }

    reset_pending_response();
    finish_pending_transaction(TFModbusTCPClientTransactionResult::Success);
    return true;
}

ssize_t TFModbusTCPClient::receive_payload(size_t length)
{
    ssize_t result = recv(socket_fd, pending_payload.bytes + pending_payload_used, length, 0);

    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }

        int saved_errno = errno;
        finish_pending_transaction(pending_header.transaction_id, TFModbusTCPClientTransactionResult::ReceiveFailed);
        disconnect(TFGenericTCPClientDisconnectReason::SocketReceiveFailed, saved_errno);
        return -1;
    }

    if (result == 0) {
        finish_pending_transaction(pending_header.transaction_id, TFModbusTCPClientTransactionResult::DisconnectedByPeer);
        disconnect(TFGenericTCPClientDisconnectReason::DisconnectedByPeer, -1);
        return -1;
    }

    pending_payload_used += result;
    return result;
}

void TFModbusTCPClient::finish_pending_transaction(uint16_t transaction_id, TFModbusTCPClientTransactionResult result)
{
    if (pending_transaction != nullptr && pending_transaction_id == transaction_id) {
        finish_pending_transaction(result);
    }
}

void TFModbusTCPClient::finish_pending_transaction(TFModbusTCPClientTransactionResult result)
{
    if (pending_transaction != nullptr) {
        TFModbusTCPClientTransactionCallback callback = std::move(pending_transaction->callback);
        pending_transaction->callback = nullptr;

        delete pending_transaction;
        pending_transaction = nullptr;
        pending_transaction_id = 0;
        pending_transaction_deadline = 0;

        callback(result);
    }
}

void TFModbusTCPClient::finish_all_transactions(TFModbusTCPClientTransactionResult result)
{
    finish_pending_transaction(result);

    TFModbusTCPClientTransaction *scheduled_transaction = scheduled_transaction_head;
    scheduled_transaction_head = nullptr;

    while (scheduled_transaction != nullptr) {
        TFModbusTCPClientTransactionCallback callback = std::move(scheduled_transaction->callback);
        scheduled_transaction->callback = nullptr;

        TFModbusTCPClientTransaction *scheduled_transaction_next = scheduled_transaction->next;

        delete scheduled_transaction;
        scheduled_transaction = scheduled_transaction_next;

        callback(result);
    }
}

void TFModbusTCPClient::check_pending_transaction_timeout()
{
    if (pending_transaction != nullptr && TFNetworkUtil::deadline_elapsed(pending_transaction_deadline)) {
        finish_pending_transaction(TFModbusTCPClientTransactionResult::Timeout);
    }
}

void TFModbusTCPClient::reset_pending_response()
{
    pending_header_used = 0;
    pending_header_checked = false;
    pending_payload_used = 0;
}
