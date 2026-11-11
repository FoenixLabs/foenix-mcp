"""run_ram.py - load the RAM MCP through the debug port (COM49), start it, show what COM50 says for N seconds."""
import subprocess, sys, time, serial, threading
DBG = r'E:/GEN2_ACTIVE_FPGA/A2560Me_RevB3A_Work/host/dbg2/a2560me_dbg.py'
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 8
import os
PORT = os.environ.get('DBG_PORT', 'COM49')			# 11-06: K60 = COM51
IMG = os.environ.get('MCP_IMG', 'mcp_ram.bin')
s = serial.Serial('COM50', 115200, timeout=0.1)
s.reset_input_buffer()
buf = bytearray()
stop = False
def rd():
    while not stop:
        buf.extend(s.read(4096))
t = threading.Thread(target=rd); t.start()
r = subprocess.run([sys.executable, DBG, '--port', PORT, '--run', IMG, '--address', '0'], capture_output=True, text=True)
print('load:', (r.stdout + r.stderr).strip().splitlines()[-1:] )
time.sleep(secs)
stop = True; t.join(); s.close()
print(buf.decode('latin-1', 'replace'))
