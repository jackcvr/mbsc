import logging
import asyncio
from pymodbus.server import ModbusSerialServer
from pymodbus.datastore import ModbusSequentialDataBlock, ModbusDeviceContext, ModbusServerContext

GOOD_PAYLOAD = b"\x01\x03\x00\x00\x00\x01\x84\x0A"
BAD_PAYLOAD  = b"\x02\x03\x00\x00\x00\x01\x84\x39"
VERY_BAD_PAYLOAD = b"\x02\x03\xF1\x9C"

logging.basicConfig(level=logging.DEBUG)
logger = logging.getLogger()

store = ModbusDeviceContext(
    di=ModbusSequentialDataBlock(1, [1] * 100),
    co=ModbusSequentialDataBlock(1, [1] * 100),
    hr=ModbusSequentialDataBlock(1, [17] * 100),
    ir=ModbusSequentialDataBlock(1, [17] * 100)
)
context = ModbusServerContext(devices=store, single=True)


async def main():
    server = ModbusSerialServer(context, port='/dev/ttyUSB1', baudrate=115200)
    asyncio.create_task(server.serve_forever())

    while not server.transport:
        await asyncio.sleep(0.1)

    serial_device = server.transport.sync_serial

    while True:
        for payload in (VERY_BAD_PAYLOAD, BAD_PAYLOAD, GOOD_PAYLOAD):
            await asyncio.sleep(5)
            serial_device.write(payload)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
