#!/usr/bin/env python3
"""Drives the drum machine over USB and measures its speaker with its own microphone."""
import os
import select
import sys
import termios
import time

PORT = "/dev/ttyACM0"
LOUDER_RATIO = 4  # 12 dB
MAX_LOAD_PERMILLE = 500


def parse_fields(line):
    fields = {}
    for word in line.split():
        name, separator, value = word.partition("=")
        if separator and value.lstrip("-").isdigit():
            fields[name] = int(value)
    return fields


def is_louder(level, reference):
    return level >= reference * LOUDER_RATIO and level > 0


class Device:
    def __init__(self):
        self.fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY)
        attributes = termios.tcgetattr(self.fd)
        attributes[0] = attributes[1] = attributes[3] = 0
        attributes[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        termios.tcsetattr(self.fd, termios.TCSANOW, attributes)
        self.buffer = b""

    def send(self, line):
        os.write(self.fd, line.encode() + b"\n")

    def read_line(self, timeout):
        deadline = time.monotonic() + timeout
        while b"\n" not in self.buffer:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            if select.select([self.fd], [], [], remaining)[0]:
                self.buffer += os.read(self.fd, 256)
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line.decode(errors="replace").strip()

    def wait_for(self, prefix, timeout=5.0):
        deadline = time.monotonic() + timeout
        while True:
            line = self.read_line(max(deadline - time.monotonic(), 0))
            if line is None:
                raise TimeoutError(f"no line starting with {prefix}")
            if line.startswith(prefix):
                return line

    def key(self, name, mods="-"):
        self.send(f"K {mods} {name}")
        self.wait_for("OK")

    def state(self):
        self.send("S")
        fields = parse_fields(self.wait_for("STATE "))
        lanes = {}
        while True:
            line = self.read_line(5.0)
            if line is None:
                raise TimeoutError("state dump ended early")
            if line.startswith("LANE"):
                _, lane, steps = line.split()
                lanes[int(lane)] = steps
            elif line.startswith("STATE-END"):
                return fields, lanes

    def level(self, ms):
        self.send(f"L {ms}")
        return parse_fields(self.wait_for("LEVEL", ms / 1000 + 5))["peak"]

    def cpu(self):
        self.send("C")
        return parse_fields(self.wait_for("CPU"))

    def read_boot(self, timeout=15):
        deadline = time.monotonic() + timeout
        lines = []
        while time.monotonic() < deadline:
            line = self.read_line(max(deadline - time.monotonic(), 0))
            if line is None:
                break
            lines.append(line)
            if line.startswith("READY"):
                break
        return lines

    def wait_ready(self):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            self.send("S")
            line = self.read_line(1.0)
            while line is not None and time.monotonic() < deadline:
                if line.startswith("STATE-END"):
                    return
                line = self.read_line(0.3)
        raise TimeoutError("device did not answer")


class Report:
    def __init__(self):
        self.failures = 0

    def check(self, name, passed, detail=""):
        print(f"{'PASS' if passed else 'FAIL'} {name} {detail}".rstrip())
        self.failures += 0 if passed else 1


def open_device(timeout=20.0):
    deadline = time.monotonic() + timeout
    while True:
        try:
            return Device()
        except OSError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.25)


def reset_device():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import slot

    slot.esptool("chip_id")


def run():
    report = Report()
    device = open_device()
    device.wait_ready()
    fields, _ = device.state()
    if fields["playing"] == 1:
        device.key("space")
        device.wait_for("SAVE")
    for _ in range(7):
        fields, _ = device.state()
        if fields["swing"] == 50:
            break
        device.key("w", "f")
    device.send("X")
    device.wait_for("OK")

    fields, lanes = device.state()
    report.check("storage available", fields["storage"] == 1)
    device.send("F")
    print(device.wait_for("FS"))

    quiet = device.level(500)
    print(f"INFO quiet peak {quiet}")

    device.key("2")  # Snare, audible on a small speaker.
    time.sleep(0.6)
    for key in ("q", "t", "a", "g"):
        device.key(key)
    fields, lanes = device.state()
    report.check("steps entered", lanes[1] == "x...x...x...x...", lanes[1])

    device.key("space")
    time.sleep(0.3)
    playing = device.level(2000)
    fields, _ = device.state()
    report.check("playing", fields["playing"] == 1)
    report.check("speaker heard while playing", is_louder(playing, quiet), f"{playing} vs {quiet}")
    cpu = device.cpu()
    report.check("cpu load", cpu["load_permille"] <= MAX_LOAD_PERMILLE, f"{cpu['load_permille'] / 10}%")
    report.check("audio exchange failures", cpu["failures"] == 0, str(cpu["failures"]))

    before = device.cpu()
    time.sleep(3)
    after = device.cpu()
    report.check("no overflows while playing", after["tx_overflows"] == before["tx_overflows"]
                 and after["rx_overflows"] == before["rx_overflows"])

    device.key("g", "f")
    fields, lanes = device.state()
    report.check("grid changed to 1/8", fields["grid"] == 1, str(fields["grid"]))
    report.check("steps remapped", lanes[1] == "x.x.x.x.........", lanes[1])
    device.key("z", "f")
    fields, lanes = device.state()
    report.check("undo restored grid", fields["grid"] == 2 and lanes[1] == "x...x...x...x...")
    device.key("w", "f")
    fields, _ = device.state()
    report.check("swing changed", fields["swing"] == 54, str(fields["swing"]))

    before = device.cpu()
    device.key("space")
    line = device.wait_for("SAVE")
    report.check("project saved", "ok=1" in line, line)
    time.sleep(2.0)
    stopped = device.level(500)
    fields, _ = device.state()
    report.check("stopped", fields["playing"] == 0)
    report.check("silent after stop", not is_louder(stopped, quiet), f"{stopped} vs {quiet}")
    after = device.cpu()
    report.check("no overflows during save", after["tx_overflows"] == before["tx_overflows"]
                 and after["rx_overflows"] == before["rx_overflows"])

    os.close(device.fd)
    reset_device()
    device = open_device()
    boot = device.read_boot(timeout=15)
    report.check("display is landscape", any("width=240 height=135" in line for line in boot), str(boot))
    report.check("keyboard started", not any(line.startswith("ERROR") for line in boot), str(boot))
    device.wait_ready()
    fields, lanes = device.state()
    report.check("pattern survives reboot", lanes[1] == "x...x...x...x...", lanes[1])
    report.check("swing survives reboot", fields["swing"] == 54, str(fields["swing"]))

    device.key("y")  # Step 5 on lane 0, the selected lane after a reboot.
    line = device.wait_for("SAVE", 6.0)
    report.check("edit saved while stopped", "ok=1" in line, line)
    os.close(device.fd)
    reset_device()
    device = open_device()
    device.wait_ready()
    fields, lanes = device.state()
    report.check("stopped edit survives reboot", lanes[0][5] == "x", lanes[0])
    device.key("y")
    device.wait_for("SAVE", 6.0)
    os.close(device.fd)

    print(f"{report.failures} failures")
    return 1 if report.failures else 0


if __name__ == "__main__":
    sys.exit(run())
