"""console.py [CMD ...] - talk to the Gen1 MCP on COM50 (115200): send each command, print what comes back.
A command is sent as typed text + CR; waits WAIT seconds for the answer (env CON_WAIT, default 1.5)."""
import os, sys, time, serial
wait = float(os.environ.get('CON_WAIT', '1.5'))
s = serial.Serial('COM50', 115200, timeout=0.1)
s.reset_input_buffer()
for cmd in sys.argv[1:]:
    for ch in (cmd + '\r').encode():
        s.write(bytes([ch])); time.sleep(0.01)
    t0 = time.time(); buf = bytearray()
    while time.time() - t0 < wait:
        buf.extend(s.read(4096))
    print('>>> %s\n%s' % (cmd, buf.decode('latin-1', 'replace')))
s.close()
