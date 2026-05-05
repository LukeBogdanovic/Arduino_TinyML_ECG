const socket = io();

const filterTypeEl = document.getElementById("filterType");
const filterOrderEl = document.getElementById("filterOrder");
const applyBtn = document.getElementById("applyBtn");
const statusBox = document.getElementById("statusBox");
const activeTypeEl = document.getElementById("activeType");
const activeOrderEl = document.getElementById("activeOrder");
const activeStabilityEl = document.getElementById("activeStability");
const stabilityBadge = document.getElementById("stabilityBadge");
const metaEl = document.getElementById("responseMeta");
const pzMeta = document.getElementById("pzMeta");

const canvas = document.getElementById("responseCanvas");
const ctx = canvas.getContext("2d");
const pzCanvas = document.getElementById("pzCanvas");
const pzCtx = pzCanvas.getContext("2d");

const C = {
    bg: "#080a0d",
    grid: "#141820",
    axis: "#2a3344",
    accent: "#00e5a0",
    blue: "#00b8d9",
    danger: "#e84040",
    muted: "#4a5568",
    text: "#dde3ed",
    passband: "rgba(0, 229, 160, 0.07)",
    passbandLine: "rgba(0, 229, 160, 0.35)",
};

const CANVAS_HEIGHT = 300;

function resizeCanvas() {
    canvas.width = canvas.offsetWidth;
    canvas.height = CANVAS_HEIGHT;
    pzCanvas.width = pzCanvas.offsetWidth;
    pzCanvas.height = CANVAS_HEIGHT;
    console.log("Canvas size:", canvas.width, canvas.height);
}

window.addEventListener("resize", () => {
    resizeCanvas();
    if (lastFreqResponse) drawFrequencyResponse(lastFreqResponse);
    if (lastPoleZero) drawPoleZero(lastPoleZero);
});

function applyFilter() {
    const filterType = filterTypeEl.value;
    const order = parseInt(filterOrderEl.value, 10);

    if (isNaN(order) || order < 1) {
        showStatus("Filter order must be greater than 0", "error");
        return;
    }
    applyBtn.disabled = true;
    applyBtn.textContent = "Applying...";
    showStatus("", "");
    socket.emit("set_filter", { type: filterType, order });
}

function showStatus(message, type) {
    statusBox.textContent = message;
    statusBox.className = `status-box ${type}`;
}

let lastFreqResponse = null;

