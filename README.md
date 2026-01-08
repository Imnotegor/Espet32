# NeuroPet

[English](#english) | [Русский](#русский)

## English

Virtual pet for ESP32-S3 with a neural "brain" and web UI.

### Hardware

- **Board**: ESP32-S3-DualKey
- **LEDs**: 2x WS2812 (GPIO21 data, GPIO40 power)
- **Buttons**: GPIO17 (feed), GPIO0 (pet)

### Features

#### Pet
- 5 state parameters: hunger, energy, affection, trust, stress
- 8 actions: sleep, rest, play, asks for food, asks for petting, happy, irritated, sad
- Online learning adapts to your interaction style
- Unique color palette per device (based on MAC address)
- Customizable name

#### Interface
- WiFi access point: `NeuroPet` / `petpetpet`
- Web UI: http://192.168.4.1
- 3D voxel pet with animation
- Two languages (RU/EN) based on browser language
- WebSocket updates in real time

#### LEDs
- **LED 0** (left): hunger level (blue → red)
- **LED 1** (right): mood (purple → green)
- Different animations for each action

#### Buttons
- **Short press**: feed / pet
- **Long press**: double portion / double petting
- **Double press**: special action
- **Both buttons 3 sec**: sleep mode (deep sleep)

### Build & Flash

```bash
cd firmware
pio run -t upload
```

### Project Structure

```
firmware/
├── src/
│   ├── main.cpp           # Main loop
│   ├── core_state.cpp     # Pet state simulation
│   ├── brain_infer.cpp    # Neural network (MLP)
│   ├── online_learn.cpp   # Online learning
│   ├── rgb_renderer.cpp   # LED control
│   ├── buttons.cpp        # Button handling
│   ├── web_server.cpp     # HTTP + WebSocket server
│   ├── storage.cpp        # NVS/SPIFFS persistence
│   ├── time_manager.cpp   # Time management
│   ├── sleep_manager.cpp  # Deep sleep
│   ├── pet_identity.cpp   # Unique identity
│   └── logger.cpp         # Event logging
├── include/               # Headers
└── platformio.ini
web/
└── index.html             # Web UI
```

### API

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/status` | Current status |
| GET | `/api/time` | Pet time |
| POST | `/api/time` | Set time `{hour, minute}` |
| GET | `/api/pet` | Pet info |
| POST | `/api/pet/name` | Change name `{name}` |
| GET | `/api/model` | Download model |
| POST | `/api/model` | Upload model |

WebSocket on port 81 sends `state_update` every 300 ms.

### Neural Network

MLP architecture 12→16→10:
- **Inputs**: hunger, energy, affection, trust, stress, time since last interaction, interaction counters, time of day, spam score
- **Outputs**: probabilities of 8 actions + valence + arousal

Online learning adjusts action biases based on owner feedback.

### TODO

- [ ]

## Русский

Виртуальный питомец на ESP32-S3 с нейросетевым "мозгом" и веб-интерфейсом.

### Железо

- **Плата**: ESP32-S3-DualKey
- **Светодиоды**: 2x WS2812 (GPIO21 data, GPIO40 power)
- **Кнопки**: GPIO17 (кормить), GPIO0 (гладить)

### Возможности

#### Питомец
- 5 параметров состояния: голод, энергия, привязанность, доверие, стресс
- 8 действий: сон, отдых, игра, просит еду, просит ласку, счастлив, раздражён, грустит
- Онлайн-обучение — питомец адаптируется к вашему стилю взаимодействия
- Уникальная окраска для каждого устройства (на основе MAC-адреса)
- Настраиваемое имя

#### Интерфейс
- WiFi точка доступа: `NeuroPet` / `petpetpet`
- Веб-интерфейс: http://192.168.4.1
- 3D воксельный питомец с анимацией
- Два языка (RU/EN) автоматически по языку браузера
- WebSocket для обновлений в реальном времени

#### Светодиоды
- **LED 0** (левый): уровень голода (синий → красный)
- **LED 1** (правый): настроение (фиолетовый → зелёный)
- Разные анимации для каждого действия

#### Кнопки
- **Короткое нажатие**: кормить / погладить
- **Длинное нажатие**: двойная порция / двойная ласка
- **Двойное нажатие**: специальное действие
- **Обе кнопки 3 сек**: режим сна (deep sleep)

### Сборка и прошивка

```bash
cd firmware
pio run -t upload
```

### Структура проекта

```
firmware/
├── src/
│   ├── main.cpp           # Основной цикл
│   ├── core_state.cpp     # Симуляция состояния питомца
│   ├── brain_infer.cpp    # Нейросеть (MLP)
│   ├── online_learn.cpp   # Онлайн-обучение
│   ├── rgb_renderer.cpp   # Управление LED
│   ├── buttons.cpp        # Обработка кнопок
│   ├── web_server.cpp     # HTTP + WebSocket сервер
│   ├── storage.cpp        # Сохранение в NVS/SPIFFS
│   ├── time_manager.cpp   # Управление временем
│   ├── sleep_manager.cpp  # Deep sleep
│   ├── pet_identity.cpp   # Уникальная идентичность
│   └── logger.cpp         # Логирование событий
├── include/               # Заголовочные файлы
└── platformio.ini
web/
└── index.html             # Веб-интерфейс
```

### API

| Метод | Путь | Описание |
|-------|------|----------|
| GET | `/api/status` | Текущий статус |
| GET | `/api/time` | Время питомца |
| POST | `/api/time` | Установить время `{hour, minute}` |
| GET | `/api/pet` | Информация о питомце |
| POST | `/api/pet/name` | Изменить имя `{name}` |
| GET | `/api/model` | Скачать модель |
| POST | `/api/model` | Загрузить модель |

WebSocket на порту 81 отправляет `state_update` каждые 300 мс.

### Нейросеть

MLP с архитектурой 12→16→10:
- **Входы**: голод, энергия, привязанность, доверие, стресс, время с последнего взаимодействия, счётчики взаимодействий, время суток, спам-скор
- **Выходы**: вероятности 8 действий + valence + arousal

Онлайн-обучение корректирует смещения действий на основе обратной связи от владельца.

### TODO

- [ ]
