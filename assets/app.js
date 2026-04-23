const socket = io();

let ecgEnabled = false;
let pending = false;
let lastRawSamples = null;
let lastFilteredSamples = null;

// ── DOM refs ──────────────────────────────────────────────
const btn = document.getElementById("ecgToggleBtn");
const statusText = document.getElementById("ecgStateText");
const statusDot = document.getElementById("statusDot");
const connDot = document.getElementById("connDot");
const connText = document.getElementById("connText");
const hrVal = document.getElementById("hrVal");
const peakFreqVal = document.getElementById("peakFreqVal");
const sampleCountVal = document.getElementById("sampleCountVal");
const snrVal = document.getElementById("snrVal");

const rawCanvas = document.getElementById("rawCanvas");
const filteredCanvas = document.getElementById("filteredCanvas");
const fftCanvas = document.getElementById("fftCanvas");
const rawCtx = rawCanvas.getContext("2d");
const filteredCtx = filteredCanvas.getContext("2d");
const fftCtx = fftCanvas.getContext("2d");

const rawStatus = document.getElementById("rawStatus");
const filteredStatus = document.getElementById("filteredStatus");
const fftStatus = document.getElementById("fftStatus");

// ── Colours (match CSS vars) ──────────────────────────────
const C = {
    bg: "#080a0d",
    grid: "#141820",
    axis: "#2a3344",
    accent: "#00e5a0",
    accentDim: "rgba(0,229,160,0.07)",
    accentMid: "rgba(0,229,160,0.4)",
    blue: "#00b8d9",
    text: "#dde3ed",
    muted: "#4a5568",
    warn: "#f5a623",
};

const SAMPLE_RATE = 200;
const CANVAS_HEIGHT = 260;

// ── Canvas sizing ─────────────────────────────────────────
function resizeCanvas(canvas) {
    canvas.width = canvas.offsetWidth;
    canvas.height = CANVAS_HEIGHT;
}

function resizeAll() {
    resizeCanvas(rawCanvas);
    resizeCanvas(filteredCanvas);
    resizeCanvas(fftCanvas);
}

window.addEventListener("resize", () => {
    resizeAll();
    // Redraw last data if available
    if (lastRawSamples)
        drawECG(rawCtx, rawCanvas, lastRawSamples, rawStatus, SAMPLE_RATE, "ADC counts");
    if (lastFilteredSamples)
        drawECG(filteredCtx, filteredCanvas, lastFilteredSamples, filteredStatus, SAMPLE_RATE, "Filtered (ADC)");
});

// ── Placeholder ───────────────────────────────────────────
function drawPlaceholder(ctx, canvas, message) {
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.strokeStyle = "#1e2530";
    ctx.lineWidth = 1;
    ctx.setLineDash([5, 5]);
    ctx.strokeRect(4, 4, canvas.width - 8, canvas.height - 8);
    ctx.setLineDash([]);
    ctx.fillStyle = C.muted;
    ctx.font = "13px 'DM Mono', monospace";
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillText(message, canvas.width / 2, canvas.height / 2);
}

// ── Utilities ─────────────────────────────────────────────
function minMax(arr) {
    let min = arr[0], max = arr[0];
    for (let i = 1; i < arr.length; i++) {
        if (arr[i] < min) min = arr[i];
        if (arr[i] > max) max = arr[i];
    }
    return [min, max];
}

function estimateSNR(raw, filtered) {
    if (!raw || !filtered || raw.length !== filtered.length) return null;
    let sigPow = 0, noisePow = 0;
    for (let i = 0; i < raw.length; i++) {
        sigPow += filtered[i] * filtered[i];
        const n = raw[i] - filtered[i];
        noisePow += n * n;
    }
    if (noisePow < 1e-9) return null;
    return (10 * Math.log10(sigPow / noisePow)).toFixed(1);
}

