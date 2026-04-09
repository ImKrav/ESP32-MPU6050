/**
 * @file web_server.c
 * @brief Servidor HTTP + WebSocket con interfaz web embebida para el péndulo de torsión
 *
 * Incluye una interfaz web premium con:
 * - WebSocket para datos en tiempo real a ~50 Hz (push desde ESP32)
 * - Gráfica en tiempo real de ω(t) (velocidad angular) y θ(t) (ángulo)
 * - Indicador gauge de velocidad angular
 * - Panel de parámetros del MAS (período, frecuencia, ωₙ)
 * - Visualización del ángulo acumulado por integración
 * - Diseño oscuro con gradientes y animaciones
 *
 * Arquitectura de datos:
 * - Una tarea FreeRTOS ("ws_push_task") lee el MPU6050 a 50 Hz
 *   y empuja cada muestra por WebSocket a todos los clientes conectados.
 * - Los endpoints REST (POST /reset, POST /calibrate) se mantienen.
 */

#include "web_server.h"
#include "mpu6050.h"
#include "wifi_softap.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WEB_SRV";
static httpd_handle_t s_server = NULL;

/* ── WebSocket ── */
#define WS_MAX_CLIENTS    4       /**< Máximo de clientes WebSocket simultáneos */
#define WS_PUSH_RATE_MS   20      /**< Intervalo de envío: 20 ms → 50 Hz */

static int s_ws_fds[WS_MAX_CLIENTS];          /**< File descriptors de clientes WS */
static int s_ws_count = 0;                     /**< Número de clientes WS activos */
static TaskHandle_t s_ws_task_handle = NULL;    /**< Handle de la tarea push */

/* ══════════════════════════════════════════════════════════════
 *  HTML/CSS/JS Embebido — Interfaz Web Premium con WebSocket
 * ══════════════════════════════════════════════════════════════ */

static const char MAIN_PAGE_HTML[] =
"<!DOCTYPE html>"
"<html lang='es'>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Péndulo de Torsión — Giroscopio Eje Z</title>"
"<style>"
/* ── Reset & Base ── */
"*{margin:0;padding:0;box-sizing:border-box}"
"body{"
    "font-family:'Segoe UI',system-ui,-apple-system,sans-serif;"
    "background:#0a0a1a;"
    "color:#e0e0f0;"
    "min-height:100vh;"
    "overflow-x:hidden;"
"}"

/* ── Background animado ── */
"body::before{"
    "content:'';"
    "position:fixed;top:0;left:0;width:100%;height:100%;"
    "background:radial-gradient(ellipse at 20% 50%,rgba(40,80,180,0.12) 0%,transparent 50%),"
               "radial-gradient(ellipse at 80% 20%,rgba(120,40,200,0.08) 0%,transparent 50%),"
               "radial-gradient(ellipse at 50% 80%,rgba(0,180,220,0.06) 0%,transparent 50%);"
    "z-index:-1;"
"}"

/* ── Header ── */
".header{"
    "text-align:center;padding:24px 16px 16px;"
    "background:linear-gradient(180deg,rgba(20,20,50,0.9) 0%,transparent 100%);"
"}"
".header h1{"
    "font-size:1.6em;font-weight:700;"
    "background:linear-gradient(135deg,#60a5fa,#a78bfa,#f472b6);"
    "-webkit-background-clip:text;-webkit-text-fill-color:transparent;"
    "background-clip:text;"
"}"
".header .subtitle{"
    "font-size:0.85em;color:#8888aa;margin-top:4px;"
    "letter-spacing:0.5px;"
"}"

/* ── Dashboard Grid ── */
".dashboard{"
    "display:grid;"
    "grid-template-columns:1fr 1fr;"
    "gap:16px;"
    "padding:16px;"
    "max-width:960px;margin:0 auto;"
"}"
"@media(max-width:640px){.dashboard{grid-template-columns:1fr;gap:12px;padding:12px}}"

/* ── Cards ── */
".card{"
    "background:linear-gradient(145deg,rgba(25,25,60,0.85),rgba(15,15,40,0.95));"
    "border:1px solid rgba(100,100,200,0.15);"
    "border-radius:16px;padding:20px;"
    "backdrop-filter:blur(12px);"
    "transition:border-color 0.3s,box-shadow 0.3s;"
"}"
".card:hover{"
    "border-color:rgba(100,140,255,0.3);"
    "box-shadow:0 0 30px rgba(80,120,255,0.08);"
"}"
".card-title{"
    "font-size:0.75em;text-transform:uppercase;letter-spacing:1.5px;"
    "color:#7777aa;margin-bottom:12px;"
    "display:flex;align-items:center;gap:6px;"
"}"
".card-title .icon{font-size:1.1em}"

/* ── Valor principal grande ── */
".big-value{"
    "font-size:2.6em;font-weight:800;"
    "font-variant-numeric:tabular-nums;"
    "line-height:1;"
    "transition:color 0.2s;"
"}"
".big-value.positive{color:#60d9a8}"
".big-value.negative{color:#f472b6}"
".big-value.zero{color:#a0a0c0}"
".unit{font-size:0.35em;font-weight:400;color:#7777aa;margin-left:4px}"

/* ── Card span completo ── */
".full-width{grid-column:1/-1}"

/* ── Canvas de gráfica ── */
".chart-container{"
    "position:relative;width:100%;height:200px;"
    "margin-top:8px;"
"}"
".chart-container canvas{"
    "width:100%!important;height:100%!important;"
    "border-radius:8px;"
"}"

/* ── Chart tabs ── */
".chart-tabs{"
    "display:flex;gap:8px;margin-bottom:8px;"
"}"
".chart-tab{"
    "padding:5px 14px;border-radius:6px;font-size:0.75em;"
    "cursor:pointer;border:1px solid rgba(100,100,200,0.2);"
    "background:transparent;color:#8888bb;transition:all 0.2s;"
    "letter-spacing:0.5px;"
"}"
".chart-tab.active{"
    "background:rgba(96,165,250,0.15);border-color:rgba(96,165,250,0.4);"
    "color:#60a5fa;"
"}"
".chart-tab:hover:not(.active){"
    "background:rgba(100,100,200,0.1);color:#aaaacc;"
"}"

/* ── Gauge SVG ── */
".gauge-container{"
    "display:flex;flex-direction:column;align-items:center;"
    "justify-content:center;padding:10px 0;"
"}"
".gauge-svg{width:180px;height:110px}"
".gauge-label{"
    "font-size:0.8em;color:#8888aa;margin-top:8px;"
    "text-align:center;"
"}"

