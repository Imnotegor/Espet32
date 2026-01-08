#!/usr/bin/env python3
"""
Trainer for NeuroPet MLP model.
Uses reward-weighted imitation learning.
"""

import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader
from pathlib import Path
import argparse
from datetime import datetime


# Model architecture (must match ESP32 implementation)
INPUT_SIZE = 12
HIDDEN_SIZE = 16
ACTION_COUNT = 8
OUTPUT_SIZE = 10  # 8 actions + valence + arousal


class NeuroPetDataset(Dataset):
    """PyTorch dataset for NeuroPet training data."""

    def __init__(self, npz_path: Path):
        data = np.load(npz_path)
        self.X = torch.from_numpy(data['X'])
        self.y_action = torch.from_numpy(data['y_action']).long()
        self.y_valence = torch.from_numpy(data['y_valence'])
        self.y_arousal = torch.from_numpy(data['y_arousal'])
        self.weights = torch.from_numpy(data['weights'])

    def __len__(self):
        return len(self.X)

    def __getitem__(self, idx):
        return (self.X[idx], self.y_action[idx], self.y_valence[idx],
                self.y_arousal[idx], self.weights[idx])


class NeuroPetMLP(nn.Module):
    """Simple MLP matching ESP32 architecture."""

    def __init__(self):
        super().__init__()
        self.fc1 = nn.Linear(INPUT_SIZE, HIDDEN_SIZE)
        self.fc2 = nn.Linear(HIDDEN_SIZE, OUTPUT_SIZE)
        self.relu = nn.ReLU()

    def forward(self, x):
        x = self.relu(self.fc1(x))
        x = self.fc2(x)

        # Split output
        action_logits = x[:, :ACTION_COUNT]
        valence = torch.tanh(x[:, ACTION_COUNT])
        arousal = torch.sigmoid(x[:, ACTION_COUNT + 1])

        return action_logits, valence, arousal


def train_epoch(model, loader, optimizer, device):
    """Train for one epoch."""
    model.train()
    total_loss = 0
    action_correct = 0
    total_samples = 0

    criterion_action = nn.CrossEntropyLoss(reduction='none')
    criterion_emotion = nn.MSELoss(reduction='none')

    for X, y_action, y_valence, y_arousal, weights in loader:
        X = X.to(device)
        y_action = y_action.to(device)
        y_valence = y_valence.to(device)
        y_arousal = y_arousal.to(device)
        weights = weights.to(device)

        optimizer.zero_grad()

        action_logits, valence, arousal = model(X)

        # Action loss (weighted cross-entropy)
        action_loss = criterion_action(action_logits, y_action)
        action_loss = (action_loss * weights).mean()

        # Emotion losses
        valence_loss = criterion_emotion(valence, y_valence)
        valence_loss = (valence_loss * weights).mean()

        arousal_loss = criterion_emotion(arousal, y_arousal)
        arousal_loss = (arousal_loss * weights).mean()

        # Combined loss
        loss = action_loss + 0.5 * valence_loss + 0.5 * arousal_loss
        loss.backward()
        optimizer.step()

        total_loss += loss.item() * len(X)

        # Accuracy
        pred = action_logits.argmax(dim=1)
        action_correct += (pred == y_action).sum().item()
        total_samples += len(X)

    return total_loss / total_samples, action_correct / total_samples


def evaluate(model, loader, device):
    """Evaluate model."""
    model.eval()
    action_correct = 0
    total_samples = 0
    valence_error = 0
    arousal_error = 0

    with torch.no_grad():
        for X, y_action, y_valence, y_arousal, _ in loader:
            X = X.to(device)
            y_action = y_action.to(device)
            y_valence = y_valence.to(device)
            y_arousal = y_arousal.to(device)

            action_logits, valence, arousal = model(X)

            pred = action_logits.argmax(dim=1)
            action_correct += (pred == y_action).sum().item()
            total_samples += len(X)

            valence_error += ((valence - y_valence) ** 2).sum().item()
            arousal_error += ((arousal - y_arousal) ** 2).sum().item()

    acc = action_correct / total_samples
    val_rmse = np.sqrt(valence_error / total_samples)
    aro_rmse = np.sqrt(arousal_error / total_samples)

    return acc, val_rmse, aro_rmse


