#!/usr/bin/env python3
"""
Export trained model to ESP32 binary format.
Optionally quantize to int8.
"""

import numpy as np
import torch
import struct
from pathlib import Path
import argparse
from datetime import datetime
import zlib

# Architecture constants (must match ESP32)
INPUT_SIZE = 12
HIDDEN_SIZE = 16
OUTPUT_SIZE = 10


def load_model(model_path: Path) -> dict:
    """Load PyTorch model checkpoint."""
    checkpoint = torch.load(model_path, map_location='cpu', weights_only=False)
    return checkpoint['model_state_dict']


def extract_weights(state_dict: dict) -> tuple:
    """Extract weights as numpy arrays."""
    w1 = state_dict['fc1.weight'].numpy()  # [HIDDEN_SIZE, INPUT_SIZE]
    b1 = state_dict['fc1.bias'].numpy()    # [HIDDEN_SIZE]
    w2 = state_dict['fc2.weight'].numpy()  # [OUTPUT_SIZE, HIDDEN_SIZE]
    b2 = state_dict['fc2.bias'].numpy()    # [OUTPUT_SIZE]

    # Transpose to match ESP32 layout [input, hidden]
    w1 = w1.T  # [INPUT_SIZE, HIDDEN_SIZE]
    w2 = w2.T  # [HIDDEN_SIZE, OUTPUT_SIZE]

    return w1, b1, w2, b2


def pack_float32(w1, b1, w2, b2, version: int) -> bytes:
    """Pack weights as float32 binary."""
    data = bytearray()

    # Version (4 bytes)
    data.extend(struct.pack('<I', version))

    # w1: [INPUT_SIZE, HIDDEN_SIZE]
    for i in range(INPUT_SIZE):
        for j in range(HIDDEN_SIZE):
            data.extend(struct.pack('<f', w1[i, j]))

    # b1: [HIDDEN_SIZE]
    for j in range(HIDDEN_SIZE):
        data.extend(struct.pack('<f', b1[j]))

    # w2: [HIDDEN_SIZE, OUTPUT_SIZE]
    for i in range(HIDDEN_SIZE):
        for j in range(OUTPUT_SIZE):
            data.extend(struct.pack('<f', w2[i, j]))

    # b2: [OUTPUT_SIZE]
    for j in range(OUTPUT_SIZE):
        data.extend(struct.pack('<f', b2[j]))

    return bytes(data)


def quantize_int8(weights: np.ndarray) -> tuple:
    """Quantize weights to int8 with scale factor."""
    w_min = weights.min()
    w_max = weights.max()
    scale = max(abs(w_min), abs(w_max)) / 127.0
    if scale == 0:
        scale = 1.0
    quantized = np.clip(np.round(weights / scale), -127, 127).astype(np.int8)
    return quantized, scale


def pack_int8(w1, b1, w2, b2, version: int) -> bytes:
    """Pack weights as int8 with scale factors."""
    data = bytearray()

    # Version (4 bytes)
    data.extend(struct.pack('<I', version))

    # Quantization flag
    data.extend(struct.pack('<B', 1))  # 1 = int8 quantized

    # Quantize each weight matrix
    w1_q, w1_scale = quantize_int8(w1)
    b1_q, b1_scale = quantize_int8(b1)
    w2_q, w2_scale = quantize_int8(w2)
    b2_q, b2_scale = quantize_int8(b2)

    # Pack scales (4 floats)
    data.extend(struct.pack('<f', w1_scale))
    data.extend(struct.pack('<f', b1_scale))
    data.extend(struct.pack('<f', w2_scale))
    data.extend(struct.pack('<f', b2_scale))

    # Pack quantized weights
    data.extend(w1_q.tobytes())
    data.extend(b1_q.tobytes())
    data.extend(w2_q.tobytes())
    data.extend(b2_q.tobytes())

    return bytes(data)


def calculate_crc32(data: bytes) -> int:
    """Calculate CRC32 checksum."""
    return zlib.crc32(data) & 0xFFFFFFFF


def export_model(model_path: Path, output_path: Path, version: int,
                 quantize: bool = False):
    """Export model to binary format."""
    print(f"Loading model from {model_path}")
    state_dict = load_model(model_path)

    w1, b1, w2, b2 = extract_weights(state_dict)

    print(f"Weights shapes:")
    print(f"  w1: {w1.shape}")
    print(f"  b1: {b1.shape}")
    print(f"  w2: {w2.shape}")
    print(f"  b2: {b2.shape}")

    if quantize:
        print("Quantizing to int8...")
        data = pack_int8(w1, b1, w2, b2, version)
    else:
        print("Using float32...")
        data = pack_float32(w1, b1, w2, b2, version)

    crc = calculate_crc32(data)

    with open(output_path, 'wb') as f:
        f.write(data)

    print(f"\nExported to: {output_path}")
    print(f"  Size: {len(data)} bytes")
    print(f"  Version: {version}")
    print(f"  CRC32: {crc:08X}")
    print(f"  Quantized: {quantize}")

    # Also save metadata as JSON
    import json
    meta_path = output_path.with_suffix('.json')
    meta = {
        'version': version,
        'features_version': 1,
        'size': len(data),
        'crc32': crc,
        'quantized': quantize,
        'created_at': int(datetime.now().timestamp()),
        'input_size': INPUT_SIZE,
        'hidden_size': HIDDEN_SIZE,
        'output_size': OUTPUT_SIZE
    }
    with open(meta_path, 'w') as f:
        json.dump(meta, f, indent=2)
    print(f"  Metadata: {meta_path}")

    return crc


def main():
    parser = argparse.ArgumentParser(description='Export NeuroPet model')
    parser.add_argument('model', type=Path, help='Input model file (.pt)')
    parser.add_argument('--output', type=Path, default=Path('model.bin'),
                        help='Output binary file')
    parser.add_argument('--version', type=int, default=1,
                        help='Model version number')
    parser.add_argument('--quantize', action='store_true',
                        help='Quantize to int8')
    args = parser.parse_args()

    if not args.model.exists():
        print(f"Model not found: {args.model}")
        return 1

    export_model(args.model, args.output, args.version, args.quantize)
    return 0


if __name__ == '__main__':
    exit(main())
