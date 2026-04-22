#!/usr/bin/env python3
import itertools
import subprocess
import time
import sys
import logging
import selectors

NAME = "mbsc.amd64"
REQUESTS = ["rr 1 0 2", "req 01030062000265D5"]
POLL_INTERVAL = 5.0
RESPONSE_TIMEOUT = 2.0

logging.basicConfig(level=logging.INFO)


def main():
    p = subprocess.Popen(
        [f"./build/{NAME}", "-o", "/dev/ttyUSB0", "-s"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1
    )

    sel = selectors.DefaultSelector()
    sel.register(p.stdout, selectors.EVENT_READ)
    sel.register(p.stderr, selectors.EVENT_READ)

    logging.info("Starting Modbus polling loop. Press Ctrl+C to exit.")

    requests = iter(itertools.cycle(REQUESTS))
    next_request_time = time.monotonic()
    request_time = None

    try:
        while True:
            # Check if the process died
            if p.poll() is not None:
                logging.error("The process terminated unexpectedly.")
                break

            events = sel.select(timeout=0.1)

            for key, _ in events:
                pipe = key.fileobj
                line = pipe.readline()

                if not line:
                    # Pipe was closed by the subprocess
                    sel.unregister(pipe)
                    continue

                line = line.strip()
                if not line:
                    continue

                # Format based on which pipe the data came from
                if pipe is p.stderr:
                    logging.error(line)
                else:
                    if request_time:
                        logging.info(line)
                    else:
                        logging.info("> %s", line)

                # Clear the timeout clock if we got ANY response or error
                if request_time is not None:
                    request_time = None

            # Handle writes and timeouts if no events are blocking us
            now = time.monotonic()

            # Check for timeout
            if request_time and now > request_time + RESPONSE_TIMEOUT:
                logging.error(f"Timeout: No response received")
                request_time = None

            # Send the next request
            if now >= next_request_time:
                try:
                    p.stdin.write(next(requests) + "\n")
                    p.stdin.flush()
                    request_time = now
                    next_request_time += POLL_INTERVAL
                except BrokenPipeError:
                    logging.error("Unable to write to process (broken pipe).")
                    break

    except KeyboardInterrupt:
        pass
    finally:
        sel.close()
        if p.poll() is None:
            p.terminate()
            p.wait(timeout=2)
            if p.poll() is None:
                p.kill()

if __name__ == "__main__":
    main()