/* ── Info bar ── */
".info-bar{"
    "display:flex;justify-content:space-between;align-items:center;"
    "padding:12px 16px;max-width:960px;margin:0 auto 16px;"
    "font-size:0.75em;color:#6666aa;"
"}"
".status-dot{"
    "display:inline-block;width:8px;height:8px;border-radius:50%;"
    "margin-right:6px;"
    "animation:pulse 2s infinite;"
"}"
".status-dot.connected{background:#4ade80}"
".status-dot.disconnected{background:#f87171;animation:none}"
".status-dot.connecting{background:#fbbf24}"
"@keyframes pulse{"
    "0%,100%{opacity:1}"
    "50%{opacity:0.4}"
"}"

/* ── Botones ── */
".btn{"
    "background:linear-gradient(135deg,rgba(100,80,200,0.3),rgba(60,40,150,0.3));"
    "border:1px solid rgba(120,100,220,0.3);"
    "color:#b0a0e0;border-radius:8px;"
    "padding:8px 20px;cursor:pointer;font-size:0.85em;"
    "transition:all 0.2s;letter-spacing:0.5px;"
"}"
".btn:hover{"
    "background:linear-gradient(135deg,rgba(120,100,240,0.4),rgba(80,60,180,0.4));"
    "border-color:rgba(140,120,240,0.5);"
    "color:#d0c0ff;"
    "transform:translateY(-1px);"
    "box-shadow:0 4px 15px rgba(100,80,220,0.2);"
"}"
".btn:active{transform:translateY(0)}"

/* ── Ángulo card ── */
".angle-value{"
    "font-size:2em;font-weight:700;"
    "font-variant-numeric:tabular-nums;"
    "color:#a78bfa;"
"}"

/* ── Parámetros MAS ── */
".mas-grid{"
    "display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:8px;"
"}"
".mas-item{"
    "background:rgba(20,20,50,0.5);border-radius:10px;padding:12px;"
    "text-align:center;border:1px solid rgba(100,100,200,0.08);"
"}"
".mas-label{"
    "font-size:0.65em;text-transform:uppercase;letter-spacing:1px;"
    "color:#6666aa;margin-bottom:4px;"
"}"
".mas-value{"
    "font-size:1.3em;font-weight:700;font-variant-numeric:tabular-nums;"
"}"
".mas-unit{"
    "font-size:0.55em;font-weight:400;color:#7777aa;margin-left:2px;"
"}"
".color-cyan{color:#22d3ee}"
".color-green{color:#4ade80}"
".color-amber{color:#fbbf24}"
".color-pink{color:#f472b6}"

/* ── Separador ecuación ── */
".equation{"
    "font-size:0.7em;color:#555580;text-align:center;"
    "margin:10px 0 4px;padding:8px;font-style:italic;"
    "background:rgba(20,20,50,0.3);border-radius:6px;"
    "font-family:'Courier New',monospace;"
"}"

/* ── Tabla de datos raw ── */
".data-row{"
    "display:flex;justify-content:space-between;"
    "padding:5px 0;border-bottom:1px solid rgba(100,100,200,0.08);"
    "font-size:0.82em;"
"}"
".data-row:last-child{border:none}"
".data-label{color:#7777aa}"
".data-value{color:#c0c0e0;font-variant-numeric:tabular-nums}"

/* ── Badge oscilaciones ── */
".osc-badge{"
    "display:inline-flex;align-items:center;gap:4px;"
    "background:rgba(74,222,128,0.12);border:1px solid rgba(74,222,128,0.25);"
    "border-radius:20px;padding:3px 10px;font-size:0.72em;color:#4ade80;"
    "margin-top:8px;"
"}"
".osc-badge.waiting{"
    "background:rgba(251,191,36,0.1);border-color:rgba(251,191,36,0.2);"
    "color:#fbbf24;"
"}"

/* ── Panel de control ── */
".ctrl-bar{"
    "display:flex;gap:10px;justify-content:center;align-items:center;"
    "padding:10px 16px;max-width:960px;margin:0 auto 4px;"
    "flex-wrap:wrap;"
"}"
".btn-ctrl{"
    "display:inline-flex;align-items:center;gap:6px;"
    "padding:10px 22px;border-radius:10px;font-size:0.82em;"
    "cursor:pointer;transition:all 0.25s;letter-spacing:0.3px;"
    "font-weight:600;border:1px solid;position:relative;overflow:hidden;"
"}"
".btn-ctrl::after{"
    "content:'';position:absolute;top:0;left:-100%;width:100%;height:100%;"
    "background:linear-gradient(90deg,transparent,rgba(255,255,255,0.05),transparent);"
    "transition:left 0.5s;"
"}"
".btn-ctrl:hover::after{left:100%}"
".btn-ctrl:active{transform:scale(0.97)}"
".btn-ctrl.disabled{opacity:0.5;pointer-events:none}"

/* Reset button (blue) */
".btn-reset{"
    "background:linear-gradient(135deg,rgba(96,165,250,0.15),rgba(96,165,250,0.08));"
    "border-color:rgba(96,165,250,0.3);color:#60a5fa;"
"}"
".btn-reset:hover{"
    "background:linear-gradient(135deg,rgba(96,165,250,0.25),rgba(96,165,250,0.15));"
    "border-color:rgba(96,165,250,0.5);color:#93c5fd;"
    "box-shadow:0 4px 20px rgba(96,165,250,0.15);"
"}"

/* Calibrate button (amber) */
".btn-calib{"
    "background:linear-gradient(135deg,rgba(251,191,36,0.15),rgba(251,191,36,0.08));"
    "border-color:rgba(251,191,36,0.3);color:#fbbf24;"
"}"
".btn-calib:hover{"
    "background:linear-gradient(135deg,rgba(251,191,36,0.25),rgba(251,191,36,0.15));"
    "border-color:rgba(251,191,36,0.5);color:#fcd34d;"
    "box-shadow:0 4px 20px rgba(251,191,36,0.15);"
"}"

/* Export button (green) */
".btn-export{"
    "background:linear-gradient(135deg,rgba(74,222,128,0.15),rgba(74,222,128,0.08));"
    "border-color:rgba(74,222,128,0.3);color:#4ade80;"
"}"
".btn-export:hover{"
    "background:linear-gradient(135deg,rgba(74,222,128,0.25),rgba(74,222,128,0.15));"
    "border-color:rgba(74,222,128,0.5);color:#86efac;"
    "box-shadow:0 4px 20px rgba(74,222,128,0.15);"
"}"

