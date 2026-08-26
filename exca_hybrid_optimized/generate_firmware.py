#!/usr/bin/env python3
"""
Generate device-specific EXCA Hybrid V2 firmware

Usage:
    python3 generate_firmware.py EXCA01
    python3 generate_firmware.py EXCA02
    python3 generate_firmware.py EXCA03
"""

import sys
import re
from pathlib import Path

def generate_firmware(device_id):
    """Generate device-specific firmware"""
    
    # Read base firmware
    base_path = Path(__file__).parent / "exca_hybrid_v2.ino"
    with open(base_path, 'r') as f:
        firmware = f.read()
    
    # Update EXCA_ID
    firmware = re.sub(
        r'const char \*EXCA_ID = "EXCA\d+";',
        f'const char *EXCA_ID = "{device_id}";',
        firmware
    )
    
    # Update AP_SSID
    ap_ssid = f"{device_id}_DATA"
    firmware = re.sub(
        r'const char \*AP_SSID = "EXCA\d+_DATA";',
        f'const char *AP_SSID = "{ap_ssid}";',
        firmware
    )
    
    # Write device-specific firmware
    output_path = Path(__file__).parent / f"exca_hybrid_{device_id.lower()}_v2.ino"
    with open(output_path, 'w') as f:
        f.write(firmware)
    
    print(f"✅ Generated: {output_path.name}")
    return output_path

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 generate_firmware.py EXCA01|EXCA02|EXCA03")
        sys.exit(1)
    
    device_id = sys.argv[1].upper()
    if not device_id.startswith("EXCA"):
        print("Error: Device ID must start with 'EXCA'")
        sys.exit(1)
    
    output = generate_firmware(device_id)
    print(f"📝 Flash {output.name} to {device_id}")

if __name__ == "__main__":
    main()
