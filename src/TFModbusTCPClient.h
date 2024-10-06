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

#include "TFGenericTCPClient.h"

// specification
#define TF_MODBUS_TCP_CLIENT_HEADER_LENGTH 7
#define TF_MODBUS_TCP_CLIENT_MIN_FRAME_LENGTH 3
#define TF_MODBUS_TCP_CLIENT_MAX_FRAME_LENGTH 253
#define TF_MODBUS_TCP_CLIENT_FRAME_IN_HEADER_LENGTH 1
#define TF_MODBUS_TCP_CLIENT_MAX_PAYLOAD_LENGTH (TF_MODBUS_TCP_CLIENT_MAX_FRAME_LENGTH - TF_MODBUS_TCP_CLIENT_FRAME_IN_HEADER_LENGTH)
#define TF_MODBUS_TCP_CLIENT_MIN_REGISTER_COUNT 1
#define TF_MODBUS_TCP_CLIENT_MAX_REGISTER_COUNT 125

// configuration
#define TF_MODBUS_TCP_CLIENT_MAX_SCHEDULED_TRANSACTION_COUNT 8

enum class TFModbusTCPClientRegisterType
{
    HoldingRegister,
    InputRegister,
};

enum class TFModbusTCPClientTransactionResult
{
    Success = 0,

    ModbusIllegalFunction = 0x01,
    ModbusIllegalDataAddress = 0x02,
    ModbusIllegalDataValue = 0x03,
    ModbusServerDeviceFailure = 0x04,
    ModbusAcknowledge = 0x05,
    ModbusServerDeviceBusy = 0x06,
    ModbusMemoryParityError = 0x08,
    ModbusGatewayPathUnvailable = 0x0a,
    ModbusGatewayTargetDeviceFailedToRespond = 0x0b,

    InvalidArgument = 256,
    Aborted,
    NoTransactionAvailable,
    NotConnected,
    DisconnectedByPeer,
    SendFailed,
    ReceiveFailed,
    Timeout,
    ResponseShorterThanMinimum,
    ResponseLongerThanMaximum,
    ResponseUnitIDMismatch,
    ResponseFunctionCodeMismatch,
    ResponseByteCountInvalid,
    ResponseRegisterCountMismatch,
    ResponseTooShort,
};

const char *get_tf_modbus_tcp_client_transaction_result_name(TFModbusTCPClientTransactionResult result);

#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wattributes"
#endif

union TFModbusTCPClientHeader
{
    struct [[gnu::packed]] {
        uint16_t transaction_id;
        uint16_t protocol_id;
        uint16_t frame_length;
        uint8_t unit_id;
    };
    uint8_t bytes[TF_MODBUS_TCP_CLIENT_HEADER_LENGTH];
};

#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

typedef std::function<void(TFModbusTCPClientTransactionResult result)> TFModbusTCPClientTransactionCallback;

struct TFModbusTCPClientTransaction
{
    uint8_t function_code;
    uint8_t unit_id;
    uint16_t start_address;
    uint16_t register_count;
    uint16_t *buffer;
    uint32_t timeout; // milliseconds
    TFModbusTCPClientTransactionCallback callback;
    TFModbusTCPClientTransaction *next;
};

union TFModbusTCPClientRegisterResponsePayload
{
    struct {
        uint8_t function_code;
        union {
            uint8_t exception_code;
            uint8_t byte_count;
        };
        uint16_t register_values[TF_MODBUS_TCP_CLIENT_MAX_REGISTER_COUNT];
    };
    uint8_t bytes[TF_MODBUS_TCP_CLIENT_MAX_PAYLOAD_LENGTH];
};

class TFModbusTCPClient final : public TFGenericTCPClient
{
public:
    TFModbusTCPClient() = default;

    void read_register(TFModbusTCPClientRegisterType register_type,
                       uint8_t unit_id,
                       uint16_t start_address,
                       uint16_t register_count,
                       uint16_t *buffer,
                       uint32_t timeout, // milliseconds
                       TFModbusTCPClientTransactionCallback &&callback);

private:
    void close_hook() override;
    void tick_hook() override;
    bool receive_hook() override;

    ssize_t receive_payload(size_t length);
    void finish_pending_transaction(uint16_t transaction_id, TFModbusTCPClientTransactionResult result);
    void finish_pending_transaction(TFModbusTCPClientTransactionResult result);
    void finish_all_transactions(TFModbusTCPClientTransactionResult result);
    void check_pending_transaction_timeout();
    void reset_pending_response();

    uint16_t next_transaction_id = 0;
    TFModbusTCPClientTransaction *pending_transaction = nullptr;
    uint16_t pending_transaction_id = 0;
    uint32_t pending_transaction_deadline = 0; // milliseconds
    TFModbusTCPClientTransaction *scheduled_transaction_head = nullptr;
    TFModbusTCPClientHeader pending_header;
    size_t pending_header_used = 0;
    bool pending_header_checked = false;
    TFModbusTCPClientRegisterResponsePayload pending_payload;
    size_t pending_payload_used = 0;
};
