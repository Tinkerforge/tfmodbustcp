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

#define debugfln(fmt, ...) tf_network_util_debugfln("TFModbusTCPClient[%p]::" fmt, static_cast<void *>(this) __VA_OPT__(,) __VA_ARGS__)

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

    case TFModbusTCPClientTransactionResult::ResponseByteCountMismatch:
        return "ResponseByteCountMismatch";

    case TFModbusTCPClientTransactionResult::ResponseStartAddressMismatch:
        return "ResponseStartAddressMismatch";

    case TFModbusTCPClientTransactionResult::ResponseDataValueMismatch:
        return "ResponseDataValueMismatch";

    case TFModbusTCPClientTransactionResult::ResponseDataCountMismatch:
        return "ResponseDataCountMismatch";

    case TFModbusTCPClientTransactionResult::ResponseTooShort:
        return "ResponseTooShort";
    }

    return "<Unknown>";
}

void TFModbusTCPClient::transact(uint8_t unit_id,
                                 TFModbusTCPFunctionCode function_code,
                                 uint16_t start_address,
                                 uint16_t data_count,
                                 void *buffer,
                                 micros_t timeout,
                                 TFModbusTCPClientTransactionCallback &&callback)
{
    switch (function_code) {
    case TFModbusTCPFunctionCode::ReadCoils:
        if (data_count < TF_MODBUS_TCP_MIN_READ_COIL_COUNT || data_count > TF_MODBUS_TCP_MAX_READ_COIL_COUNT) {
            callback(TFModbusTCPClientTransactionResult::InvalidArgument);
            return;
        }

        break;

    case TFModbusTCPFunctionCode::ReadDiscreteInputs:
        if (data_count < TF_MODBUS_TCP_MIN_READ_COIL_COUNT || data_count > TF_MODBUS_TCP_MAX_READ_COIL_COUNT) {
            callback(TFModbusTCPClientTransactionResult::InvalidArgument);
            return;
        }

        break;

    case TFModbusTCPFunctionCode::ReadHoldingRegisters:
        if (data_count < TF_MODBUS_TCP_MIN_READ_REGISTER_COUNT || data_count > TF_MODBUS_TCP_MAX_READ_REGISTER_COUNT) {
            callback(TFModbusTCPClientTransactionResult::InvalidArgument);
            return;
        }

        break;

    case TFModbusTCPFunctionCode::ReadInputRegisters:
        if (data_count < TF_MODBUS_TCP_MIN_READ_REGISTER_COUNT || data_count > TF_MODBUS_TCP_MAX_READ_REGISTER_COUNT) {
            callback(TFModbusTCPClientTransactionResult::InvalidArgument);
            return;
        }

        break;

    case TFModbusTCPFunctionCode::WriteSingleCoil:
        if (data_count != 1) {
            callback(TFModbusTCPClientTransactionResult::InvalidArgument);
            return;
        }

        if ((static_cast<uint8_t *>(buffer)[0] | 0x01) != 0x01) {
            callback(TFModbusTCPClientTransactionResult::InvalidArgument);
            return;
        }

        break;

    case TFModbusTCPFunctionCode::WriteSingleRegister:
        if (data_count != 1) {
            callback(TFModbusTCPClientTransactionResult::InvalidArgument);
            return;
        }

        break;

    case TFModbusTCPFunctionCode::WriteMultipleCoils:
        if (data_count < TF_MODBUS_TCP_MIN_WRITE_COIL_COUNT || data_count > TF_MODBUS_TCP_MAX_WRITE_COIL_COUNT) {
            callback(TFModbusTCPClientTransactionResult::InvalidArgument);
            return;
        }

        if ((static_cast<uint8_t *>(buffer)[(data_count + 7) / 8 - 1] | ((1u << (data_count % 8)) - 1)) != ((1u << (data_count % 8)) - 1)) {
            callback(TFModbusTCPClientTransactionResult::InvalidArgument);
            return;
        }

        break;

    case TFModbusTCPFunctionCode::WriteMultipleRegisters:
        if (data_count < TF_MODBUS_TCP_MIN_WRITE_REGISTER_COUNT || data_count > TF_MODBUS_TCP_MAX_WRITE_REGISTER_COUNT) {
            callback(TFModbusTCPClientTransactionResult::InvalidArgument);
            return;
        }

        break;

    default:
        callback(TFModbusTCPClientTransactionResult::InvalidArgument);
        return;
    }

    if (buffer == nullptr || timeout < 0_s || !callback) {
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

    transaction->unit_id       = unit_id;
    transaction->function_code = function_code;
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
        pending_transaction_deadline = calculate_deadline(pending_transaction->timeout);

        TFModbusTCPRequest request;
        size_t payload_length = offsetof(TFModbusTCPRequestPayload, byte_count);

        request.header.transaction_id = htons(pending_transaction_id);
        request.header.protocol_id    = htons(0);
        request.header.frame_length   = htons(TF_MODBUS_TCP_FRAME_IN_HEADER_LENGTH + payload_length);
        request.header.unit_id        = pending_transaction->unit_id;

        request.payload.function_code = static_cast<uint8_t>(pending_transaction->function_code);
        request.payload.start_address = htons(pending_transaction->start_address);

        switch (pending_transaction->function_code) {
        case TFModbusTCPFunctionCode::ReadCoils:
        case TFModbusTCPFunctionCode::ReadDiscreteInputs:
        case TFModbusTCPFunctionCode::ReadHoldingRegisters:
        case TFModbusTCPFunctionCode::ReadInputRegisters:
            request.payload.data_count = htons(pending_transaction->data_count);
            break;

        case TFModbusTCPFunctionCode::WriteSingleCoil:
            request.payload.data_value = htons(static_cast<uint8_t *>(pending_transaction->buffer)[0] != 0 ? 0xFF00 : 0x0000);
            break;

        case TFModbusTCPFunctionCode::WriteSingleRegister:
            if (register_byte_order == TFModbusTCPByteOrder::Host) {
                request.payload.data_value = htons(static_cast<uint16_t *>(pending_transaction->buffer)[0]);
            }
            else { // TFModbusTCPByteOrder::Network
                request.payload.data_value = static_cast<uint16_t *>(pending_transaction->buffer)[0];
            }

            break;

        case TFModbusTCPFunctionCode::WriteMultipleCoils:
            request.payload.data_count = htons(pending_transaction->data_count);
            request.payload.byte_count = (pending_transaction->data_count + 7) / 8;

            memcpy(request.payload.coil_values, pending_transaction->buffer, request.payload.byte_count);
            break;

        case TFModbusTCPFunctionCode::WriteMultipleRegisters:
            request.payload.data_count = htons(pending_transaction->data_count);
            request.payload.byte_count = pending_transaction->data_count * 2;

            if (register_byte_order == TFModbusTCPByteOrder::Host) {
                uint16_t *buffer = static_cast<uint16_t *>(pending_transaction->buffer);

                for (size_t i = 0; i < pending_transaction->data_count; ++i) {
                    request.payload.register_values[i] = htons(buffer[i]);
                }
            }
            else { // TFModbusTCPByteOrder::Network
                memcpy(request.payload.register_values, pending_transaction->buffer, request.payload.byte_count);
            }

            break;
        }

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

    size_t pending_response_header_missing = sizeof(pending_response.header) - pending_response_header_used;

    if (pending_response_header_missing > 0) {
        ssize_t result = recv(socket_fd, pending_response.header.bytes + pending_response_header_used, pending_response_header_missing, 0);

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

        pending_response_header_used += result;
        return true;
    }

    if (!pending_response_header_checked) {
        pending_response.header.transaction_id = ntohs(pending_response.header.transaction_id);
        pending_response.header.protocol_id    = ntohs(pending_response.header.protocol_id);
        pending_response.header.frame_length   = ntohs(pending_response.header.frame_length);

        if (pending_response.header.protocol_id != 0) {
            disconnect(TFGenericTCPClientDisconnectReason::ProtocolError, -1);
            return false;
        }

        if (pending_response.header.frame_length < TF_MODBUS_TCP_MIN_RESPONSE_FRAME_LENGTH) {
            finish_pending_transaction(pending_response.header.transaction_id, TFModbusTCPClientTransactionResult::ResponseShorterThanMinimum);
            disconnect(TFGenericTCPClientDisconnectReason::ProtocolError, -1);
            return false;
        }

        if (pending_response.header.frame_length > TF_MODBUS_TCP_MAX_RESPONSE_FRAME_LENGTH) {
            finish_pending_transaction(pending_response.header.transaction_id, TFModbusTCPClientTransactionResult::ResponseLongerThanMaximum);
            disconnect(TFGenericTCPClientDisconnectReason::ProtocolError, -1);
            return false;
        }

        pending_response_header_checked = true;
    }

    size_t pending_response_payload_missing = pending_response.header.frame_length - TF_MODBUS_TCP_FRAME_IN_HEADER_LENGTH - pending_response_payload_used;

    if (pending_response_payload_missing > 0) {
        if (receive_response_payload(pending_response_payload_missing) <= 0) {
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
        finish_pending_transaction(pending_response.header.transaction_id, TFModbusTCPClientTransactionResult::ReceiveFailed);
        disconnect(TFGenericTCPClientDisconnectReason::SocketIoctlFailed, saved_errno);
        return false;
    }

    if (readable > 0
     && static_cast<size_t>(readable) < sizeof(TFModbusTCPHeader)
     && pending_response_payload_used + readable <= TF_MODBUS_TCP_MAX_RESPONSE_PAYLOAD_LENGTH) {
        ssize_t result = receive_response_payload(readable);

        if (result <= 0) {
            return false;
        }

        pending_response.header.frame_length += result;
    }

    if (pending_transaction == nullptr || pending_transaction_id != pending_response.header.transaction_id) {
        reset_pending_response();
        return true;
    }

    if (pending_transaction->unit_id != pending_response.header.unit_id) {
        reset_pending_response();
        finish_pending_transaction(TFModbusTCPClientTransactionResult::ResponseUnitIDMismatch);
        return true;
    }

    if (pending_transaction->function_code != static_cast<TFModbusTCPFunctionCode>(pending_response.payload.function_code & 0x7F)) {
        debugfln("receive_hook() function code mismatch (pending_transaction->function_code=0x%02x pending_response.payload.function_code=0x%02x)",
                 static_cast<uint8_t>(pending_transaction->function_code), pending_response.payload.function_code);

        reset_pending_response();
        finish_pending_transaction(TFModbusTCPClientTransactionResult::ResponseFunctionCodeMismatch);
        return true;
    }

    if ((pending_response.payload.function_code & 0x80) != 0) {
        reset_pending_response();
        finish_pending_transaction(static_cast<TFModbusTCPClientTransactionResult>(pending_response.payload.exception_code));
        return true;
    }

    uint8_t expected_byte_count = 0;
    bool copy_coil_values       = false;
    bool copy_register_values   = false;
    bool check_start_address    = false;
    bool check_data_value       = false;
    uint16_t expected_data_value; // as TFModbusTCPByteOrder::Host
    bool check_data_count       = false;

    switch (static_cast<TFModbusTCPFunctionCode>(pending_response.payload.function_code)) {
    case TFModbusTCPFunctionCode::ReadCoils:
    case TFModbusTCPFunctionCode::ReadDiscreteInputs:
        expected_byte_count = (pending_transaction->data_count + 7) / 8;
        copy_coil_values    = true;
        break;

    case TFModbusTCPFunctionCode::ReadHoldingRegisters:
    case TFModbusTCPFunctionCode::ReadInputRegisters:
        expected_byte_count  = pending_transaction->data_count * 2;
        copy_register_values = true;
        break;

    case TFModbusTCPFunctionCode::WriteSingleCoil:
        check_start_address = true;
        check_data_value    = true;
        expected_data_value = static_cast<uint8_t *>(pending_transaction->buffer)[0] != 0 ? 0xFF00 : 0x0000;
        break;

    case TFModbusTCPFunctionCode::WriteSingleRegister:
        check_start_address = true;
        check_data_value    = true;

        if (register_byte_order == TFModbusTCPByteOrder::Host) {
            expected_data_value = static_cast<uint16_t *>(pending_transaction->buffer)[0];
        }
        else { // TFModbusTCPByteOrder::Network
            expected_data_value = ntohs(static_cast<uint16_t *>(pending_transaction->buffer)[0]);
        }

        break;

    case TFModbusTCPFunctionCode::WriteMultipleCoils:
    case TFModbusTCPFunctionCode::WriteMultipleRegisters:
        check_start_address = true;
        check_data_count    = true;
        break;

    default:
        reset_pending_response();
        finish_pending_transaction(TFModbusTCPClientTransactionResult::ResponseFunctionCodeNotSupported);
        return true;
    }

    if (expected_byte_count > 0) {
        if (pending_response.payload.byte_count != expected_byte_count) {
            debugfln("receive_hook() byte count mismatch (pending_response.payload.byte_count=%u pending_response.header.frame_length=%u expected_byte_count=%u)",
                     pending_response.payload.byte_count, pending_response.header.frame_length, expected_byte_count);

            reset_pending_response();
            finish_pending_transaction(TFModbusTCPClientTransactionResult::ResponseByteCountMismatch);
            return true;
        }

        if (pending_response_payload_used < TF_MODBUS_TCP_RESPONSE_PAYLOAD_BEFORE_DATA_LENGTH + pending_response.payload.byte_count) {
            // Intentionally accept too long responses
            reset_pending_response();
            finish_pending_transaction(TFModbusTCPClientTransactionResult::ResponseTooShort);
            return true;
        }

        if (pending_response_payload_used > TF_MODBUS_TCP_RESPONSE_PAYLOAD_BEFORE_DATA_LENGTH + pending_response.payload.byte_count) {
            debugfln("receive_hook() accepting excess payload (excess_payload_length=%zu)",
                     pending_response_payload_used - (TF_MODBUS_TCP_RESPONSE_PAYLOAD_BEFORE_DATA_LENGTH + pending_response.payload.byte_count));
        }

        if (pending_transaction->buffer != nullptr) {
            if (copy_coil_values) {
                memcpy(pending_transaction->buffer, pending_response.payload.coil_values, pending_response.payload.byte_count);
                static_cast<uint8_t *>(pending_transaction->buffer)[pending_response.payload.byte_count - 1] &= (1u << (pending_transaction->data_count % 8)) - 1;
            }

            if (copy_register_values) {
                if (register_byte_order == TFModbusTCPByteOrder::Host) {
                    uint16_t *buffer = static_cast<uint16_t *>(pending_transaction->buffer);

                    for (size_t i = 0; i < pending_transaction->data_count; ++i) {
                        buffer[i] = ntohs(pending_response.payload.register_values[i]);
                    }
                }
                else { // TFModbusTCPByteOrder::Network
                    memcpy(pending_transaction->buffer, pending_response.payload.register_values, pending_response.payload.byte_count);
                }
            }
        }
    }

    if (check_start_address) {
        uint16_t actual_start_address = ntohs(pending_response.payload.start_address);

        if (actual_start_address != pending_transaction->start_address) {
            debugfln("receive_hook() start address mismatch (pending_response.payload.start_address=%u pending_transaction->start_address=%u)",
                     actual_start_address, pending_transaction->start_address);

            reset_pending_response();
            finish_pending_transaction(TFModbusTCPClientTransactionResult::ResponseStartAddressMismatch);
            return true;
        }
    }

    if (check_data_value) {
        uint16_t actual_data_value = ntohs(pending_response.payload.data_value);

        if (actual_data_value != expected_data_value) {
            debugfln("receive_hook() data value mismatch (pending_response.payload.data_value=%u expected_data_value=%u)",
                     actual_data_value, expected_data_value);

            reset_pending_response();
            finish_pending_transaction(TFModbusTCPClientTransactionResult::ResponseDataValueMismatch);
            return true;
        }
    }

    if (check_data_count) {
        uint16_t actual_data_count = ntohs(pending_response.payload.data_count);

        if (actual_data_count != pending_transaction->data_count) {
            debugfln("receive_hook() data count mismatch (pending_response.payload.data_count=%u pending_transaction->data_count=%u)",
                     actual_data_count, pending_transaction->data_count);

            reset_pending_response();
            finish_pending_transaction(TFModbusTCPClientTransactionResult::ResponseDataCountMismatch);
            return true;
        }
    }

    reset_pending_response();
    finish_pending_transaction(TFModbusTCPClientTransactionResult::Success);
    return true;
}

ssize_t TFModbusTCPClient::receive_response_payload(size_t length)
{
    if (length == 0) {
        return 0;
    }

    ssize_t result = recv(socket_fd, pending_response.payload.bytes + pending_response_payload_used, length, 0);

    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }

        int saved_errno = errno;
        finish_pending_transaction(pending_response.header.transaction_id, TFModbusTCPClientTransactionResult::ReceiveFailed);
        disconnect(TFGenericTCPClientDisconnectReason::SocketReceiveFailed, saved_errno);
        return -1;
    }

    if (result == 0) {
        finish_pending_transaction(pending_response.header.transaction_id, TFModbusTCPClientTransactionResult::DisconnectedByPeer);
        disconnect(TFGenericTCPClientDisconnectReason::DisconnectedByPeer, -1);
        return -1;
    }

    pending_response_payload_used += result;
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
        pending_transaction_deadline = 0_s;

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
    if (pending_transaction != nullptr && deadline_elapsed(pending_transaction_deadline)) {
        finish_pending_transaction(TFModbusTCPClientTransactionResult::Timeout);
    }
}

void TFModbusTCPClient::reset_pending_response()
{
    pending_response_header_used    = 0;
    pending_response_header_checked = false;
    pending_response_payload_used   = 0;
}
