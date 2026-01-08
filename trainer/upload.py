#!/usr/bin/env python3
"""
Upload trained model to ESP32 via HTTP.
"""

import requests
import json
from pathlib import Path
import argparse
import time


def load_metadata(meta_path: Path) -> dict:
    """Load model metadata."""
    with open(meta_path) as f:
        return json.load(f)


def upload_model(esp_ip: str, model_path: Path, meta_path: Path = None,
                 timeout: int = 30) -> bool:
    """Upload model to ESP32."""
    url = f"http://{esp_ip}/api/model"

    # Load model binary
    with open(model_path, 'rb') as f:
        model_data = f.read()

    print(f"Model size: {len(model_data)} bytes")

    # Load or generate metadata
    if meta_path and meta_path.exists():
        meta = load_metadata(meta_path)
    else:
        # Try adjacent .json file
        auto_meta = model_path.with_suffix('.json')
        if auto_meta.exists():
            meta = load_metadata(auto_meta)
        else:
            print("Warning: No metadata file, using defaults")
            import zlib
            meta = {
                'version': 1,
                'features_version': 1,
                'size': len(model_data),
                'crc32': zlib.crc32(model_data) & 0xFFFFFFFF,
                'created_at': int(time.time())
            }

    print(f"Metadata: version={meta['version']}, crc32={meta['crc32']:08X}")

    headers = {
        'Content-Type': 'application/octet-stream',
        'X-Model-Size': str(meta['size']),
        'X-Model-Version': str(meta['version']),
        'X-Features-Version': str(meta.get('features_version', 1)),
        'X-Model-CRC': f"{meta['crc32']:08X}",
        'X-Model-Created': str(meta.get('created_at', int(time.time())))
    }

    print(f"Uploading to {url}...")

    try:
        response = requests.post(url, data=model_data, headers=headers,
                                 timeout=timeout)

        if response.status_code == 200:
            print("Upload successful!")
            result = response.json()
            print(f"Response: {result}")
            return True
        else:
            print(f"Upload failed: {response.status_code}")
            print(f"Response: {response.text}")
            return False

    except requests.RequestException as e:
        print(f"Upload error: {e}")
        return False


def check_status(esp_ip: str, timeout: int = 5) -> dict:
    """Check ESP32 status."""
    url = f"http://{esp_ip}/api/status"
    try:
        response = requests.get(url, timeout=timeout)
        return response.json()
    except:
        return None


def check_model(esp_ip: str, timeout: int = 5) -> dict:
    """Check current model on ESP32."""
    url = f"http://{esp_ip}/api/model/meta"
    try:
        response = requests.get(url, timeout=timeout)
        if response.status_code == 200:
            return response.json()
        return None
    except:
        return None


def main():
    parser = argparse.ArgumentParser(description='Upload NeuroPet model to ESP32')
    parser.add_argument('model', type=Path, nargs='?',
                        help='Model binary file (.bin)')
    parser.add_argument('--ip', default='192.168.4.1',
                        help='ESP32 IP address (default: 192.168.4.1)')
    parser.add_argument('--meta', type=Path,
                        help='Metadata JSON file')
    parser.add_argument('--status', action='store_true',
                        help='Check ESP32 status only')
    parser.add_argument('--check-model', action='store_true',
                        help='Check current model only')
    args = parser.parse_args()

    if args.status:
        print(f"Checking status of {args.ip}...")
        status = check_status(args.ip)
        if status:
            print(json.dumps(status, indent=2))
        else:
            print("Could not connect to ESP32")
        return

    if args.check_model:
        print(f"Checking model on {args.ip}...")
        model_info = check_model(args.ip)
        if model_info:
            print(json.dumps(model_info, indent=2))
        else:
            print("No model or could not connect")
        return

    if not args.model:
        parser.print_help()
        print("\nError: model file required for upload")
        return 1

    if not args.model.exists():
        print(f"Model file not found: {args.model}")
        return 1

    # First check connection
    print(f"Connecting to ESP32 at {args.ip}...")
    status = check_status(args.ip)
    if status:
        print(f"Connected! Firmware: {status.get('firmware_version', 'unknown')}")
    else:
        print("Warning: Could not verify connection, attempting upload anyway...")

    # Upload
    success = upload_model(args.ip, args.model, args.meta)

    if success:
        print("\nVerifying upload...")
        time.sleep(1)
        model_info = check_model(args.ip)
        if model_info:
            print(f"Model on device: v{model_info.get('version', '?')}")
        return 0
    else:
        return 1


if __name__ == '__main__':
    exit(main())