/* Record toggle button (red/recording state) */
".btn-rec{"
    "background:linear-gradient(135deg,rgba(248,113,113,0.15),rgba(248,113,113,0.08));"
    "border-color:rgba(248,113,113,0.3);color:#f87171;"
"}"
".btn-rec:hover{"
    "background:linear-gradient(135deg,rgba(248,113,113,0.25),rgba(248,113,113,0.15));"
    "border-color:rgba(248,113,113,0.5);color:#fca5a5;"
    "box-shadow:0 4px 20px rgba(248,113,113,0.15);"
"}"
".btn-rec.recording{"
    "background:linear-gradient(135deg,rgba(248,113,113,0.3),rgba(248,113,113,0.18));"
    "border-color:rgba(248,113,113,0.5);color:#fca5a5;"
    "box-shadow:0 0 12px rgba(248,113,113,0.2);"
"}"
".btn-rec.recording .rec-dot{"
    "display:inline-block;width:8px;height:8px;border-radius:50%;"
    "background:#f87171;margin-right:4px;animation:pulse 1s infinite;"
"}"
".rec-counter{"
    "display:inline-flex;align-items:center;gap:5px;"
    "padding:3px 10px;border-radius:12px;font-size:0.72em;"
    "border:1px solid rgba(74,222,128,0.2);"
    "background:rgba(74,222,128,0.06);color:#4ade80;"
"}"
".rec-counter .counter-num{font-weight:700;font-variant-numeric:tabular-nums}"

/* Status toast */
".toast{"
    "position:fixed;bottom:24px;left:50%;transform:translateX(-50%) translateY(80px);"
    "padding:12px 24px;border-radius:12px;font-size:0.85em;"
    "z-index:999;transition:transform 0.4s cubic-bezier(0.16,1,0.3,1),opacity 0.4s;"
    "opacity:0;backdrop-filter:blur(12px);"
    "border:1px solid rgba(255,255,255,0.1);"
"}"
".toast.show{transform:translateX(-50%) translateY(0);opacity:1}"
".toast.success{background:rgba(74,222,128,0.15);color:#4ade80;border-color:rgba(74,222,128,0.3)}"
".toast.info{background:rgba(96,165,250,0.15);color:#60a5fa;border-color:rgba(96,165,250,0.3)}"
".toast.warning{background:rgba(251,191,36,0.15);color:#fbbf24;border-color:rgba(251,191,36,0.3)}"
".toast.error{background:rgba(244,114,182,0.15);color:#f472b6;border-color:rgba(244,114,182,0.3)}"

/* Spinner */
".spinner{display:inline-block;width:14px;height:14px;border:2px solid currentColor;"
    "border-right-color:transparent;border-radius:50%;animation:spin 0.6s linear infinite}"
"@keyframes spin{to{transform:rotate(360deg)}}"

/* ── WS status badge ── */
".ws-badge{"
    "display:inline-flex;align-items:center;gap:5px;"
    "padding:3px 10px;border-radius:12px;font-size:0.72em;"
    "border:1px solid rgba(100,100,200,0.15);"
    "background:rgba(15,15,40,0.6);"
"}"

"</style>"
"</head>"
"<body>"

/* ── Header ── */
"<div class='header'>"
    "<h1>⟳ Péndulo de Torsión</h1>"
    "<div class='subtitle'>Giroscopio MPU-6050 • Eje Z • ESP32 WROOM 32D • <strong>WebSocket 50 Hz</strong></div>"
"</div>"

/* ── Info bar ── */
"<div class='info-bar'>"
    "<div><span class='status-dot connecting' id='wsDot'></span>"
        "<span id='wsStatus'>Conectando...</span></div>"
    "<div class='ws-badge'>⚡ <span id='fps'>— muestras/s</span></div>"
"</div>"

/* ── Panel de control ── */
"<div class='ctrl-bar'>"
    "<button class='btn-ctrl btn-reset' id='btnReset' onclick='doReset()'>"
        "↺ Reiniciar (θ=0, ω=0, MAS)"
    "</button>"
    "<button class='btn-ctrl btn-calib' id='btnCalib' onclick='doCalibrate()'>"
        "⚖ Calibrar Bias (~5s)"
    "</button>"
    "<button class='btn-ctrl btn-rec recording' id='btnRec' onclick='toggleRecording()'>"
        "<span class='rec-dot'></span> ● Grabando"
    "</button>"
    "<button class='btn-ctrl btn-export' id='btnExport' onclick='doExport()'>"
        "📥 Exportar CSV"
    "</button>"
    "<span class='rec-counter' id='recCounter'>📊 <span class='counter-num' id='sampleCount'>0</span> muestras</span>"
"</div>"

/* ── Toast notification ── */
"<div class='toast' id='toast'></div>"

