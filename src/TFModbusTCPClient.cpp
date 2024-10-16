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
#include <sys/types.h>
#include <lwip/sockets.h>

#include "TFNetworkUtil.h"

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

    case TFModbusTCPClientTransactionResult::ResponseFunctionCodeNotSupported:
        return "ResponseFunctionCodeNotSupported";

    case TFModbusTCPClientTransactionResult::ResponseByteCountInvalid:
        return "ResponseByteCountInvalid";

    case TFModbusTCPClientTransactionResult::ResponseRegisterCountMismatch:
        return "ResponseRegisterCountMismatch";

    case TFModbusTCPClientTransactionResult::ResponseTooShort:
        return "ResponseTooShort";
    }

    return "Unknown";
}

void TFModbusTCPClient::read(TFModbusTCPDataType data_type,
                             uint8_t unit_id,
                             uint16_t start_address,
                             uint16_t data_count,
                             void *buffer,
                             uint32_t timeout, // milliseconds
                             TFModbusTCPClientTransactionCallback &&callback)
{
    uint8_t function_code;

    switch (data_type) {
    case TFModbusTCPDataType::Coil:
        if (data_count < TF_MODBUS_TCP_MIN_READ_COIL_COUNT || data_count > TF_MODBUS_TCP_MAX_READ_COIL_COUNT) {
            callback(TFModbusTCPClientTransactionResult::InvalidArgument);
            return;
        }

        function_code = static_cast<uint8_t>(TFModbusTCPFunctionCode::ReadCoils);
        break;

    case TFModbusTCPDataType::DiscreteInput:
        if (data_count < TF_MODBUS_TCP_MIN_READ_COIL_COUNT || data_count > TF_MODBUS_TCP_MAX_READ_COIL_COUNT) {
            callback(TFModbusTCPClientTransactionResult::InvalidArgument);
            return;
        }

        function_code = static_cast<uint8_t>(TFModbusTCPFunctionCode::ReadDiscreteInputs);
        break;

    case TFModbusTCPDataType::HoldingRegister:
        if (data_count < TF_MODBUS_TCP_MIN_READ_REGISTER_COUNT || data_count > TF_MODBUS_TCP_MAX_READ_REGISTER_COUNT) {
            callback(TFModbusTCPClientTransactionResult::InvalidArgument);
            return;
        }

        function_code = static_cast<uint8_t>(TFModbusTCPFunctionCode::ReadHoldingRegisters);
        break;

    case TFModbusTCPDataType::InputRegister:
        if (data_count < TF_MODBUS_TCP_MIN_READ_REGISTER_COUNT || data_count > TF_MODBUS_TCP_MAX_READ_REGISTER_COUNT) {
            callback(TFModbusTCPClientTransactionResult::InvalidArgument);
            return;
        }

        function_code = static_cast<uint8_t>(TFModbusTCPFunctionCode::ReadInputRegisters);
        break;

    default:
        callback(TFModbusTCPClientTransactionResult::InvalidArgument);
        return;
    }

    if (buffer == nullptr || !callback) {
        callback(TFModbusTCPClientTransactionResult::InvalidArgument);
        return;
    }

    if (socket_fd < 0) {
        callback(TFModbusTCPClientTransactionResult::NotConnected);
        return;
    }

    TFModbusTCPClientTransaction **tail_ptr = &scheduled_transaction_head;
    size_t scheduled_transaction_count = 0;

    while (*tail_ptr != nullptr) {
        tail_ptr = &(*tail_ptr)->next;
        ++scheduled_transaction_count;
    }

    if (scheduled_transaction_count >= TF_MODBUS_TCP_CLIENT_MAX_SCHEDULED_TRANSACTION_COUNT) {
        callback(TFModbusTCPClientTransactionResult::NoTransactionAvailable);
        return;
    }

    TFModbusTCPClientTransaction *transaction = new TFModbusTCPClientTransaction;

    transaction->function_code = function_code;
    transaction->unit_id       = unit_id;
    transaction->start_address = start_address;
    transaction->data_count    = data_count;
    transaction->buffer        = buffer;
    transaction->timeout       = timeout;
    transaction->callback      = std::move(callback);
    transaction->next          = nullptr;

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
        pending_transaction          = scheduled_transaction_head;
        scheduled_transaction_head   = scheduled_transaction_head->next;
        pending_transaction->next    = nullptr;
        pending_transaction_id       = next_transaction_id++;
        pending_transaction_deadline = TFNetworkUtil::calculate_deadline(pending_transaction->timeout);

        TFModbusTCPRequest request;
        size_t payload_length = offsetof(TFModbusTCPRequestPayload, byte_count);

        request.header.transaction_id = swap_16(pending_transaction_id);
        request.header.protocol_id    = swap_16(0);
        request.header.frame_length   = swap_16(TF_MODBUS_TCP_FRAME_IN_HEADER_LENGTH + payload_length);
        request.header.unit_id        = pending_transaction->unit_id;

        request.payload.function_code = pending_transaction->function_code;
        request.payload.start_address = swap_16(pending_transaction->start_address);
        request.payload.data_count    = swap_16(pending_transaction->data_count);

        if (!send(request.bytes, sizeof(request.header) + payload_length)) {
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
        pending_header.protocol_id    = swap_16(pending_header.protocol_id);
        pending_header.frame_length   = swap_16(pending_header.frame_length);

        if (pending_header.protocol_id != 0) {
            disconnect(TFGenericTCPClientDisconnectReason::ProtocolError, -1);
            return false;
        }

        if (pending_header.frame_length < TF_MODBUS_TCP_MIN_FRAME_LENGTH) {
            finish_pending_transaction(pending_header.transaction_id, TFModbusTCPClientTransactionResult::ResponseShorterThanMinimum);
            disconnect(TFGenericTCPClientDisconnectReason::ProtocolError, -1);
            return false;
        }

        if (pending_header.frame_length > TF_MODBUS_TCP_MAX_FRAME_LENGTH) {
            finish_pending_transaction(pending_header.transaction_id, TFModbusTCPClientTransactionResult::ResponseLongerThanMaximum);
            disconnect(TFGenericTCPClientDisconnectReason::ProtocolError, -1);
            return false;
        }

        pending_header_checked = true;
    }

    size_t pending_payload_missing = pending_header.frame_length - TF_MODBUS_TCP_FRAME_IN_HEADER_LENGTH - pending_payload_used;

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

    if (readable > 0 && static_cast<size_t>(readable) < sizeof(TFModbusTCPHeader) && pending_payload_used + readable <= TF_MODBUS_TCP_MAX_PAYLOAD_LENGTH) {
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

    if (pending_transaction->function_code != (pending_payload.function_code & 0x7F)) {
        reset_pending_response();
        finish_pending_transaction(TFModbusTCPClientTransactionResult::ResponseFunctionCodeMismatch);
        return true;
    }

    if ((pending_payload.function_code & 0x80) != 0) {
        reset_pending_response();
        finish_pending_transaction(static_cast<TFModbusTCPClientTransactionResult>(pending_payload.exception_code));
        return true;
    }

    uint8_t expected_byte_count;
    bool copy_coil_values = false;
    bool copy_register_values = false;

    switch (static_cast<TFModbusTCPFunctionCode>(pending_payload.function_code)) {
    case TFModbusTCPFunctionCode::ReadCoils:
    case TFModbusTCPFunctionCode::ReadDiscreteInputs:
        expected_byte_count = (pending_transaction->data_count + 7) / 8;
        copy_coil_values = true;
        break;

    case TFModbusTCPFunctionCode::ReadHoldingRegisters:
    case TFModbusTCPFunctionCode::ReadInputRegisters:
        expected_byte_count = pending_transaction->data_count * 2;
        copy_register_values = true;
        break;

    default:
        reset_pending_response();
        finish_pending_transaction(TFModbusTCPClientTransactionResult::ResponseFunctionCodeNotSupported);
        return true;
    }

    if (expected_byte_count != pending_payload.byte_count) {
        tf_network_util_debugfln("TFModbusTCPClient[%p]::receive_hook() byte count mismatch (expected_byte_count=%u pending_payload.byte_count=%u pending_header.frame_length=%u)",
                                 (void *)this, expected_byte_count, pending_payload.byte_count, pending_header.frame_length);

        reset_pending_response();
        finish_pending_transaction(TFModbusTCPClientTransactionResult::ResponseRegisterCountMismatch);
        return true;
    }

    if (pending_payload_used < TF_MODBUS_TCP_RESPONSE_PAYLOAD_BEFORE_DATA_LENGTH + pending_payload.byte_count) {
        // Intentionally accept too long responses
        reset_pending_response();
        finish_pending_transaction(TFModbusTCPClientTransactionResult::ResponseTooShort);
        return true;
    }

    if (pending_payload_used > TF_MODBUS_TCP_RESPONSE_PAYLOAD_BEFORE_DATA_LENGTH + pending_payload.byte_count) {
        tf_network_util_debugfln("TFModbusTCPClient[%p]::receive_hook() accepting excess payload (excess_payload_length=%zu)",
                                 (void *)this, pending_payload_used - (TF_MODBUS_TCP_RESPONSE_PAYLOAD_BEFORE_DATA_LENGTH + pending_payload.byte_count));
    }

    if (pending_transaction->buffer != nullptr) {
        if (copy_coil_values) {
            uint8_t *buffer = static_cast<uint8_t *>(pending_transaction->buffer);

            for (size_t i = 0; i < pending_payload.byte_count; ++i) {
                buffer[i] = pending_payload.coil_values[i];
            }
        }
        else if (copy_register_values) {
            uint16_t *buffer = static_cast<uint16_t *>(pending_transaction->buffer);

            for (size_t i = 0; i < pending_transaction->data_count; ++i) {
                buffer[i] = swap_16(pending_payload.register_values[i]);
            }
        }
    }

    reset_pending_response();
    finish_pending_transaction(TFModbusTCPClientTransactionResult::Success);
    return true;
}

ssize_t TFModbusTCPClient::receive_payload(size_t length)
{
    if (length == 0) {
        return 0;
    }

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
        pending_transaction          = nullptr;
        pending_transaction_id       = 0;
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
    pending_header_used    = 0;
    pending_header_checked = false;
    pending_payload_used   = 0;
}
