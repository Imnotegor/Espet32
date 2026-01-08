#!/usr/bin/env python3
"""
Dataset Builder for NeuroPet
Downloads logs from ESP32 and builds training dataset.
"""

import json
import requests
import numpy as np
from pathlib import Path
from dataclasses import dataclass
from typing import List, Optional
import argparse


# Feature names matching ESP32 implementation
FEATURE_NAMES = [
    'hunger', 'energy', 'affection_need', 'trust', 'stress',
    'dt_seconds_norm', 'feed_count_5m_norm', 'pet_count_5m_norm',
    'ignore_time_norm', 'time_of_day_sin', 'time_of_day_cos', 'spam_score_norm'
]

# Action names
ACTION_NAMES = ['sleep', 'idle', 'play', 'ask_food', 'ask_pet', 'happy', 'annoyed', 'sad']

# Input event types
INPUT_TYPES = ['none', 'feed_short', 'feed_long', 'feed_double',
               'pet_short', 'pet_long', 'pet_double', 'ignore']


@dataclass
class LogEntry:
    timestamp: int
    input_event: int
    features: np.ndarray
    action_id: int
    valence: float
    arousal: float
    state_after: dict


def fetch_log(esp_ip: str, timeout: int = 10) -> List[dict]:
    """Fetch event log from ESP32."""
    url = f"http://{esp_ip}/api/log"
    try:
        response = requests.get(url, timeout=timeout)
        response.raise_for_status()
        return response.json()
    except requests.RequestException as e:
        print(f"Error fetching log: {e}")
        return []


def parse_log_entry(entry: dict) -> Optional[LogEntry]:
    """Parse a raw log entry into structured format."""
    try:
        features = entry.get('features', {})
        brain = entry.get('brain', {})
        state = entry.get('state', {})

        feature_array = np.array([
            features.get('hunger', 0),
            features.get('energy', 0),
            features.get('affection', 0),
            features.get('trust', 0),
            features.get('stress', 0),
            features.get('dt', 0),
            features.get('feed_5m', 0),
            features.get('pet_5m', 0),
            features.get('ignore', 0),
            features.get('tod_sin', 0),
            features.get('tod_cos', 0),
            features.get('spam', 0),
        ], dtype=np.float32)

        return LogEntry(
            timestamp=entry.get('ts', 0),
            input_event=entry.get('event', 0),
            features=feature_array,
            action_id=brain.get('action', 0),
            valence=brain.get('valence', 0),
            arousal=brain.get('arousal', 0),
            state_after=state
        )
    except Exception as e:
        print(f"Error parsing entry: {e}")
        return None


def calculate_reward(entry: LogEntry, next_entry: Optional[LogEntry]) -> float:
    """
    Calculate reward for a given interaction.
    Reward-weighted imitation learning approach.
    """
    reward = 0.0
    input_type = INPUT_TYPES[entry.input_event] if entry.input_event < len(INPUT_TYPES) else 'none'
    features = entry.features

    hunger = features[0]
    energy = features[1]
    affection = features[2]
    trust = features[3]
    stress = features[4]
    spam = features[11]

    # Reward for feeding when hungry
    if input_type.startswith('feed'):
        if hunger > 0.5:
            reward += 1.0 * hunger  # More reward for higher hunger
        elif hunger < 0.2:
            reward -= 0.5  # Slight penalty for overfeeding

    # Reward for petting when pet needs affection
    if input_type.startswith('pet'):
        if affection > 0.5:
            reward += 1.0 * affection
        elif affection < 0.15:
            reward -= 0.3  # Slight penalty for over-petting

    # Penalty for ignore when needs are high
    if input_type == 'ignore':
        if hunger > 0.6 or affection > 0.6:
            reward -= 1.0

    # Penalty for spam
    if spam > 0.5:
        reward -= 0.5 * spam

    # Bonus for trust improvement
    if next_entry is not None:
        trust_change = next_entry.state_after.get('trust', trust) - trust
        reward += trust_change * 2.0

    # Stress reduction is good
    if next_entry is not None:
        stress_change = stress - next_entry.state_after.get('stress', stress)
        if stress_change > 0:
            reward += stress_change * 0.5

    return np.clip(reward, -1.0, 1.0)