/* ── Dashboard ── */
"<div class='dashboard'>"

    /* Card: Velocidad Angular ω(t) */
    "<div class='card'>"
        "<div class='card-title'><span class='icon'>⟳</span> Velocidad Angular ω(t)</div>"
        "<div class='big-value zero' id='gyroZ'>0.00<span class='unit'>°/s</span></div>"
        "<div style='margin-top:12px'>"
            "<div class='data-row'>"
                "<span class='data-label'>Valor Raw</span>"
                "<span class='data-value' id='rawZ'>0</span>"
            "</div>"
            "<div class='data-row'>"
                "<span class='data-label'>Bias calibrado</span>"
                "<span class='data-value' id='biasZ'>— °/s</span>"
            "</div>"
            "<div class='data-row'>"
                "<span class='data-label'>Timestamp</span>"
                "<span class='data-value' id='timestamp'>0 ms</span>"
            "</div>"
        "</div>"
    "</div>"

    /* Card: Ángulo de Rotación θ(t) */
    "<div class='card'>"
        "<div class='card-title'><span class='icon'>∠</span> Ángulo de Rotación θ(t)</div>"
        "<div class='angle-value' id='angleZ'>0.00<span class='unit'>°</span></div>"
        "<div class='equation'>θ(t) = ∫ω(t)dt &nbsp;|&nbsp; ω = dθ/dt</div>"
    "</div>"

    /* Card: Gráficas en tiempo real (tabs ω y θ) */
    "<div class='card full-width'>"
        "<div class='card-title'><span class='icon'>📈</span> Gráfica en Tiempo Real</div>"
        "<div class='chart-tabs'>"
            "<div class='chart-tab active' id='tabOmega' onclick='switchChart(0)'>ω(t) Velocidad Angular</div>"
            "<div class='chart-tab' id='tabTheta' onclick='switchChart(1)'>θ(t) Ángulo</div>"
            "<div class='chart-tab' id='tabBoth' onclick='switchChart(2)'>ω + θ Ambas</div>"
        "</div>"
        "<div class='chart-container'>"
            "<canvas id='chart'></canvas>"
        "</div>"
    "</div>"

    /* Card: Parámetros del MAS */
    "<div class='card full-width'>"
        "<div class='card-title'><span class='icon'>⚛</span> Movimiento Armónico Simple (MAS)</div>"
        "<div class='equation'>θ(t) = θ₀·cos(ωₙt + φ)·e^(-γt) &nbsp;|&nbsp; ωₙ = 2π/T = √(κ/I)</div>"
        "<div class='mas-grid'>"
            "<div class='mas-item'>"
                "<div class='mas-label'>Período T</div>"
                "<div class='mas-value color-cyan' id='masT'>—<span class='mas-unit'>s</span></div>"
            "</div>"
            "<div class='mas-item'>"
                "<div class='mas-label'>Frecuencia f</div>"
                "<div class='mas-value color-green' id='masF'>—<span class='mas-unit'>Hz</span></div>"
            "</div>"
            "<div class='mas-item'>"
                "<div class='mas-label'>ωₙ (frec. natural)</div>"
                "<div class='mas-value color-amber' id='masOmegaN'>—<span class='mas-unit'>rad/s</span></div>"
            "</div>"
            "<div class='mas-item'>"
                "<div class='mas-label'>Amplitud |ω|max</div>"
                "<div class='mas-value color-pink' id='masAmp'>—<span class='mas-unit'>°/s</span></div>"
            "</div>"
        "</div>"
        "<div id='oscBadge' class='osc-badge waiting'>⏳ Esperando oscilaciones...</div>"
    "</div>"

    /* Card: Gauge */
    "<div class='card'>"
        "<div class='card-title'><span class='icon'>◎</span> Gauge ω(t)</div>"
        "<div class='gauge-container'>"
            "<svg class='gauge-svg' viewBox='0 0 200 120'>"
                /* Arco de fondo */
                "<path d='M 20 110 A 80 80 0 0 1 180 110' fill='none' "
                    "stroke='rgba(100,100,200,0.15)' stroke-width='12' stroke-linecap='round'/>"
                /* Arco de valor */
                "<path id='gaugeArc' d='M 20 110 A 80 80 0 0 1 180 110' fill='none' "
                    "stroke='url(#gaugeGrad)' stroke-width='12' stroke-linecap='round' "
                    "stroke-dasharray='0 999'/>"
                /* Gradiente */
                "<defs><linearGradient id='gaugeGrad' x1='0%' y1='0%' x2='100%' y2='0%'>"
                    "<stop offset='0%' stop-color='#60d9a8'/>"
                    "<stop offset='50%' stop-color='#60a5fa'/>"
                    "<stop offset='100%' stop-color='#f472b6'/>"
                "</linearGradient></defs>"
                /* Texto central */
                "<text id='gaugeText' x='100' y='95' text-anchor='middle' "
                    "fill='#c0c0e0' font-size='22' font-weight='700' "
                    "font-family='Segoe UI,system-ui,sans-serif'>0.0</text>"
                "<text x='100' y='112' text-anchor='middle' fill='#6666aa' "
                    "font-size='10' font-family='Segoe UI,system-ui,sans-serif'>°/s</text>"
            "</svg>"
            "<div class='gauge-label'>Rango: ±2000 °/s</div>"
        "</div>"
    "</div>"

    /* Card: Info del Sistema */
    "<div class='card'>"
        "<div class='card-title'><span class='icon'>⚡</span> Sistema</div>"
        "<div class='data-row'>"
            "<span class='data-label'>Sensor</span>"
            "<span class='data-value'>MPU-6050</span>"
        "</div>"
        "<div class='data-row'>"
            "<span class='data-label'>MCU</span>"
            "<span class='data-value'>ESP32 WROOM 32D</span>"
        "</div>"
        "<div class='data-row'>"
            "<span class='data-label'>Transporte</span>"
            "<span class='data-value'>WebSocket (push)</span>"
        "</div>"
        "<div class='data-row'>"
            "<span class='data-label'>I2C</span>"
            "<span class='data-value'>400 kHz (GPIO21/22)</span>"
        "</div>"
        "<div class='data-row'>"
            "<span class='data-label'>Giroscopio</span>"
            "<span class='data-value'>±2000 °/s (16.4 LSB)</span>"
        "</div>"
        "<div class='data-row'>"
            "<span class='data-label'>DLPF</span>"
            "<span class='data-value'>42 Hz</span>"
        "</div>"
        "<div class='data-row'>"
            "<span class='data-label'>Push Rate</span>"
            "<span class='data-value' id='pushRate'>50 Hz</span>"
        "</div>"
        "<div class='data-row'>"
            "<span class='data-label'>Temperatura</span>"
            "<span class='data-value' id='tempC'>— °C</span>"
        "</div>"
    "</div>"

"</div>"

/* ═══════════════════ JavaScript (WebSocket) ═══════════════════ */
"<script>"

/* ── Estado ── */
"const MAX_PTS=300;"
"let omegaPts=[];"
"let thetaPts=[];"
"let chartMode=0;"  /* 0=omega, 1=theta, 2=both */
"let fpsCount=0;"
"let lastFpsTime=Date.now();"

/* ── Data Export Buffer ── */
"let exportBuf=[];"   /* Array de objetos: {ts, gz, rz, az, bi, tc, pe, fr, on, am, oc, mv} */
"let isRecording=true;"  /* Empieza grabando por defecto */

/* ── Canvas setup (optimizado para rendimiento) ── */
"const canvas=document.getElementById('chart');"
"const ctx=canvas.getContext('2d',{alpha:false});"  /* opaque = faster compositing */
"let cW,cH,dpr=1;"

"function resizeCanvas(){"
    "const r=canvas.parentElement.getBoundingClientRect();"
    "dpr=Math.min(window.devicePixelRatio||1,1.5);"  /* cap DPR to avoid perf hit */
    "canvas.width=Math.round(r.width*dpr);"
    "canvas.height=Math.round(r.height*dpr);"
    "cW=r.width;cH=r.height;"
    "ctx.setTransform(dpr,0,0,dpr,0,0);"
"}"
"resizeCanvas();"
"window.addEventListener('resize',()=>{resizeCanvas();drawChart()});"

/* ── Cache DOM refs (evitar getElementById en cada frame) ── */
"const $tabOmega=document.getElementById('tabOmega');"
"const $tabTheta=document.getElementById('tabTheta');"
"const $tabBoth=document.getElementById('tabBoth');"
"const $gaugeArc=document.getElementById('gaugeArc');"
"const $gaugeText=document.getElementById('gaugeText');"