// ── Shared axes ───────────────────────────────────────────
function drawAxes(ctx, lp, tp, pw, ph) {
    ctx.strokeStyle = C.grid;
    ctx.lineWidth = 1;
    for (let i = 0; i <= 5; i++) {
        const y = tp + (i / 5) * ph;
        ctx.beginPath(); ctx.moveTo(lp, y); ctx.lineTo(lp + pw, y); ctx.stroke();
    }
    for (let i = 0; i <= 8; i++) {
        const x = lp + (i / 8) * pw;
        ctx.beginPath(); ctx.moveTo(x, tp); ctx.lineTo(x, tp + ph); ctx.stroke();
    }
    ctx.strokeStyle = C.axis;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(lp, tp);
    ctx.lineTo(lp, tp + ph);
    ctx.lineTo(lp + pw, tp + ph);
    ctx.stroke();
}

// ── ECG draw ──────────────────────────────────────────────
function drawECG(ctx, canvas, samples, statusEl, sampleRateHz = SAMPLE_RATE, yLabel = "ADC counts") {
    if (!Array.isArray(samples) || samples.length === 0) {
        statusEl.textContent = "Empty buffer";
        return;
    }

    statusEl.textContent = `${samples.length} samples @ ${sampleRateHz} Hz`;
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    const lp = 58, rp = 16, tp = 16, bp = 40;
    const pw = canvas.width - lp - rp;
    const ph = canvas.height - tp - bp;

    drawAxes(ctx, lp, tp, pw, ph);

    const [mn, mx] = minMax(samples);
    const rng = Math.max(mx - mn, 1);

    // Signal line with glow
    ctx.save();
    ctx.shadowColor = C.accent;
    ctx.shadowBlur = 5;
    ctx.strokeStyle = C.accent;
    ctx.lineWidth = 1.5;
    ctx.lineJoin = "round";
    ctx.beginPath();
    samples.forEach((v, i) => {
        const x = lp + (i / (samples.length - 1)) * pw;
        const y = tp + ph - ((v - mn) / rng) * ph;
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    });
    ctx.stroke();
    ctx.restore();

    // Tick labels
    ctx.font = "10px 'DM Mono', monospace";
    ctx.fillStyle = C.text;

    // Time axis
    ctx.textAlign = "center";
    ctx.textBaseline = "top";
    const dur = samples.length / sampleRateHz;
    for (let i = 0; i <= 8; i++) {
        const x = lp + (i / 8) * pw;
        ctx.fillText(((i / 8) * dur).toFixed(2) + "s", x, tp + ph + 6);
    }

    // Amplitude axis
    ctx.textAlign = "right";
    ctx.textBaseline = "middle";
    for (let i = 0; i <= 5; i++) {
        const y = tp + ph - (i / 5) * ph;
        ctx.fillText((mn + (i / 5) * rng).toFixed(0), lp - 6, y);
    }

    // Axis labels
    ctx.fillStyle = C.text;
    ctx.font = "10px 'DM Mono', monospace";
    ctx.textAlign = "center";
    ctx.textBaseline = "alphabetic";
    ctx.fillText("Time (s)", lp + pw / 2, canvas.height - 4);

    ctx.save();
    ctx.translate(12, tp + ph / 2);
    ctx.rotate(-Math.PI / 2);
    ctx.textAlign = "center";
    ctx.fillText(yLabel, 0, 0);
    ctx.restore();
}

// ── FFT draw ──────────────────────────────────────────────
function drawFFT(ctx, canvas, samples, sampleRateHz = SAMPLE_RATE) {
    if (!Array.isArray(samples) || samples.length === 0) return;

    ctx.clearRect(0, 0, canvas.width, canvas.height);

    const lp = 58, rp = 16, tp = 16, bp = 40;
    const pw = canvas.width - lp - rp;
    const ph = canvas.height - tp - bp;
    const nyq = sampleRateHz / 2;

    const [, maxMag] = minMax(samples);
    const mx = Math.max(maxMag, 1);

    drawAxes(ctx, lp, tp, pw, ph);

    // Bars
    ctx.strokeStyle = "#4477cc";
    ctx.lineWidth = 1.5;
    ctx.lineJoin = "round";
    ctx.beginPath();
    samples.forEach((v, i) => {
        const x = lp + (i / (samples.length - 1)) * pw;
        const y = tp + ph - (v / mx) * ph;
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    });
    ctx.stroke();

    // Frequency ticks
    ctx.fillStyle = C.text;
    ctx.font = "10px 'DM Mono', monospace";
    ctx.textAlign = "center";
    ctx.textBaseline = "top";
    for (let i = 0; i <= 8; i++) {
        const x = lp + (i / 8) * pw;
        const f = (i / 8) * nyq;
        ctx.fillText(f.toFixed(1), x, tp + ph + 6);
    }

    // Magnitude ticks
    ctx.textAlign = "right";
    ctx.textBaseline = "middle";
    for (let i = 0; i <= 5; i++) {
        const y = tp + ph - (i / 5) * ph;
        ctx.fillText(((i / 5) * mx).toFixed(0), lp - 6, y);
    }

    // Axis labels
    ctx.fillStyle = C.text;
    ctx.textAlign = "center";
    ctx.textBaseline = "alphabetic";
    ctx.fillText("Frequency (Hz)", lp + pw / 2, canvas.height - 4);

    ctx.save();
    ctx.translate(12, tp + ph / 2);
    ctx.rotate(-Math.PI / 2);
    ctx.fillText("Magnitude", 0, 0);
    ctx.restore();
}

