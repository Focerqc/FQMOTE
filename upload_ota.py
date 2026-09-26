#!/usr/bin/env python3
"""
FQMOTE Local OTA Upload Utility
Uploads firmware.bin wirelessly to Leafblaster / Pubmote over local home Wi-Fi.
No dependencies required (uses standard Python library).
"""

import sys
import os
import time
import urllib.request
import urllib.error

def print_progress(bytes_sent, total_bytes, start_time):
    pct = (bytes_sent / total_bytes) * 100 if total_bytes > 0 else 0
    elapsed = time.time() - start_time
    speed = (bytes_sent / 1024) / elapsed if elapsed > 0 else 0
    bar_width = 30
    filled = int(bar_width * bytes_sent / total_bytes)
    bar = '=' * filled + '-' * (bar_width - filled)
    sys.stdout.write(f"\r[{bar}] {pct:5.1f}% ({bytes_sent//1024:,} / {total_bytes//1024:,} KB) @ {speed:.1f} KB/s")
    sys.stdout.flush()

class ProgressReader:
    def __init__(self, filename, total_size):
        self.f = open(filename, 'rb')
        self.total = total_size
        self.sent = 0
        self.start = time.time()

    def read(self, size=-1):
        chunk = self.f.read(size)
        if chunk:
            self.sent += len(chunk)
            print_progress(self.sent, self.total, self.start)
        return chunk

    def __len__(self):
        return self.total

    def close(self):
        self.f.close()

def main():
    if len(sys.argv) < 2:
        print("\n=== FQMOTE Wi-Fi OTA Flasher ===")
        print("Usage: python upload_ota.py <IP_ADDRESS> [PATH_TO_FIRMWARE_BIN]")
        print("Example: python upload_ota.py 192.168.1.150\n")
        sys.exit(1)

    ip = sys.argv[1].strip()
    if ip.startswith("http://"):
        ip = ip[7:]
    if ip.endswith("/"):
        ip = ip[:-1]
    
    url = f"http://{ip}/update"

    bin_path = None
    if len(sys.argv) >= 3:
        bin_path = sys.argv[2]
    else:
        default_path = os.path.join(".pio", "build", "leafblaster_esp32s3_touch_amoled_143_co5300", "firmware.bin")
        if os.path.exists(default_path):
            bin_path = default_path
        else:
            print(f"Error: Firmware binary not found at default location:\n  {default_path}")
            print("Please build firmware first or specify path as argument.")
            sys.exit(1)

    if not os.path.exists(bin_path):
        print(f"Error: Specified file does not exist: {bin_path}")
        sys.exit(1)

    file_size = os.path.getsize(bin_path)
    print("\n=== FQMOTE Wi-Fi OTA Flasher ===")
    print(f"Target Remote : {url}")
    print(f"Firmware File : {bin_path}")
    print(f"Firmware Size : {file_size:,} bytes ({file_size/1024/1024:.2f} MB)")
    print("Initiating upload to Leafblaster...\n")

    reader = ProgressReader(bin_path, file_size)
    req = urllib.request.Request(
        url,
        data=reader,
        headers={
            'Content-Type': 'application/octet-stream',
            'Content-Length': str(file_size),
        },
        method='POST'
    )

    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            reader.close()
            print("\n")
            if resp.status == 200:
                print("==================================================")
                print(" SUCCESS: Firmware uploaded & flashed successfully!")
                print(" Leafblaster remote is rebooting into new firmware.")
                print("==================================================\n")
            else:
                body = resp.read().decode('utf-8', errors='ignore')
                print(f"Server response ({resp.status}): {body}")
    except urllib.error.HTTPError as e:
        reader.close()
        body = e.read().decode('utf-8', errors='ignore')
        print(f"\n\nHTTP Error {e.code}: {body}")
        sys.exit(1)
    except Exception as e:
        reader.close()
        print(f"\n\nUpload Failed: {e}")
        print("Tip: Check that the remote is on the 'Update' screen and connected to Wi-Fi.")
        sys.exit(1)

if __name__ == '__main__':
    main()