/* ── Cambiar gráfica ── */
"function switchChart(mode){"
    "chartMode=mode;"
    "$tabOmega.className='chart-tab'+(mode===0?' active':'');"
    "$tabTheta.className='chart-tab'+(mode===1?' active':'');"
    "$tabBoth.className='chart-tab'+(mode===2?' active':'');"
    "drawChart();"
"}"

/* ── Dibujar una serie (SIN shadowBlur — rendimiento 10x mejor) ── */
"function drawSeries(pts,color,fillColor,label,baseY,H){"
    "const n=pts.length;"
    "if(n<2)return;"

    /* Auto-rango */
    "let mx=0.5;"
    "for(let i=0;i<n;i++){const a=pts[i]<0?-pts[i]:pts[i];if(a>mx)mx=a;}"
    "mx*=1.3;"

    "const padL=45,midY=baseY+H*0.5,hH=H*0.5;"
    "const step=(cW-padL)/MAX_PTS;"

    /* Zero-line (solid, no dashed = más rápido) */
    "ctx.strokeStyle='rgba(100,140,255,0.12)';"
    "ctx.lineWidth=0.5;"
    "ctx.beginPath();"
    "ctx.moveTo(padL,midY);"
    "ctx.lineTo(cW,midY);"
    "ctx.stroke();"

    /* Y labels + serie label */
    "ctx.font='9px system-ui';"
    "ctx.fillStyle='#444466';"
    "ctx.fillText('+'+mx.toFixed(0),2,baseY+12);"
    "ctx.fillText('0',2,midY+3);"
    "ctx.fillText('-'+mx.toFixed(0),2,baseY+H-2);"
    "ctx.fillStyle=color;ctx.font='bold 10px system-ui';"
    "ctx.fillText(label,padL+3,baseY+12);"

    /* Pre-calcular coordenadas Y (un solo pass) */
    "const invR=hH/mx;"

    /* Construir path de la línea */
    "ctx.beginPath();"
    "ctx.moveTo(padL,midY-pts[0]*invR);"
    "for(let i=1;i<n;i++){"
        "ctx.lineTo(padL+i*step,midY-pts[i]*invR);"
    "}"

    /* Glow: trazo ancho semitransparente (NO usa shadowBlur) */
    "ctx.lineWidth=4;ctx.strokeStyle=fillColor;ctx.lineJoin='round';ctx.lineCap='round';"
    "ctx.stroke();"

    /* Línea principal nítida */
    "ctx.lineWidth=1.5;ctx.strokeStyle=color;"
    "ctx.stroke();"

    /* Area fill: cerrar path hacia abajo */
    "const lastX=padL+(n-1)*step;"
    "ctx.lineTo(lastX,baseY+H);"
    "ctx.lineTo(padL,baseY+H);"
    "ctx.closePath();"
    "ctx.fillStyle=fillColor;"
    "ctx.fill();"

    /* Punto actual (sin shadow) */
    "const ly=midY-pts[n-1]*invR;"
    "ctx.fillStyle=color;"
    "ctx.beginPath();ctx.arc(lastX,ly,3,0,6.283);ctx.fill();"
    /* Punto outer glow (circle behind) */
    "ctx.fillStyle=fillColor;"
    "ctx.beginPath();ctx.arc(lastX,ly,6,0,6.283);ctx.fill();"
    /* Redraw inner dot on top */
    "ctx.fillStyle=color;"
    "ctx.beginPath();ctx.arc(lastX,ly,2.5,0,6.283);ctx.fill();"
"}"

/* ── Dibujar gráfica completa ── */
"function drawChart(){"
    /* Clear con fondo sólido (más rápido que clearRect en canvas opaco) */
    "ctx.fillStyle='#11112a';"
    "ctx.fillRect(0,0,cW,cH);"

    "if(chartMode===0){"
        "drawSeries(omegaPts,'rgba(96,165,250,0.9)','rgba(96,165,250,0.12)','ω °/s',0,cH);"
    "}else if(chartMode===1){"
        "drawSeries(thetaPts,'rgba(167,139,250,0.9)','rgba(167,139,250,0.12)','θ °',0,cH);"
    "}else{"
        "const hH=cH*0.5;"
        "drawSeries(omegaPts,'rgba(96,165,250,0.9)','rgba(96,165,250,0.10)','ω °/s',0,hH);"
        "drawSeries(thetaPts,'rgba(167,139,250,0.9)','rgba(167,139,250,0.10)','θ °',hH,hH);"
    "}"
"}"

/* ── Actualizar gauge (refs cacheadas) ── */
"function updateGauge(v){"
    "const mx=2000;"
    "const c=Math.max(-mx,Math.min(mx,v));"
    "const pct=(c+mx)/(2*mx);"
    "$gaugeArc.setAttribute('stroke-dasharray',(pct*251)+' 251');"
    "$gaugeText.textContent=v.toFixed(1);"
"}"

/* ── Render loop: requestAnimationFrame continuo ── */
"let drawPending=false;"
"function schedDraw(){"
    "if(!drawPending){"
        "drawPending=true;"
        "requestAnimationFrame(()=>{drawChart();drawPending=false;});"
    "}"
"}"

/* ── Toast notification ── */
"function showToast(msg,type,duration){"
    "const t=document.getElementById('toast');"
    "t.textContent=msg;"
    "t.className='toast '+type+' show';"
    "clearTimeout(t._tid);"
    "t._tid=setTimeout(()=>{t.className='toast'},duration||3000);"
"}"

/* ══════════════════════════════════════════════════════════════
 *  WebSocket — Conexión push en tiempo real
 * ══════════════════════════════════════════════════════════════ */

"let ws=null;"
"let wsReconnectTimer=null;"
"let wsReconnectDelay=500;"  /* Empieza con 500ms, crece hasta 5s */

"function wsConnect(){"
    "if(ws&&(ws.readyState===0||ws.readyState===1))return;"  /* ya conectado o conectando */

    "const host=location.host||'192.168.4.1';"
    "const url='ws://'+host+'/ws';"
    "console.log('WS conectando a',url);"

    "setWsUI('connecting','Conectando...');"
    "ws=new WebSocket(url);"

    "ws.onopen=function(){"
        "console.log('WS conectado');"
        "wsReconnectDelay=500;"  /* reset delay on success */
        "setWsUI('connected','WebSocket activo • 192.168.4.1');"
        "showToast('⚡ WebSocket conectado — datos en tiempo real','success',2000);"
    "};"

    "ws.onmessage=function(ev){"
        "try{"
            "const d=JSON.parse(ev.data);"
            "if(d.error)return;"
            "handleData(d);"
        "}catch(e){console.error('WS parse:',e)}"
    "};"

    "ws.onclose=function(){"
        "console.log('WS cerrado, reconectando en',wsReconnectDelay,'ms');"
        "setWsUI('disconnected','Desconectado — reconectando...');"
        "schedReconnect();"
    "};"

    "ws.onerror=function(e){"
        "console.error('WS error:',e);"
        "ws.close();"
    "};"
