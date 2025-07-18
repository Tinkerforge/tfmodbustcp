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

#include "TFModbusTCPCommon.h"

#include <stddef.h>

static_assert(sizeof(TFModbusTCPHeader) == TF_MODBUS_TCP_HEADER_LENGTH, "TFModbusTCPHeader has unexpected size");
static_assert(offsetof(TFModbusTCPHeader, transaction_id) == 0, "TFModbusTCPHeader::transaction_id has unexpected offset");
static_assert(offsetof(TFModbusTCPHeader, protocol_id)    == 2, "TFModbusTCPHeader::protocol_id has unexpected offset");
static_assert(offsetof(TFModbusTCPHeader, frame_length)   == 4, "TFModbusTCPHeader::frame_length has unexpected offset");
static_assert(offsetof(TFModbusTCPHeader, unit_id)        == 6, "TFModbusTCPHeader::unit_id has unexpected offset");

static_assert(sizeof(TFModbusTCPRequestPayload) == TF_MODBUS_TCP_MAX_REQUEST_PAYLOAD_LENGTH, "TFModbusTCPRequestPayload has unexpected size");
static_assert(offsetof(TFModbusTCPRequestPayload, function_code)   == 0, "TFModbusTCPRequestPayload::function_code has unexpected offset");
static_assert(offsetof(TFModbusTCPRequestPayload, start_address)   == 1, "TFModbusTCPRequestPayload::start_address has unexpected offset");
static_assert(offsetof(TFModbusTCPRequestPayload, data_count)      == 3, "TFModbusTCPRequestPayload::data_count has unexpected offset");
static_assert(offsetof(TFModbusTCPRequestPayload, data_value)      == 3, "TFModbusTCPRequestPayload::data_value has unexpected offset");
static_assert(offsetof(TFModbusTCPRequestPayload, and_mask)        == 3, "TFModbusTCPRequestPayload::and_mask has unexpected offset");
static_assert(offsetof(TFModbusTCPRequestPayload, byte_count)      == 5, "TFModbusTCPRequestPayload::byte_count has unexpected offset");
static_assert(offsetof(TFModbusTCPRequestPayload, or_mask)         == 5, "TFModbusTCPRequestPayload::or_mask has unexpected offset");
static_assert(offsetof(TFModbusTCPRequestPayload, coil_values)     == 6, "TFModbusTCPRequestPayload::coil_values has unexpected offset");
static_assert(offsetof(TFModbusTCPRequestPayload, register_values) == 6, "TFModbusTCPRequestPayload::register_values has unexpected offset");
static_assert(offsetof(TFModbusTCPRequestPayload, sentinel)        == 7, "TFModbusTCPRequestPayload::sentinel has unexpected offset");
static_assert(offsetof(TFModbusTCPRequestPayload, bytes)           == 0, "TFModbusTCPRequestPayload::header has unexpected offset");

static_assert(sizeof(TFModbusTCPRequest) == TF_MODBUS_TCP_HEADER_LENGTH + TF_MODBUS_TCP_MAX_REQUEST_PAYLOAD_LENGTH, "TFModbusTCPRequest has unexpected size");
static_assert(offsetof(TFModbusTCPRequest, header)  == 0, "TFModbusTCPRequest::header has unexpected offset");
static_assert(offsetof(TFModbusTCPRequest, payload) == 7, "TFModbusTCPRequest::payload has unexpected offset");