def generate_synthetic_data(n_samples: int = 1000) -> Path:
    """Generate synthetic training data for initial model."""
    print(f"Generating {n_samples} synthetic samples...")

    X = np.random.rand(n_samples, INPUT_SIZE).astype(np.float32)
    y_action = np.zeros(n_samples, dtype=np.int32)
    y_valence = np.zeros(n_samples, dtype=np.float32)
    y_arousal = np.zeros(n_samples, dtype=np.float32)
    weights = np.ones(n_samples, dtype=np.float32)

    for i in range(n_samples):
        hunger = X[i, 0]
        energy = X[i, 1]
        affection = X[i, 2]
        trust = X[i, 3]
        stress = X[i, 4]

        # Generate realistic time of day (cyclical encoding)
        # More samples at typical owner interaction times (morning, evening)
        if np.random.rand() < 0.3:
            hour = np.random.choice([7, 8, 9, 18, 19, 20, 21])  # Typical interaction times
        else:
            hour = np.random.randint(0, 24)

        hour_angle = hour / 24.0 * 2 * np.pi
        X[i, 9] = np.sin(hour_angle)   # time_of_day_sin
        X[i, 10] = np.cos(hour_angle)  # time_of_day_cos

        # Determine if it's night (22:00 - 07:00)
        is_night = hour >= 22 or hour < 7

        # Determine action (with time awareness)
        if is_night and energy < 0.5:
            action = 0  # sleep - prefer sleep at night
        elif energy < 0.15:
            action = 0  # sleep - very tired
        elif is_night:
            # At night, prefer calm activities
            if hunger > 0.8:
                action = 3  # ask_food (only if very hungry)
            elif np.random.rand() < 0.6:
                action = 0  # sleep
            else:
                action = 1  # idle
        elif hunger > 0.7:
            action = 3  # ask_food
        elif affection > 0.6:
            action = 4  # ask_pet
        elif stress > 0.6:
            action = 6  # annoyed
        elif hunger < 0.3 and energy > 0.5 and affection < 0.3 and stress < 0.3:
            action = 5  # happy
        elif energy > 0.6 and stress < 0.4:
            action = 2  # play
        else:
            action = 1  # idle

        y_action[i] = action

        # Determine emotions (with time influence)
        valence = trust - 0.5 - hunger * 0.3 - affection * 0.2 - stress * 0.4
        if is_night:
            valence -= 0.1  # Slightly lower mood at night
        y_valence[i] = np.clip(valence, -1.0, 1.0)

        arousal = energy * 0.5 + stress * 0.3
        if is_night:
            arousal *= 0.7  # Lower arousal at night
        y_arousal[i] = np.clip(arousal, 0.0, 1.0)

        # Add some noise
        y_valence[i] += np.random.randn() * 0.1
        y_arousal[i] += np.random.randn() * 0.1
        y_valence[i] = np.clip(y_valence[i], -1.0, 1.0)
        y_arousal[i] = np.clip(y_arousal[i], 0.0, 1.0)

    output_path = Path('synthetic_dataset.npz')
    np.savez(output_path,
             X=X, y_action=y_action, y_valence=y_valence,
             y_arousal=y_arousal, weights=weights)

    return output_path


def main():
    parser = argparse.ArgumentParser(description='Train NeuroPet model')
    parser.add_argument('--dataset', type=Path, help='Dataset file (.npz)')
    parser.add_argument('--synthetic', type=int, default=0,
                        help='Generate N synthetic samples if no dataset')
    parser.add_argument('--epochs', type=int, default=100)
    parser.add_argument('--lr', type=float, default=0.001)
    parser.add_argument('--batch-size', type=int, default=32)
    parser.add_argument('--output', type=Path, default=Path('model.pt'))
    args = parser.parse_args()

    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    print(f"Using device: {device}")

    # Load or generate dataset
    if args.dataset and args.dataset.exists():
        dataset_path = args.dataset
    elif args.synthetic > 0:
        dataset_path = generate_synthetic_data(args.synthetic)
    else:
        print("Generating default synthetic dataset...")
        dataset_path = generate_synthetic_data(2000)

    dataset = NeuroPetDataset(dataset_path)
    print(f"Dataset: {len(dataset)} samples")

    # Split train/val
    train_size = int(0.8 * len(dataset))
    val_size = len(dataset) - train_size
    train_dataset, val_dataset = torch.utils.data.random_split(
        dataset, [train_size, val_size])

    train_loader = DataLoader(train_dataset, batch_size=args.batch_size, shuffle=True)
    val_loader = DataLoader(val_dataset, batch_size=args.batch_size)

    # Create model
    model = NeuroPetMLP().to(device)
    optimizer = optim.Adam(model.parameters(), lr=args.lr)
    scheduler = optim.lr_scheduler.ReduceLROnPlateau(optimizer, patience=10, factor=0.5)

    print(f"\nTraining for {args.epochs} epochs...")
    best_acc = 0

    for epoch in range(args.epochs):
        train_loss, train_acc = train_epoch(model, train_loader, optimizer, device)
        val_acc, val_rmse, aro_rmse = evaluate(model, val_loader, device)

        scheduler.step(val_acc)

        if (epoch + 1) % 10 == 0 or val_acc > best_acc:
            print(f"Epoch {epoch+1:3d}: loss={train_loss:.4f} "
                  f"train_acc={train_acc:.3f} val_acc={val_acc:.3f} "
                  f"val_rmse={val_rmse:.3f} aro_rmse={aro_rmse:.3f}")

        if val_acc > best_acc:
            best_acc = val_acc
            torch.save({
                'model_state_dict': model.state_dict(),
                'epoch': epoch,
                'accuracy': val_acc,
                'timestamp': datetime.now().isoformat()
            }, args.output)

    print(f"\nTraining complete. Best accuracy: {best_acc:.3f}")
    print(f"Model saved to: {args.output}")


if __name__ == '__main__':
    main()