"}"

"function schedReconnect(){"
    "clearTimeout(wsReconnectTimer);"
    "wsReconnectTimer=setTimeout(()=>{"
        "wsConnect();"
        "wsReconnectDelay=Math.min(wsReconnectDelay*1.5,5000);"
    "},wsReconnectDelay);"
"}"

"function setWsUI(state,text){"
    "const dot=document.getElementById('wsDot');"
    "const st=document.getElementById('wsStatus');"
    "dot.className='status-dot '+state;"
    "st.textContent=text;"
"}"

/* ── Procesar dato recibido (refs cacheadas para 50 Hz) ── */
"const $gyroZ=document.getElementById('gyroZ');"
"const $rawZ=document.getElementById('rawZ');"
"const $ts=document.getElementById('timestamp');"
"const $biasZ=document.getElementById('biasZ');"
"const $angleZ=document.getElementById('angleZ');"
"const $tempC=document.getElementById('tempC');"
"const $masT=document.getElementById('masT');"
"const $masF=document.getElementById('masF');"
"const $masOn=document.getElementById('masOmegaN');"
"const $masAmp=document.getElementById('masAmp');"
"const $oscBadge=document.getElementById('oscBadge');"
"const $fps=document.getElementById('fps');"
"const $sampleCount=document.getElementById('sampleCount');"

"function handleData(d){"
    "const gz=d.gz;"
    "const angle=d.az;"

    /* ω(t) */
    "$gyroZ.textContent=gz.toFixed(2);"
    "$gyroZ.className='big-value '+(gz>0.5?'positive':(gz<-0.5?'negative':'zero'));"
    "$rawZ.textContent=d.rz;"
    "$ts.textContent=d.ts+' ms';"

    /* Bias */
    "if(d.bi!==undefined){"
        "$biasZ.textContent=d.bi.toFixed(4)+' °/s';"
    "}"

    /* θ(t) */
    "$angleZ.textContent=angle.toFixed(2)+'°';"

    /* Temperatura */
    "if(d.tc!==undefined){"
        "$tempC.textContent=d.tc.toFixed(1)+' °C';"
    "}"

    /* MAS parameters */
    "if(d.mv){"
        "$masT.textContent=d.pe.toFixed(3)+' s';"
        "$masF.textContent=d.fr.toFixed(3)+' Hz';"
        "$masOn.textContent=d.on.toFixed(3)+' rad/s';"
        "$masAmp.textContent=d.am.toFixed(2)+' °/s';"
        "$oscBadge.className='osc-badge';"
        "$oscBadge.textContent='✓ '+d.oc+' oscilaciones detectadas';"
    "}else{"
        "$oscBadge.className='osc-badge waiting';"
        "$oscBadge.textContent='⏳ Esperando oscilaciones...';"
    "}"

    /* Gráficas — acumular puntos y pedir dibujo */
    "omegaPts.push(gz);"
    "thetaPts.push(angle);"
    "if(omegaPts.length>MAX_PTS)omegaPts.shift();"
    "if(thetaPts.length>MAX_PTS)thetaPts.shift();"
    "schedDraw();"

    /* Acumular en buffer de export si está grabando */
    "if(isRecording){"
        "exportBuf.push({"
            "ts:d.ts,gz:d.gz,rz:d.rz,az:d.az,"
            "bi:d.bi!==undefined?d.bi:0,"
            "tc:d.tc!==undefined?d.tc:0,"
            "mv:d.mv?1:0,pe:d.pe||0,fr:d.fr||0,"
            "on:d.on||0,am:d.am||0,oc:d.oc||0"
        "});"
        "$sampleCount.textContent=exportBuf.length;"
    "}"

    /* Gauge */
    "updateGauge(gz);"

    /* FPS counter */
    "fpsCount++;"
    "const now=Date.now();"
    "if(now-lastFpsTime>=1000){"
        "$fps.textContent=fpsCount+' muestras/s';"
        "fpsCount=0;lastFpsTime=now;"
    "}"
"}"

/* ── Reinicio completo (REST) ── */
"async function doReset(){"
    "const btn=document.getElementById('btnReset');"
    "btn.classList.add('disabled');"
    "btn.innerHTML='<span class=\"spinner\"></span> Reiniciando...';"
    "try{"
        "const r=await fetch('/reset',{method:'POST'});"
        "const d=await r.json();"
        "omegaPts=[];thetaPts=[];exportBuf=[];"
        "document.getElementById('sampleCount').textContent='0';"
        "document.getElementById('angleZ').innerHTML='0.00<span class=\"unit\">°</span>';"
        "document.getElementById('gyroZ').innerHTML='0.00<span class=\"unit\">°/s</span>';"
        "document.getElementById('gyroZ').className='big-value zero';"
        "drawChart();"
        "showToast('✓ Datos reiniciados: θ=0, ω=0, MAS limpio','success');"
    "}catch(e){showToast('✗ Error al reiniciar','error')}"
    "btn.classList.remove('disabled');"
    "btn.innerHTML='↺ Reiniciar (θ=0, ω=0, MAS)';"
"}"

/* ── Calibración manual (REST) ── */
"async function doCalibrate(){"
    "const btn=document.getElementById('btnCalib');"
    "btn.classList.add('disabled');"
    "btn.innerHTML='<span class=\"spinner\"></span> Calibrando... (~5s)';"
    "showToast('⚖ Calibrando bias — NO mover el sensor...','warning',7000);"
    "try{"
        "const r=await fetch('/calibrate',{method:'POST'});"
        "const d=await r.json();"
        "if(d.status==='ok'){"
            "omegaPts=[];thetaPts=[];"
            "drawChart();"
            "document.getElementById('biasZ').textContent=d.bias.toFixed(4)+' °/s';"
            "document.getElementById('angleZ').innerHTML='0.00<span class=\"unit\">°</span>';"
            "showToast('✓ Calibración exitosa. Nuevo bias: '+d.bias.toFixed(4)+' °/s','success',4000);"
        "}else{"
            "showToast('✗ Calibración fallida: '+d.message,'error',4000);"
        "}"
    "}catch(e){showToast('✗ Error de conexión al calibrar','error')}"
    "btn.classList.remove('disabled');"
    "btn.innerHTML='⚖ Calibrar Bias (~5s)';"