function drawFrequencyResponse(data) {
    console.log("drawFrequencyResponse called", data);
    lastFreqResponse = data;
    const { frequencies, magnitude_db, lowcut_hz, highcut_hz, nyquist_hz } = data;

    if (!frequencies || !frequencies.length) return;

    ctx.clearRect(0, 0, canvas.width, canvas.height);

    const lp = 58, rp = 16, tp = 20, bp = 40;
    const pw = canvas.width - lp - rp;
    const ph = canvas.height - tp - bp;
    const yMin = -80, yMax = 10;
    const yRange = yMax - yMin;

    ctx.strokeStyle = C.grid;
    ctx.lineWidth = 1;
    const dbLines = [-80, -60, -40, -20, -10, -3, 0];
    dbLines.forEach(db => {
        const y = tp + ph - ((db - yMin) / yRange) * ph;
        ctx.beginPath();
        ctx.moveTo(lp, y);
        ctx.lineTo(lp + pw, y);
        ctx.stroke();
    });
    for (let i = 0; i <= 8; i++) {
        const x = lp + (i / 8) * pw;
        ctx.beginPath();
        ctx.moveTo(x, tp);
        ctx.lineTo(x, tp + ph);
        ctx.stroke();
    }

    const xLow = lp + (lowcut_hz / nyquist_hz) * pw;
    const xHigh = lp + (highcut_hz / nyquist_hz) * pw;
    ctx.fillStyle = C.passband;
    ctx.fillRect(xLow, tp, xHigh - xLow, ph);

    ctx.strokeStyle = C.passbandLine;
    ctx.lineWidth = 1;
    ctx.setLineDash([3, 3]);
    ctx.beginPath();
    ctx.moveTo(xLow, tp);
    ctx.lineTo(xLow, tp + ph);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(xHigh, tp);
    ctx.lineTo(xHigh, tp + ph);
    ctx.stroke();
    ctx.setLineDash([]);

    ctx.fillStyle = C.passbandLine;
    ctx.font = "9px 'DM Mono', monospace";
    ctx.textAlign = "center";
    ctx.textBaseline = "top";
    ctx.fillText("Passband", xLow + (xHigh - xLow) / 2, tp + 4);

    const y3db = tp + ph - ((-3 - yMin) / yRange) * ph;
    ctx.strokeStyle = "rgba(245, 166, 35, 0.4)";
    ctx.lineWidth = 1;
    ctx.setLineDash([4, 4]);
    ctx.beginPath(); ctx.moveTo(lp, y3db); ctx.lineTo(lp + pw, y3db); ctx.stroke();
    ctx.setLineDash([]);
    ctx.fillStyle = "rgba(245, 166, 35, 0.6)";
    ctx.textAlign = "left";
    ctx.textBaseline = "bottom";
    ctx.fillText("-3 dB", lp + 4, y3db - 2);

    ctx.strokeStyle = C.axis;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(lp, tp);
    ctx.lineTo(lp, tp + ph);
    ctx.lineTo(lp + pw, tp + ph);
    ctx.stroke();

    ctx.save();
    ctx.shadowColor = C.accent;
    ctx.shadowBlur = 6;
    ctx.strokeStyle = C.accent;
    ctx.lineWidth = 2;
    ctx.lineJoin = "round";
    ctx.beginPath();

    frequencies.forEach((f, i) => {
        const x = lp + (f / nyquist_hz) * pw;
        const db = Math.max(magnitude_db[i], yMin);
        const y = tp + ph - ((db - yMin) / yRange) * ph;
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    });
    ctx.stroke();
    ctx.restore();

    ctx.font = "10px 'DM Mono', monospace";
    ctx.fillStyle = C.muted;

    ctx.textAlign = "center";
    ctx.textBaseline = "top";
    for (let i = 0; i <= 8; i++) {
        const x = lp + (i / 8) * pw;
        const f = (i / 8) * nyquist_hz;
        ctx.fillText(f.toFixed(0), x, tp + ph + 6);
    }

    ctx.textAlign = "right";
    ctx.textBaseline = "middle";
    dbLines.forEach(db => {
        const y = tp + ph - ((db - yMin) / yRange) * ph;
        ctx.fillText(`${db}`, lp - 6, y);
    });

    ctx.fillStyle = C.muted;
    ctx.textAlign = "center";
    ctx.textBaseline = "alphabetic";
    ctx.fillText("Frequency (Hz)", lp + pw / 2, canvas.height - 4);

    ctx.save();
    ctx.translate(14, tp + ph / 2);
    ctx.rotate(-Math.PI / 2);
    ctx.fillText("Magnitude (dB)", 0, 0);
    ctx.restore();

    metaEl.textContent = `${frequencies.length} frequency points · passband ${lowcut_hz}-${highcut_hz} Hz`;
}

let lastPoleZero = null;

