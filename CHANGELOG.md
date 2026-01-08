# Changelog

## 2026-01-08

### Сессия: Финальная подготовка к продакшену

**Выполнено:**

1. **Удалены LLM-комментарии из кода**
   - `main.cpp` — убраны секционные разделители
   - `rgb_renderer.cpp` — убраны секционные разделители

2. **Создан README.md**
   - Описание железа и пинов
   - Возможности питомца
   - API endpoints
   - Структура проекта
   - Архитектура нейросети

3. **Предыдущие изменения в этой сессии:**
   - Добавлена i18n поддержка (RU/EN) на основе языка браузера
   - Убран HWID из интерфейса (выглядел некрасиво)
   - Убрана версия "v 1.0.0" из статус-бара
   - Улучшена стабильность WebSocket (heartbeat, reconnect)
   - Очищен UI от технических данных (emotion badge, log section)

**Статус сборки:**
```
ESP32-S3: SUCCESS
RAM:   18.1% (59,280 / 327,680 bytes)
Flash: 25.6% (855,249 / 3,342,336 bytes)
```

**Управление устройством:**
- WiFi AP: `NeuroPet` / `petpetpet`
- Web UI: http://192.168.4.1
- Сон: зажать обе кнопки 3 сек
- Пробуждение: любая кнопка

**Файлы проекта:**
```
firmware/src/
├── main.cpp
├── core_state.cpp
├── brain_infer.cpp
├── online_learn.cpp
├── rgb_renderer.cpp
├── buttons.cpp
├── web_server.cpp
├── storage.cpp
├── time_manager.cpp
├── sleep_manager.cpp
├── pet_identity.cpp
├── logger.cpp
└── web_content.cpp (auto-generated)

web/
└── index.html
```