"}"

/* ── Toggle grabación ── */
"function toggleRecording(){"
    "isRecording=!isRecording;"
    "const btn=document.getElementById('btnRec');"
    "if(isRecording){"
        "btn.className='btn-ctrl btn-rec recording';"
        "btn.innerHTML='<span class=\"rec-dot\"></span> ● Grabando';"
        "showToast('● Grabación reanudada','success',1500);"
    "}else{"
        "btn.className='btn-ctrl btn-rec';"
        "btn.innerHTML='⏸ Pausado';"
        "showToast('⏸ Grabación pausada — los datos existentes se conservan','info',2000);"
    "}"
"}"

/* ── Exportar datos como CSV ── */
"function doExport(){"
    "if(exportBuf.length===0){"
        "showToast('⚠ No hay datos para exportar — espera a recibir muestras','warning',3000);"
        "return;"
    "}"

    /* Cabecera CSV */
    "const hdr='Timestamp_ms,Gyro_Z_dps,Raw_Z,Angulo_Z_deg,Bias_dps,Temp_C,MAS_Valido,Periodo_s,Frecuencia_Hz,Omega_n_rads,Amplitud_dps,Oscilaciones';"

    /* Filas */
    "let csv=hdr+'\\n';"
    "for(const r of exportBuf){"
        "csv+=r.ts+','+r.gz.toFixed(4)+','+r.rz+','+r.az.toFixed(4)+','"
            "+r.bi.toFixed(4)+','+r.tc.toFixed(1)+','+r.mv+','"
            "+r.pe.toFixed(4)+','+r.fr.toFixed(4)+','"
            "+r.on.toFixed(4)+','+r.am.toFixed(3)+','+r.oc+'\\n';"
    "}"

    /* Crear blob y descargar */
    "const blob=new Blob([csv],{type:'text/csv;charset=utf-8;'});"
    "const url=URL.createObjectURL(blob);"
    "const a=document.createElement('a');"
    "const now=new Date();"
    "const fname='pendulo_torsion_'"
        "+now.getFullYear()"
        "+String(now.getMonth()+1).padStart(2,'0')"
        "+String(now.getDate()).padStart(2,'0')"
        "+'_'"
        "+String(now.getHours()).padStart(2,'0')"
        "+String(now.getMinutes()).padStart(2,'0')"
        "+String(now.getSeconds()).padStart(2,'0')"
        "+'.csv';"
    "a.href=url;a.download=fname;"
    "document.body.appendChild(a);a.click();"
    "document.body.removeChild(a);"
    "URL.revokeObjectURL(url);"
    "showToast('✓ CSV exportado: '+exportBuf.length+' muestras → '+fname,'success',4000);"
"}"

/* ── Iniciar WebSocket ── */
"wsConnect();"

"</script>"
"</body></html>";

/* ══════════════════════════════════════════════════════════════
 *  WebSocket — Gestión del lado servidor (ESP32)
 * ══════════════════════════════════════════════════════════════ */

/**
 * @brief Elimina un file descriptor de la lista de clientes WS
 */
static void ws_remove_client(int fd)
{
    for (int i = 0; i < s_ws_count; i++) {
        if (s_ws_fds[i] == fd) {
            s_ws_fds[i] = s_ws_fds[s_ws_count - 1];
            s_ws_count--;
            ESP_LOGI(TAG, "WS cliente desconectado (fd=%d). Activos: %d", fd, s_ws_count);
            return;
        }
    }
}

/**
 * @brief Handler para el endpoint WebSocket /ws
 *
 * Cuando un cliente se conecta, su file descriptor se guarda para
 * que la tarea ws_push_task le envíe datos.
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    /* Si es un handshake (HTTP GET → Upgrade), aceptar */
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "WS handshake aceptado (fd=%d)", fd);

        /* Agregar a la lista de clientes */
        if (s_ws_count < WS_MAX_CLIENTS) {
            s_ws_fds[s_ws_count++] = fd;
            ESP_LOGI(TAG, "WS clientes activos: %d", s_ws_count);
        } else {
            ESP_LOGW(TAG, "Máximo de clientes WS alcanzado (%d), rechazando", WS_MAX_CLIENTS);
        }
        return ESP_OK;
    }

    /* Frame de datos: leer (puede ser ping/pong/close del cliente) */
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WS recv frame error: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Si el cliente envió un close o se perdió el frame, remover */
    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        int fd = httpd_req_to_sockfd(req);
        ws_remove_client(fd);
    }

    return ESP_OK;
}

/**
 * @brief Tarea FreeRTOS que lee el MPU6050 y empuja datos a clientes WS
 *
 * Esta tarea corre a WS_PUSH_RATE_MS (20ms = 50 Hz) y envía un JSON
 * compacto con todos los datos del sensor a cada cliente WebSocket conectado.
 *
 * Las claves JSON son cortas para minimizar el tamaño del payload:
 *   gz = gyro_z_dps, rz = raw_z, az = angle_z, ts = timestamp,
 *   bi = bias, tc = temp_c, mv = mas_valid, pe = period, fr = freq,
 *   on = omega_n, am = amplitude, oc = oscillations
 */