function drawPoleZero(data) {
    lastPoleZero = data;
    const { poles, zeros, stable, max_pole_radius } = data;

    stabilityBadge.textContent = stable ? "STABLE" : "UNSTABLE";
    stabilityBadge.className = stable ? "stability-badge" : "stability-badge unstable";

    activeStabilityEl.textContent = stable ? "Stable" : "unstable";
    activeStabilityEl.className = stable ? "stat-value green" : "stat-value danger";

    const W = pzCanvas.width;
    const H = CANVAS_HEIGHT;
    const cx = W / 2;
    const cy = H / 2;

    const margin = 12;
    const radius = Math.min(cx, cy) - margin;

    pzCtx.clearRect(0, 0, W, H);

    pzCtx.strokeStyle = C.grid;
    pzCtx.lineWidth = 1;
    pzCtx.beginPath();
    pzCtx.moveTo(0, cy);
    pzCtx.lineTo(W, cy);
    pzCtx.stroke();
    pzCtx.beginPath();
    pzCtx.moveTo(cx, 0);
    pzCtx.lineTo(cx, H);
    pzCtx.stroke();

    const circleColor = stable ? "rgba(0, 229, 160, 0.4)" : "rgba(232, 64, 64, 0.4)";
    pzCtx.strokeStyle = circleColor;
    pzCtx.lineWidth = 1.5;
    pzCtx.beginPath();
    pzCtx.arc(cx, cy, radius, 0, 2 * Math.PI);
    pzCtx.stroke();

    pzCtx.font = "9px 'DM Mono', monospace";
    pzCtx.fillStyle = C.muted;

    pzCtx.textAlign = "center";
    pzCtx.textBaseline = "top";
    pzCtx.fillText("1", cx + radius, cy + 4);
    pzCtx.fillText("-1", cx - radius, cy + 4);
    pzCtx.fillText("Re", cx + radius - 4, cy + 14);

    pzCtx.textAlign = "right";
    pzCtx.textBaseline = "middle";
    pzCtx.fillText("j", cx - 4, cy - radius);
    pzCtx.fillText("-j", cx - 4, cy + radius);
    pzCtx.fillText("Im", cx - 4, cy - radius + 14);

    const toCanvas = (re, img) => ({
        x: cx + re * radius,
        y: cy - img * radius,
    });

    zeros.forEach(z => {
        const { x, y } = toCanvas(z.re, z.img);
        console.log("zero at canvas coords:", x, y, "from complex:", z.re, z.im);
        pzCtx.strokeStyle = C.blue;
        pzCtx.lineWidth = 1.5;
        pzCtx.beginPath();
        pzCtx.arc(x, y, 5, 0, 2 * Math.PI);
        pzCtx.stroke();
    });

    poles.forEach(p => {
        const { x, y } = toCanvas(p.re, p.img);
        console.log("pole at canvas coords:", x, y, "from complex:", p.re, p.im);
        const r = Math.sqrt(p.re ** 2 + p.im ** 2);
        const inside = r < 1.0;
        pzCtx.strokeStyle = inside ? C.accent : C.danger;
        pzCtx.lineWidth = 2;
        const s = 5;
        pzCtx.beginPath();
        pzCtx.moveTo(x - s, y - s); pzCtx.lineTo(x + s, y + s);
        pzCtx.moveTo(x + s, y - s); pzCtx.lineTo(x - s, y + s);
        pzCtx.stroke();
    });

    const lx = margin;
    const ly = H - 20;
    pzCtx.font = "10px 'DM Mono', monospace";

    pzCtx.strokeStyle = C.blue;
    pzCtx.lineWidth = 1.5;
    pzCtx.beginPath();
    pzCtx.arc(lx, ly, 4, 0, 2 * Math.PI);
    pzCtx.stroke();
    pzCtx.fillStyle = C.muted;
    pzCtx.textAlign = "left";
    pzCtx.textBaseline = "middle";
    pzCtx.fillText("Zero", lx + 10, ly);

    pzCtx.strokeStyle = C.accent;
    pzCtx.lineWidth = 2;
    const px = lx + 70;
    pzCtx.beginPath();
    pzCtx.moveTo(px - 4, ly - 4); pzCtx.lineTo(px + 4, ly + 4);
    pzCtx.moveTo(px + 4, ly - 4); pzCtx.lineTo(px - 4, ly + 4);
    pzCtx.stroke();
    pzCtx.fillStyle = C.muted;
    pzCtx.fillText("Pole", px + 10, ly);

    const stabilityMargin = (1.0 - max_pole_radius).toFixed(4);
    pzMeta.textContent =
        `${poles.length} poles · ${zeros.length} zeros · `
        + `stability margin: ${stabilityMargin} `
        + `(max |pole| = ${max_pole_radius.toFixed(4)})`;
}

socket.on("connect", () => {
    console.log("Socket connected, requesting filter state");
    socket.emit("get_filter", {});
});

socket.on("filter_state", (data) => {
    console.log("filter state received", data);
    applyBtn.disabled = false;
    applyBtn.textContent = "Apply Filter";

    activeTypeEl.textContent = data.type.charAt(0).toUpperCase() + data.type.slice(1);
    activeOrderEl.textContent = data.order;

    filterTypeEl.value = data.type;
    filterOrderEl.value = data.order;

    if (data.freq_response) {
        drawFrequencyResponse(data.freq_response);
    }
    if (data.pole_zero) {
        drawPoleZero(data.pole_zero);
    }

    showStatus(`Filter applied: ${data.type} order ${data.order}`, "success");
});

socket.on("filter_error", (data) => {
    applyBtn.disabled = false;
    applyBtn.textContent = "Apply Filter";
    showStatus(data.message, "error");
});

resizeCanvas();