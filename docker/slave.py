import logging
import asyncio
from pymodbus.server import ModbusSerialServer
from pymodbus.datastore import ModbusSequentialDataBlock, ModbusDeviceContext, ModbusServerContext

VERY_BAD_PAYLOAD = b"\x02\x03\xF1\x9C"
BAD_PAYLOAD  = b"\x02\x03\x00\x00\x00\x01\x84\x39"
GOOD_PAYLOAD = b"\x01\x03\x00\x00\x00\x01\x84\x0A"

logging.basicConfig(level=logging.DEBUG)
logger = logging.getLogger()

store = ModbusDeviceContext(
    di=ModbusSequentialDataBlock(1, [1] * 100),
    co=ModbusSequentialDataBlock(1, [1] * 100),
    hr=ModbusSequentialDataBlock(1, [17] * 100),
    ir=ModbusSequentialDataBlock(1, [17] * 100)
)
context = ModbusServerContext(devices=store, single=True)

lock = asyncio.Lock()


async def main():
    server = ModbusSerialServer(context, port='/dev/ttyUSB1', baudrate=115200)
    asyncio.create_task(server.serve_forever())

    while not server.transport:
        await asyncio.sleep(0.1)

    # 1. Save the original non-blocking asyncio transport write method
    original_transport_write = server.transport.write

    # 2. Monkey-patch the transport write so pymodbus responses respect the lock
    def locked_transport_write(data):
        async def _write_with_lock():
            async with lock:
                original_transport_write(data)
                # Brief sleep to ensure the OS buffer doesn't glue back-to-back frames together
                await asyncio.sleep(0.02)

        # Fire off the locked write as a background task
        asyncio.create_task(_write_with_lock())

    # Apply the patch to the server
    server.transport.write = locked_transport_write

    while True:
        for payload in (VERY_BAD_PAYLOAD, BAD_PAYLOAD, GOOD_PAYLOAD):
            await asyncio.sleep(5)

            # 3. Use `async with` for the asyncio.Lock
            async with lock:
                # Use the non-blocking original_transport_write instead of the blocking sync_serial
                original_transport_write(payload)

                # Brief sleep to ensure our manual payload clears before a pending slave response
                await asyncio.sleep(0.02)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
