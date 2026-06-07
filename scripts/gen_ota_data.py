#!/usr/bin/env python3
import struct
import zlib

seq  = 1
data = struct.pack('<I', seq) + b'\xff' * 20
crc  = zlib.crc32(data) & 0xffffffff
entry = data + struct.pack('<I', crc)
entry = entry + b'\xff' * (32 - len(entry))
sector = entry + b'\xff' * (4096 - len(entry))
ota_data = sector + b'\xff' * 4096

with open('build/ota_data_initial.bin', 'wb') as f:
    f.write(ota_data)

print(f"ota_data_initial.bin written ({len(ota_data)} bytes)")
print(f"First 8 bytes: {ota_data[:8].hex()} (should be 01000000ffffffff)")