static_assert(sizeof(TFModbusTCPResponsePayload) == TF_MODBUS_TCP_MAX_RESPONSE_PAYLOAD_LENGTH, "TFModbusTCPResponsePayload has unexpected size");
static_assert(offsetof(TFModbusTCPResponsePayload, function_code)   == 0, "TFModbusTCPResponsePayload::function_code has unexpected offset");
static_assert(offsetof(TFModbusTCPResponsePayload, exception_code)  == 1, "TFModbusTCPResponsePayload::exception_code has unexpected offset");
static_assert(offsetof(TFModbusTCPResponsePayload, byte_count)      == 1, "TFModbusTCPResponsePayload::byte_count has unexpected offset");
static_assert(offsetof(TFModbusTCPResponsePayload, coil_values)     == 2, "TFModbusTCPResponsePayload::coil_values has unexpected offset");
static_assert(offsetof(TFModbusTCPResponsePayload, register_values) == 2, "TFModbusTCPResponsePayload::register_values has unexpected offset");
static_assert(offsetof(TFModbusTCPResponsePayload, start_address)   == 1, "TFModbusTCPResponsePayload::start_address has unexpected offset");
static_assert(offsetof(TFModbusTCPResponsePayload, data_value)      == 3, "TFModbusTCPResponsePayload::data_value has unexpected offset");
static_assert(offsetof(TFModbusTCPResponsePayload, data_count)      == 3, "TFModbusTCPResponsePayload::data_count has unexpected offset");
static_assert(offsetof(TFModbusTCPResponsePayload, and_mask)        == 3, "TFModbusTCPResponsePayload::and_mask has unexpected offset");
static_assert(offsetof(TFModbusTCPResponsePayload, or_mask)         == 5, "TFModbusTCPResponsePayload::or_mask has unexpected offset");
static_assert(offsetof(TFModbusTCPResponsePayload, sentinel)        == 7, "TFModbusTCPResponsePayload::sentinel has unexpected offset");
static_assert(offsetof(TFModbusTCPResponsePayload, bytes)           == 0, "TFModbusTCPResponsePayload::bytes has unexpected offset");

static_assert(sizeof(TFModbusTCPResponse) == TF_MODBUS_TCP_HEADER_LENGTH + TF_MODBUS_TCP_MAX_RESPONSE_PAYLOAD_LENGTH, "TFModbusTCPResponse has unexpected size");
static_assert(offsetof(TFModbusTCPResponse, header)  == 0, "TFModbusTCPResponse::header has unexpected offset");
static_assert(offsetof(TFModbusTCPResponse, payload) == 7, "TFModbusTCPResponse::payload has unexpected offset");

const char *get_tf_modbus_tcp_byte_order_name(TFModbusTCPByteOrder byte_order)
{
    switch (byte_order) {
    case TFModbusTCPByteOrder::Host:
        return "Host";

    case TFModbusTCPByteOrder::Network:
        return "Network";
    }

    return "<Unknown>";
}

const char *get_tf_modbus_tcp_function_code_name(TFModbusTCPFunctionCode function_code)
{
    switch (function_code) {
    case TFModbusTCPFunctionCode::ReadCoils:
        return "ReadCoils";

    case TFModbusTCPFunctionCode::ReadDiscreteInputs:
        return "ReadDiscreteInputs";

    case TFModbusTCPFunctionCode::ReadHoldingRegisters:
        return "ReadHoldingRegisters";

    case TFModbusTCPFunctionCode::ReadInputRegisters:
        return "ReadInputRegisters";

    case TFModbusTCPFunctionCode::WriteSingleCoil:
        return "WriteSingleCoil";

    case TFModbusTCPFunctionCode::WriteSingleRegister:
        return "WriteSingleRegister";

    case TFModbusTCPFunctionCode::WriteMultipleCoils:
        return "WriteMultipleCoils";

    case TFModbusTCPFunctionCode::WriteMultipleRegisters:
        return "WriteMultipleRegisters";

    case TFModbusTCPFunctionCode::MaskWriteRegister:
        return "MaskWriteRegister";
    }

    return "<Unknown>";
}

const char *get_tf_modbus_tcp_exception_code_name(TFModbusTCPExceptionCode exception_code)
{
    switch (exception_code) {
    case TFModbusTCPExceptionCode::Success:
        return "<Success>";

    case TFModbusTCPExceptionCode::ForceTimeout:
        return "<ForceTimeout>";

    case TFModbusTCPExceptionCode::IllegalFunction:
        return "IllegalFunction";

    case TFModbusTCPExceptionCode::IllegalDataAddress:
        return "IllegalDataAddress";

    case TFModbusTCPExceptionCode::IllegalDataValue:
        return "IllegalDataValue";

    case TFModbusTCPExceptionCode::ServerDeviceFailure:
        return "ServerDeviceFailure";

    case TFModbusTCPExceptionCode::Acknowledge:
        return "Acknowledge";

    case TFModbusTCPExceptionCode::ServerDeviceBusy:
        return "ServerDeviceBusy";

    case TFModbusTCPExceptionCode::MemoryParityError:
        return "MemoryParityError";

    case TFModbusTCPExceptionCode::GatewayPathUnvailable:
        return "GatewayPathUnvailable";

    case TFModbusTCPExceptionCode::GatewayTargetDeviceFailedToRespond:
        return "GatewayTargetDeviceFailedToRespond";
    }

    return "<Unknown>";
}
