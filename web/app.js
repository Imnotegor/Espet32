// NeuroPet Web Interface

const ACTION_NAMES = [
    'Sleeping',
    'Idle',
    'Playing',
    'Hungry!',
    'Wants pets',
    'Happy!',
    'Annoyed',
    'Sad'
];

let ws = null;
let reconnectTimeout = null;

// DOM elements
const elements = {
    wsIndicator: document.getElementById('ws-indicator'),
    wsStatus: document.getElementById('ws-status'),
    firmwareVersion: document.getElementById('firmware-version'),
    modelVersion: document.getElementById('model-version'),
    rgbGlow: document.getElementById('rgb-glow'),
    petBody: document.getElementById('pet-body'),
    eyeLeft: document.getElementById('eye-left'),
    eyeRight: document.getElementById('eye-right'),
    petMouth: document.getElementById('pet-mouth'),
    actionLabel: document.getElementById('action-label'),
    hungerBar: document.getElementById('hunger-bar'),
    hungerValue: document.getElementById('hunger-value'),
    energyBar: document.getElementById('energy-bar'),
    energyValue: document.getElementById('energy-value'),
    affectionBar: document.getElementById('affection-bar'),
    affectionValue: document.getElementById('affection-value'),
    trustBar: document.getElementById('trust-bar'),
    trustValue: document.getElementById('trust-value'),
    stressBar: document.getElementById('stress-bar'),
    stressValue: document.getElementById('stress-value'),
    valenceValue: document.getElementById('valence-value'),
    arousalValue: document.getElementById('arousal-value'),
    logEntries: document.getElementById('log-entries'),
    modelFile: document.getElementById('model-file'),
    modelInfo: document.getElementById('model-info')
};

// WebSocket connection
function connect() {
    const wsUrl = `ws://${window.location.hostname}:81`;
    console.log('Connecting to', wsUrl);

    ws = new WebSocket(wsUrl);

    ws.onopen = () => {
        console.log('WebSocket connected');
        elements.wsIndicator.classList.add('connected');
        elements.wsStatus.textContent = 'Connected';
        addLogEntry('Connected to pet');
    };

    ws.onclose = () => {
        console.log('WebSocket disconnected');
        elements.wsIndicator.classList.remove('connected');
        elements.wsStatus.textContent = 'Disconnected';
        addLogEntry('Connection lost');

        // Reconnect after 2 seconds
        reconnectTimeout = setTimeout(connect, 2000);
    };

    ws.onerror = (error) => {
        console.error('WebSocket error:', error);
    };

    ws.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            handleMessage(data);
        } catch (e) {
            console.error('Parse error:', e);
        }
    };
}

function handleMessage(data) {
    switch (data.type) {
        case 'connected':
            elements.firmwareVersion.textContent = data.firmware || '-';
            break;

        case 'state_update':
            updateState(data);
            break;

        case 'event':
            addLogEntry(`${data.event}: ${data.data || ''}`);
            break;
    }
}

function updateState(data) {
    const { state, brain, rgb } = data;

    // Update stats
    if (state) {
        updateStat('hunger', state.hunger, getHungerColor(state.hunger));
        updateStat('energy', state.energy, getEnergyColor(state.energy));
        updateStat('affection', state.affection, getAffectionColor(state.affection));
        updateStat('trust', state.trust, getTrustColor(state.trust));
        updateStat('stress', state.stress, getStressColor(state.stress));
    }

    // Update brain/emotions
    if (brain) {
        elements.valenceValue.textContent = brain.valence.toFixed(2);
        elements.arousalValue.textContent = brain.arousal.toFixed(2);
        elements.actionLabel.textContent = ACTION_NAMES[brain.action_id] || 'Unknown';

        // Update pet animation based on action
        updatePetAnimation(brain.action_id, brain.valence);
    }

    // Update RGB glow
    if (rgb) {
        elements.rgbGlow.style.background = `rgb(${rgb.r}, ${rgb.g}, ${rgb.b})`;
        elements.petBody.style.background = `rgb(${Math.floor(rgb.r * 0.3 + 40)}, ${Math.floor(rgb.g * 0.3 + 40)}, ${Math.floor(rgb.b * 0.3 + 40)})`;
    }
}

function updateStat(name, value, color) {
    const bar = elements[`${name}Bar`];
    const valueEl = elements[`${name}Value`];

    if (bar && valueEl) {
        const percent = Math.round(value * 100);
        bar.style.width = `${percent}%`;
        bar.style.background = color;
        valueEl.textContent = `${percent}%`;
    }
}

function getHungerColor(value) {
    if (value > 0.7) return '#ff4444';
    if (value > 0.4) return '#ffaa00';
    return '#44ff44';
}

function getEnergyColor(value) {
    if (value < 0.2) return '#ff4444';
    if (value < 0.5) return '#ffaa00';
    return '#44ff44';
}