def determine_target_action(entry: LogEntry) -> int:
    """
    Determine the 'ideal' action based on state (teacher labels).
    This provides ground truth for imitation learning.
    """
    features = entry.features
    hunger = features[0]
    energy = features[1]
    affection = features[2]
    stress = features[4]

    # Priority-based action selection
    if energy < 0.2:
        return 0  # sleep
    if hunger > 0.7:
        return 3  # ask_food
    if affection > 0.6:
        return 4  # ask_pet
    if stress > 0.6:
        return 6  # annoyed
    if hunger < 0.3 and energy > 0.5 and affection < 0.3 and stress < 0.3:
        return 5  # happy
    if energy > 0.6 and stress < 0.4:
        return 2  # play

    return 1  # idle


def determine_target_emotions(entry: LogEntry) -> tuple:
    """Determine target valence and arousal based on state."""
    features = entry.features
    hunger = features[0]
    energy = features[1]
    affection = features[2]
    trust = features[3]
    stress = features[4]

    # Valence: based on needs being met and trust
    valence = trust - 0.5  # Start with trust influence
    valence -= hunger * 0.3  # Hunger decreases valence
    valence -= affection * 0.2  # Unmet affection decreases
    valence -= stress * 0.4  # Stress decreases
    valence = np.clip(valence, -1.0, 1.0)

    # Arousal: based on energy and stress
    arousal = energy * 0.5 + stress * 0.3
    arousal = np.clip(arousal, 0.0, 1.0)

    return valence, arousal


def build_dataset(logs: List[dict], output_path: Path):
    """Build training dataset from logs."""
    entries = [parse_log_entry(e) for e in logs]
    entries = [e for e in entries if e is not None]

    if len(entries) < 2:
        print("Not enough entries to build dataset")
        return

    X = []  # Features
    y_action = []  # Target actions
    y_valence = []  # Target valence
    y_arousal = []  # Target arousal
    weights = []  # Sample weights (based on reward)

    for i, entry in enumerate(entries):
        next_entry = entries[i + 1] if i + 1 < len(entries) else None

        X.append(entry.features)
        y_action.append(determine_target_action(entry))

        valence, arousal = determine_target_emotions(entry)
        y_valence.append(valence)
        y_arousal.append(arousal)

        reward = calculate_reward(entry, next_entry)
        # Convert reward to positive weight (shift and scale)
        weight = (reward + 1.0) / 2.0 + 0.1  # 0.1 to 1.1
        weights.append(weight)

    # Convert to numpy arrays
    X = np.array(X, dtype=np.float32)
    y_action = np.array(y_action, dtype=np.int32)
    y_valence = np.array(y_valence, dtype=np.float32)
    y_arousal = np.array(y_arousal, dtype=np.float32)
    weights = np.array(weights, dtype=np.float32)

    # Save dataset
    np.savez(output_path,
             X=X,
             y_action=y_action,
             y_valence=y_valence,
             y_arousal=y_arousal,
             weights=weights,
             feature_names=FEATURE_NAMES,
             action_names=ACTION_NAMES)

    print(f"Dataset saved: {output_path}")
    print(f"  Samples: {len(X)}")
    print(f"  Features: {X.shape[1]}")
    print(f"  Actions distribution: {np.bincount(y_action, minlength=len(ACTION_NAMES))}")


def main():
    parser = argparse.ArgumentParser(description='Build NeuroPet training dataset')
    parser.add_argument('--ip', default='192.168.4.1', help='ESP32 IP address')
    parser.add_argument('--input', type=Path, help='Input JSON file (instead of fetching)')
    parser.add_argument('--output', type=Path, default=Path('dataset.npz'),
                        help='Output dataset file')
    args = parser.parse_args()

    if args.input:
        # Load from file
        with open(args.input) as f:
            logs = json.load(f)
    else:
        # Fetch from ESP32
        print(f"Fetching logs from {args.ip}...")
        logs = fetch_log(args.ip)

    if logs:
        print(f"Got {len(logs)} log entries")
        build_dataset(logs, args.output)
    else:
        print("No logs to process")


if __name__ == '__main__':
    main()
