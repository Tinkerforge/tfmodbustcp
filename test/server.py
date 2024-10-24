import logging
from pymodbus.server import StartTcpServer
from pymodbus.datastore import ModbusSequentialDataBlock
from pymodbus.datastore import ModbusSlaveContext, ModbusServerContext

logging.basicConfig()
log = logging.getLogger()
log.setLevel(logging.DEBUG)

store = ModbusSlaveContext(
    di = ModbusSequentialDataBlock(0, [1]*2000),
    co = ModbusSequentialDataBlock(0, [0, 1]*2000),
    hr = ModbusSequentialDataBlock(0, [17]*2000),
    ir = ModbusSequentialDataBlock(0, [17]*2000))
context = ModbusServerContext(slaves=store, single=True)

StartTcpServer(context=context, address=('', 502))