static void ws_push_task(void *arg)
{
    httpd_handle_t server = (httpd_handle_t)arg;

    ESP_LOGI(TAG, "WS push task iniciada (intervalo=%d ms, target=%d Hz)",
             WS_PUSH_RATE_MS, 1000 / WS_PUSH_RATE_MS);

    char json_buf[320];
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t temp_counter = 0;
    float cached_temp = 0.0f;

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(WS_PUSH_RATE_MS));

        /* Si no hay clientes, no hacer nada */
        if (s_ws_count == 0) {
            continue;
        }

        /* Leer sensor */
        mpu6050_data_t sd = {0};
        esp_err_t ret = mpu6050_read_gyro_z(&sd);
        if (ret != ESP_OK) {
            continue;
        }

        /* Leer temperatura solo cada ~1 segundo (50 ciclos) para no sobrecargar I2C */
        temp_counter++;
        if (temp_counter >= 50) {
            mpu6050_read_temperature(&cached_temp);
            temp_counter = 0;
        }

        /* Construir JSON compacto */
        int len = snprintf(json_buf, sizeof(json_buf),
            "{\"gz\":%.3f,\"rz\":%d,\"az\":%.3f,"
            "\"ts\":%lld,\"tc\":%.1f,\"bi\":%.4f,"
            "\"mv\":%s,\"pe\":%.4f,\"fr\":%.4f,"
            "\"on\":%.4f,\"am\":%.3f,\"oc\":%lu}",
            sd.gyro_z_dps,
            (int)sd.gyro_z_raw,
            sd.angle_z_deg,
            (long long)sd.timestamp_ms,
            cached_temp,
            sd.bias_z_dps,
            sd.mas_valid ? "true" : "false",
            sd.period_s,
            sd.frequency_hz,
            sd.omega_n,
            sd.amplitude_dps,
            (unsigned long)sd.oscillations
        );

        /* Enviar a cada cliente WS conectado */
        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(ws_pkt));
        ws_pkt.payload = (uint8_t *)json_buf;
        ws_pkt.len = len;
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;

        for (int i = 0; i < s_ws_count; ) {
            esp_err_t send_ret = httpd_ws_send_frame_async(server, s_ws_fds[i], &ws_pkt);
            if (send_ret != ESP_OK) {
                ESP_LOGW(TAG, "WS envío fallido a fd=%d (%s), removiendo",
                         s_ws_fds[i], esp_err_to_name(send_ret));
                ws_remove_client(s_ws_fds[i]);
                /* No incrementar i, el array se compactó */
            } else {
                i++;
            }
        }
    }
}

/* ══════════════════════════════════════════════════════════════
 *  HTTP Handlers (REST para acciones, no para datos)
 * ══════════════════════════════════════════════════════════════ */

/**
 * @brief Handler GET / — Sirve la página web principal
 */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    /* Enviar el HTML en chunks para no saturar el buffer de TX.
     * La página es ~30KB, demasiado para enviar de golpe en ESP32. */
    const size_t total_len = sizeof(MAIN_PAGE_HTML) - 1;
    const size_t chunk_size = 2048;
    const char *ptr = MAIN_PAGE_HTML;
    size_t remaining = total_len;

    while (remaining > 0) {
        size_t to_send = remaining > chunk_size ? chunk_size : remaining;
        esp_err_t err = httpd_resp_send_chunk(req, ptr, to_send);
        if (err != ESP_OK) {
            /* Abortar envio */
            httpd_resp_send_chunk(req, NULL, 0);
            return err;
        }
        ptr += to_send;
        remaining -= to_send;
    }

    /* Finalizar chunked response */
    return httpd_resp_send_chunk(req, NULL, 0);
}

/**
 * @brief Handler POST /reset — Reinicia el ángulo acumulado y MAS
 */
static esp_err_t reset_post_handler(httpd_req_t *req)
{
    mpu6050_reset_angle();

    const char *resp = "{\"status\":\"ok\",\"message\":\"angle_and_mas_reset\"}";
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, resp, strlen(resp));
}

/**
 * @brief Handler POST /calibrate — Recalibra el bias del giroscopio
 *
 * El sensor DEBE estar en reposo durante la calibración (~5 segundos).
 * Después de calibrar, reinicia el ángulo y MAS.
 */
static esp_err_t calibrate_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Solicitud de calibración manual recibida");

    esp_err_t ret = mpu6050_recalibrate();

    char json_buf[256];
    if (ret == ESP_OK) {
        mpu6050_data_t sd = {0};
        mpu6050_read_gyro_z(&sd);
        snprintf(json_buf, sizeof(json_buf),
            "{\"status\":\"ok\",\"message\":\"calibration_complete\",\"bias\":%.4f}",
            sd.bias_z_dps);
    } else {
        snprintf(json_buf, sizeof(json_buf),
            "{\"status\":\"error\",\"message\":\"calibration_failed\",\"code\":%d}",
            (int)ret);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_buf, strlen(json_buf));
}

/* ── URI descriptors ── */
static const httpd_uri_t uri_root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL,
};

static const httpd_uri_t uri_ws = {
    .uri          = "/ws",
    .method       = HTTP_GET,
    .handler      = ws_handler,
    .user_ctx     = NULL,
    .is_websocket = true,
};

static const httpd_uri_t uri_reset = {
    .uri       = "/reset",
    .method    = HTTP_POST,
    .handler   = reset_post_handler,
    .user_ctx  = NULL,
};

static const httpd_uri_t uri_calibrate = {
    .uri       = "/calibrate",
    .method    = HTTP_POST,
    .handler   = calibrate_post_handler,
    .user_ctx  = NULL,
};

/* ══════════════════════════════════════════════════════════════
 *  API Pública
 * ══════════════════════════════════════════════════════════════ */

esp_err_t web_server_start(void)
{
    if (s_server != NULL) {
        ESP_LOGW(TAG, "Servidor ya está corriendo");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.max_open_sockets = 5;    /* 5 user + 3 internal = 8 ≤ LWIP_MAX_SOCKETS(10) */
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  Iniciando servidor HTTP+WS en puerto %d", config.server_port);

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al iniciar servidor: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Registrar handlers */
    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_ws);
    httpd_register_uri_handler(s_server, &uri_reset);
    httpd_register_uri_handler(s_server, &uri_calibrate);

    /* Inicializar lista de clientes WS */
    s_ws_count = 0;
    memset(s_ws_fds, -1, sizeof(s_ws_fds));

    /* Crear tarea de push WebSocket */
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        ws_push_task,
        "ws_push",
        4096,               /* Stack: 4 KB es suficiente */
        (void *)s_server,   /* Pasar handle del servidor */
        5,                  /* Prioridad alta para baja latencia */
        &s_ws_task_handle,
        1                   /* Core 1 (Core 0 para WiFi) */
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Error al crear tarea ws_push");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "  Servidor HTTP+WebSocket iniciado ✓");
    ESP_LOGI(TAG, "  Endpoints:");
    ESP_LOGI(TAG, "    GET  /          → Interfaz web");
    ESP_LOGI(TAG, "    WS   /ws        → WebSocket datos tiempo real (50 Hz)");
    ESP_LOGI(TAG, "    POST /reset     → Reiniciar ángulo + MAS");
    ESP_LOGI(TAG, "    POST /calibrate → Recalibrar bias del gyro");
    ESP_LOGI(TAG, "═══════════════════════════════════════════");

    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (s_server == NULL) {
        return ESP_OK;
    }

    /* Detener tarea de push */
    if (s_ws_task_handle != NULL) {
        vTaskDelete(s_ws_task_handle);
        s_ws_task_handle = NULL;
    }

    s_ws_count = 0;

    esp_err_t ret = httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "Servidor HTTP+WS detenido");
    return ret;
}