function getAffectionColor(value) {
    if (value > 0.7) return '#ff44ff';
    if (value > 0.4) return '#aa44ff';
    return '#44aaff';
}

function getTrustColor(value) {
    if (value > 0.7) return '#44ffaa';
    if (value > 0.3) return '#ffaa44';
    return '#ff4444';
}

function getStressColor(value) {
    if (value > 0.6) return '#ff4444';
    if (value > 0.3) return '#ffaa00';
    return '#44ff44';
}

function updatePetAnimation(actionId, valence) {
    // Reset classes
    elements.petBody.classList.remove('sleeping', 'playing');
    elements.eyeLeft.classList.remove('sleeping');
    elements.eyeRight.classList.remove('sleeping');

    // Update mouth based on valence
    if (valence > 0.3) {
        // Happy mouth
        elements.petMouth.style.borderRadius = '0 0 30px 30px';
        elements.petMouth.style.borderTop = 'none';
    } else if (valence < -0.3) {
        // Sad mouth (inverted)
        elements.petMouth.style.borderRadius = '30px 30px 0 0';
        elements.petMouth.style.borderTop = '3px solid white';
        elements.petMouth.style.borderBottom = 'none';
    } else {
        // Neutral
        elements.petMouth.style.borderRadius = '0';
        elements.petMouth.style.border = '3px solid white';
        elements.petMouth.style.borderTop = 'none';
        elements.petMouth.style.borderBottom = 'none';
        elements.petMouth.style.height = '3px';
    }

    // Action-specific animations
    switch (actionId) {
        case 0: // Sleep
            elements.petBody.classList.add('sleeping');
            elements.eyeLeft.classList.add('sleeping');
            elements.eyeRight.classList.add('sleeping');
            break;
        case 2: // Play
        case 5: // Happy
            elements.petBody.classList.add('playing');
            break;
    }
}

function addLogEntry(message) {
    const now = new Date();
    const time = now.toTimeString().split(' ')[0];

    const entry = document.createElement('div');
    entry.className = 'log-entry';
    entry.innerHTML = `<span class="log-time">${time}</span> ${message}`;

    elements.logEntries.insertBefore(entry, elements.logEntries.firstChild);

    // Keep only last 50 entries
    while (elements.logEntries.children.length > 50) {
        elements.logEntries.removeChild(elements.logEntries.lastChild);
    }
}

// Model upload
elements.modelFile.addEventListener('change', async (e) => {
    const file = e.target.files[0];
    if (!file) return;

    addLogEntry(`Uploading model: ${file.name}`);

    try {
        const buffer = await file.arrayBuffer();
        const data = new Uint8Array(buffer);

        // Calculate CRC32
        const crc = crc32(data);

        const formData = new FormData();
        formData.append('model', file);

        const response = await fetch('/api/model', {
            method: 'POST',
            headers: {
                'X-Model-Size': data.length.toString(),
                'X-Model-Version': '1',
                'X-Features-Version': '1',
                'X-Model-CRC': crc.toString(16),
                'X-Model-Created': Math.floor(Date.now() / 1000).toString()
            },
            body: file
        });

        if (response.ok) {
            addLogEntry('Model uploaded successfully!');
            elements.modelInfo.textContent = `Model: ${file.name} (${data.length} bytes)`;
        } else {
            const error = await response.json();
            addLogEntry(`Upload failed: ${error.error}`);
        }
    } catch (err) {
        addLogEntry(`Upload error: ${err.message}`);
    }

    e.target.value = '';
});

// CRC32 calculation
function crc32(data) {
    let crc = 0xFFFFFFFF;
    const table = makeCRC32Table();

    for (let i = 0; i < data.length; i++) {
        crc = (crc >>> 8) ^ table[(crc ^ data[i]) & 0xFF];
    }

    return (crc ^ 0xFFFFFFFF) >>> 0;
}

function makeCRC32Table() {
    const table = new Uint32Array(256);
    for (let i = 0; i < 256; i++) {
        let c = i;
        for (let j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
        }
        table[i] = c;
    }
    return table;
}

// Fetch initial status
async function fetchStatus() {
    try {
        const response = await fetch('/api/status');
        const data = await response.json();
        elements.firmwareVersion.textContent = data.firmware_version || '-';
        updateState({ state: data.state, brain: data.brain });
    } catch (e) {
        console.log('Could not fetch status');
    }
}

// Fetch model metadata
async function fetchModelMeta() {
    try {
        const response = await fetch('/api/model/meta');
        if (response.ok) {
            const meta = await response.json();
            elements.modelVersion.textContent = `v${meta.version}`;
            elements.modelInfo.textContent = `Model v${meta.version} (${meta.size} bytes)`;
        }
    } catch (e) {
        console.log('No model loaded');
    }
}

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    fetchStatus();
    fetchModelMeta();
    connect();
});