// ── UI state ──────────────────────────────────────────────
function updateUI() {
    if (pending) {
        btn.textContent = ecgEnabled ? "Turning Off..." : "Turning On...";
        btn.classList.add("pending");
    } else {
        btn.textContent = ecgEnabled ? "Shut Down ECG" : "Turn ECG On";
        btn.classList.remove("pending");
        btn.className = btn.className.replace("off", "").trim();
        if (ecgEnabled) btn.classList.add("off");
    }
    statusText.textContent = ecgEnabled ? "ECG is ON" : "ECG is OFF";
    statusDot.className = ecgEnabled ? "status-dot on" : "status-dot";
    btn.disabled = pending;
}

function toggleECG() {
    if (pending) return;
    pending = true;
    updateUI();
    socket.emit("set_ecg_enabled", !ecgEnabled);
}

// ── Socket events ─────────────────────────────────────────
socket.on("connect", () => {
    connDot.style.background = "#00e5a0";
    connDot.style.boxShadow = "0 0 6px #00e5a0";
    connText.textContent = "CONNECTED";
});

socket.on("disconnect", () => {
    connDot.style.background = "#4a5568";
    connDot.style.boxShadow = "none";
    connText.textContent = "DISCONNECTED";
});

socket.on("raw_ecg", (data) => {
    lastRawSamples = data;
    sampleCountVal.textContent = data.length;
    drawECG(rawCtx, rawCanvas, data, rawStatus, SAMPLE_RATE, "ADC counts");

    // Update SNR if we have both signals
    if (lastFilteredSamples) {
        const snr = estimateSNR(data, lastFilteredSamples);
        if (snr !== null) snrVal.textContent = `${snr} dB`;
    }
});

socket.on("filtered_ecg", (data) => {
    lastFilteredSamples = data;
    drawECG(filteredCtx, filteredCanvas, data, filteredStatus, SAMPLE_RATE, "Filtered (ADC)");
});

socket.on("fft_ecg", (data) => {
    fftStatus.textContent = `${data.length} bins`;
    drawFFT(fftCtx, fftCanvas, data, SAMPLE_RATE);
});

socket.on("ecg_state", (state) => {
    ecgEnabled = state;
    pending = false;
    updateUI();

    const msg = state ? "Waiting for first buffer…" : "Enable ECG to begin";
    drawPlaceholder(rawCtx, rawCanvas, msg);
    drawPlaceholder(filteredCtx, filteredCanvas, msg);
    drawPlaceholder(fftCtx, fftCanvas, msg);

    if (!state) {
        hrVal.textContent = "—";
        peakFreqVal.textContent = "—";
        sampleCountVal.textContent = "—";
        snrVal.textContent = "—";
        lastRawSamples = null;
        lastFilteredSamples = null;
    }
});

socket.on("heart_rate", (data) => {
    hrVal.textContent = `${data.bpm} BPM`;
    peakFreqVal.textContent = `${data.peak_freq} Hz`;
});

// ── Init ──────────────────────────────────────────────────
resizeAll();
drawPlaceholder(rawCtx, rawCanvas, "Enable ECG to begin");
drawPlaceholder(filteredCtx, filteredCanvas, "Enable ECG to begin");
drawPlaceholder(fftCtx, fftCanvas, "Enable ECG to begin");
updateUI();