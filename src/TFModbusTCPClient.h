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
#include "TFModbusTCPCommon.h"

#include <sys/types.h>

// configuration
#define TF_MODBUS_TCP_CLIENT_MAX_SCHEDULED_TRANSACTION_COUNT 8

enum class TFModbusTCPClientTransactionResult
{
    Success = 0,

    ModbusIllegalFunction                    = 0x01,
    ModbusIllegalDataAddress                 = 0x02,
    ModbusIllegalDataValue                   = 0x03,
    ModbusServerDeviceFailure                = 0x04,
    ModbusAcknowledge                        = 0x05,
    ModbusServerDeviceBusy                   = 0x06,
    ModbusMemoryParityError                  = 0x08,
    ModbusGatewayPathUnvailable              = 0x0A,
    ModbusGatewayTargetDeviceFailedToRespond = 0x0B,

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
    ResponseFunctionCodeNotSupported,
    ResponseByteCountInvalid,
    ResponseRegisterCountMismatch,
    ResponseTooShort,
};

const char *get_tf_modbus_tcp_client_transaction_result_name(TFModbusTCPClientTransactionResult result);

typedef std::function<void(TFModbusTCPClientTransactionResult result)> TFModbusTCPClientTransactionCallback;

struct TFModbusTCPClientTransaction
{
    uint8_t function_code;
    uint8_t unit_id;
    uint16_t start_address;
    uint16_t data_count;
    void *buffer;
    uint32_t timeout; // milliseconds
    TFModbusTCPClientTransactionCallback callback;
    TFModbusTCPClientTransaction *next;
};

class TFModbusTCPClient final : public TFGenericTCPClient
{
public:
    TFModbusTCPClient() {};

    void read(TFModbusTCPDataType data_type,
              uint8_t unit_id,
              uint16_t start_address,
              uint16_t data_count,
              void *buffer,
              uint32_t timeout, // milliseconds
              TFModbusTCPClientTransactionCallback &&callback);

private:
    void close_hook() override;
    void tick_hook() override;
    bool receive_hook() override;

    ssize_t receive_response_payload(size_t length);
    void finish_pending_transaction(uint16_t transaction_id, TFModbusTCPClientTransactionResult result);
    void finish_pending_transaction(TFModbusTCPClientTransactionResult result);
    void finish_all_transactions(TFModbusTCPClientTransactionResult result);
    void check_pending_transaction_timeout();
    void reset_pending_response();

    uint16_t next_transaction_id                             = 0;
    TFModbusTCPClientTransaction *pending_transaction        = nullptr;
    uint16_t pending_transaction_id                          = 0;
    uint32_t pending_transaction_deadline                    = 0; // milliseconds
    TFModbusTCPClientTransaction *scheduled_transaction_head = nullptr;
    TFModbusTCPResponse pending_response;
    size_t pending_response_header_used                      = 0;
    bool pending_response_header_checked                     = false;
    size_t pending_response_payload_used                     = 0;
};
