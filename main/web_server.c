/**
 * @file web_server.c
 * @brief Servidor HTTP con interfaz web embebida para el péndulo de torsión
 *
 * Incluye una interfaz web premium con:
 * - Gráfica en tiempo real de ω(t) (velocidad angular) y θ(t) (ángulo)
 * - Indicador gauge de velocidad angular
 * - Panel de parámetros del MAS (período, frecuencia, ωₙ)
 * - Visualización del ángulo acumulado por integración
 * - Diseño oscuro con gradientes y animaciones
 */

#include "web_server.h"
#include "mpu6050.h"
#include "wifi_softap.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "WEB_SRV";
static httpd_handle_t s_server = NULL;

/* ══════════════════════════════════════════════════════════════
 *  HTML/CSS/JS Embebido — Interfaz Web Premium
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
    "background:#4ade80;margin-right:6px;"
    "animation:pulse 2s infinite;"
"}"
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

"</style>"
"</head>"
"<body>"

/* ── Header ── */
"<div class='header'>"
    "<h1>⟳ Péndulo de Torsión</h1>"
    "<div class='subtitle'>Giroscopio MPU-6050 • Eje Z • ESP32 WROOM 32D</div>"
"</div>"

/* ── Info bar ── */
"<div class='info-bar'>"
    "<div><span class='status-dot'></span>Red: PenduloTorsion • 192.168.4.1</div>"
    "<div id='fps'>— muestras/s</div>"
"</div>"

/* ── Panel de control ── */
"<div class='ctrl-bar'>"
    "<button class='btn-ctrl btn-reset' id='btnReset' onclick='doReset()'>"
        "↺ Reiniciar (θ=0, ω=0, MAS)"
    "</button>"
    "<button class='btn-ctrl btn-calib' id='btnCalib' onclick='doCalibrate()'>"
        "⚖ Calibrar Bias (~5s)"
    "</button>"
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
            "<div class='gauge-label'>Rango: ±250 °/s</div>"
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
            "<span class='data-label'>I2C</span>"
            "<span class='data-value'>400 kHz (GPIO21/22)</span>"
        "</div>"
        "<div class='data-row'>"
            "<span class='data-label'>Giroscopio</span>"
            "<span class='data-value'>±250 °/s (131 LSB)</span>"
        "</div>"
        "<div class='data-row'>"
            "<span class='data-label'>DLPF</span>"
            "<span class='data-value'>42 Hz</span>"
        "</div>"
        "<div class='data-row'>"
            "<span class='data-label'>Sample Rate</span>"
            "<span class='data-value'>100 Hz</span>"
        "</div>"
        "<div class='data-row'>"
            "<span class='data-label'>Temperatura</span>"
            "<span class='data-value' id='tempC'>— °C</span>"
        "</div>"
    "</div>"

"</div>"

/* ═══════════════════ JavaScript ═══════════════════ */
"<script>"

/* ── Estado ── */
"const MAX_PTS=200;"
"let omegaPts=[];"
"let thetaPts=[];"
"let chartMode=0;"  /* 0=omega, 1=theta, 2=both */
"let fpsCount=0;"
"let lastFpsTime=Date.now();"

/* ── Canvas setup ── */
"const canvas=document.getElementById('chart');"
"const ctx=canvas.getContext('2d');"
"let cW,cH;"

"function resizeCanvas(){"
    "const r=canvas.parentElement.getBoundingClientRect();"
    "canvas.width=r.width*2;canvas.height=r.height*2;"
    "cW=r.width;cH=r.height;"
"}"
"resizeCanvas();"
"window.addEventListener('resize',()=>{resizeCanvas();drawChart()});"

/* ── Cambiar gráfica ── */
"function switchChart(mode){"
    "chartMode=mode;"
    "document.getElementById('tabOmega').className='chart-tab'+(mode===0?' active':'');"
    "document.getElementById('tabTheta').className='chart-tab'+(mode===1?' active':'');"
    "document.getElementById('tabBoth').className='chart-tab'+(mode===2?' active':'');"
    "drawChart();"
"}"

/* ── Dibujar una serie en el canvas ── */
"function drawSeries(pts,color,glowColor,label,yOffset,yScale){"
    "if(pts.length<2)return;"
    "const W=cW,H=cH*yScale;"
    "const baseY=yOffset;"

    "let minV=Infinity,maxV=-Infinity;"
    "for(const p of pts){if(p<minV)minV=p;if(p>maxV)maxV=p}"
    "const range=Math.max(Math.abs(minV),Math.abs(maxV),0.5)*1.3;"

    "const plotW=W-55;"
    "const step=plotW/MAX_PTS;"

    /* Cero line */
    "ctx.save();"
    "ctx.strokeStyle='rgba(100,140,255,0.15)';"
    "ctx.lineWidth=0.5;"
    "ctx.setLineDash([3,3]);"
    "ctx.beginPath();"
    "ctx.moveTo(45,baseY+H/2);"
    "ctx.lineTo(W,baseY+H/2);"
    "ctx.stroke();"
    "ctx.setLineDash([]);"

    /* Y labels */
    "ctx.font='9px system-ui';"
    "ctx.fillStyle='#444466';"
    "ctx.fillText('+'+range.toFixed(1),2,baseY+12);"
    "ctx.fillText('0',2,baseY+H/2+3);"
    "ctx.fillText('-'+range.toFixed(1),2,baseY+H-2);"

    /* Label */
    "ctx.fillStyle=color;ctx.font='bold 10px system-ui';"
    "ctx.fillText(label,48,baseY+12);"

    /* Glow */
    "ctx.shadowColor=glowColor;"
    "ctx.shadowBlur=6;"

    "ctx.strokeStyle=color;"
    "ctx.lineWidth=1.8;"
    "ctx.lineJoin='round';ctx.lineCap='round';"
    "ctx.beginPath();"
    "for(let i=0;i<pts.length;i++){"
        "const x=45+i*step;"
        "const y=baseY+H/2-(pts[i]/range)*(H/2);"
        "if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);"
    "}"
    "ctx.stroke();"
    "ctx.shadowBlur=0;"

    /* Area fill */
    "const aGrad=ctx.createLinearGradient(0,baseY,0,baseY+H);"
    "aGrad.addColorStop(0,color.replace('0.9','0.1').replace(')',',0.1)'));"
    "aGrad.addColorStop(1,'rgba(0,0,0,0)');"
    "ctx.fillStyle=aGrad;"
    "ctx.lineTo(45+(pts.length-1)*step,baseY+H);"
    "ctx.lineTo(45,baseY+H);"
    "ctx.closePath();ctx.fill();"

    /* Current point */
    "if(pts.length>0){"
        "const lx=45+(pts.length-1)*step;"
        "const ly=baseY+H/2-(pts[pts.length-1]/range)*(H/2);"
        "ctx.shadowColor=glowColor;ctx.shadowBlur=10;"
        "ctx.fillStyle=color;"
        "ctx.beginPath();ctx.arc(lx,ly,3,0,Math.PI*2);ctx.fill();"
        "ctx.shadowBlur=0;"
    "}"
    "ctx.restore();"
"}"

/* ── Dibujar gráfica completa ── */
"function drawChart(){"
    "ctx.save();"
    "ctx.setTransform(2,0,0,2,0,0);" /* Retina */
    "ctx.clearRect(0,0,cW,cH);"

    "if(chartMode===0){"
        "drawSeries(omegaPts,'rgba(96,165,250,0.9)','rgba(96,165,250,0.5)','ω(t) °/s',0,1);"
    "}else if(chartMode===1){"
        "drawSeries(thetaPts,'rgba(167,139,250,0.9)','rgba(167,139,250,0.5)','θ(t) °',0,1);"
    "}else{"
        "drawSeries(omegaPts,'rgba(96,165,250,0.9)','rgba(96,165,250,0.4)','ω(t) °/s',0,0.5);"
        "drawSeries(thetaPts,'rgba(167,139,250,0.9)','rgba(167,139,250,0.4)','θ(t) °',cH*0.5,0.5);"
    "}"
    "ctx.restore();"
"}"

/* ── Actualizar gauge ── */
"function updateGauge(v){"
    "const mx=250;"
    "const c=Math.max(-mx,Math.min(mx,v));"
    "const pct=(c+mx)/(2*mx);"
    "const arc=251;"
    "document.getElementById('gaugeArc').setAttribute('stroke-dasharray',(pct*arc)+' '+arc);"
    "document.getElementById('gaugeText').textContent=v.toFixed(1);"
"}"

/* ── Fetch de datos ── */
"async function fetchData(){"
    "try{"
        "const r=await fetch('/data');"
        "const d=await r.json();"
        "if(d.error)return;"

        "const gz=d.gyro_z;"
        "const angle=d.angle_z;"

        /* ω(t) */
        "const el=document.getElementById('gyroZ');"
        "el.innerHTML=gz.toFixed(2)+'<span class=\"unit\">°/s</span>';"
        "el.className='big-value '+(gz>0.5?'positive':(gz<-0.5?'negative':'zero'));"
        "document.getElementById('rawZ').textContent=d.raw_z;"
        "document.getElementById('timestamp').textContent=d.timestamp+' ms';"

        /* Bias */
        "if(d.bias!==undefined){"
            "document.getElementById('biasZ').textContent=d.bias.toFixed(4)+' °/s';"
        "}"

        /* θ(t) */
        "document.getElementById('angleZ').innerHTML=angle.toFixed(2)+'<span class=\"unit\">°</span>';"

        /* Temperatura */
        "if(d.temp_c!==undefined){"
            "document.getElementById('tempC').textContent=d.temp_c.toFixed(1)+' °C';"
        "}"

        /* MAS parameters */
        "if(d.mas_valid){"
            "document.getElementById('masT').innerHTML=d.period.toFixed(3)+'<span class=\"mas-unit\">s</span>';"
            "document.getElementById('masF').innerHTML=d.freq.toFixed(3)+'<span class=\"mas-unit\">Hz</span>';"
            "document.getElementById('masOmegaN').innerHTML=d.omega_n.toFixed(3)+'<span class=\"mas-unit\">rad/s</span>';"
            "document.getElementById('masAmp').innerHTML=d.amplitude.toFixed(2)+'<span class=\"mas-unit\">°/s</span>';"
            "const b=document.getElementById('oscBadge');"
            "b.className='osc-badge';"
            "b.innerHTML='✓ '+d.oscillations+' oscilaciones detectadas';"
        "}else{"
            "const b=document.getElementById('oscBadge');"
            "b.className='osc-badge waiting';"
            "b.innerHTML='⏳ Esperando oscilaciones... (mueve el péndulo)';"
        "}"

        /* Gráficas */
        "omegaPts.push(gz);"
        "thetaPts.push(angle);"
        "if(omegaPts.length>MAX_PTS)omegaPts.shift();"
        "if(thetaPts.length>MAX_PTS)thetaPts.shift();"
        "drawChart();"

        /* Gauge */
        "updateGauge(gz);"

        /* FPS counter */
        "fpsCount++;"
        "const now=Date.now();"
        "if(now-lastFpsTime>=1000){"
            "document.getElementById('fps').textContent=fpsCount+' muestras/s';"
            "fpsCount=0;lastFpsTime=now;"
        "}"
    "}catch(e){console.error('Fetch error:',e)}"
"}"

/* ── Toast notification ── */
"function showToast(msg,type,duration){"
    "const t=document.getElementById('toast');"
    "t.textContent=msg;"
    "t.className='toast '+type+' show';"
    "clearTimeout(t._tid);"
    "t._tid=setTimeout(()=>{t.className='toast'},duration||3000);"
"}"

/* ── Reinicio completo ── */
"let polling=true;"
"async function doReset(){"
    "const btn=document.getElementById('btnReset');"
    "btn.classList.add('disabled');"
    "btn.innerHTML='<span class=\"spinner\"></span> Reiniciando...';"
    "try{"
        "const r=await fetch('/reset',{method:'POST'});"
        "const d=await r.json();"
        "omegaPts=[];thetaPts=[];"
        "document.getElementById('angleZ').innerHTML='0.00<span class=\"unit\">°</span>';"
        "document.getElementById('gyroZ').innerHTML='0.00<span class=\"unit\">°/s</span>';"
        "document.getElementById('gyroZ').className='big-value zero';"
        "drawChart();"
        "showToast('✓ Datos reiniciados: θ=0, ω=0, MAS limpio','success');"
    "}catch(e){showToast('✗ Error al reiniciar','error')}"
    "btn.classList.remove('disabled');"
    "btn.innerHTML='↺ Reiniciar (θ=0, ω=0, MAS)';"
"}"

/* ── Calibración manual ── */
"async function doCalibrate(){"
    "const btn=document.getElementById('btnCalib');"
    "btn.classList.add('disabled');"
    "btn.innerHTML='<span class=\"spinner\"></span> Calibrando... (~5s)';"
    "showToast('⚖ Calibrando bias — NO mover el sensor...','warning',7000);"
    "polling=false;"  /* Pausar polling durante calibración */
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
    "polling=true;"
    "btn.classList.remove('disabled');"
    "btn.innerHTML='⚖ Calibrar Bias (~5s)';"
"}"

/* ── Wrapper de fetch con pausa ── */
"async function safeFetch(){"
    "if(polling)await fetchData();"
"}"

/* ── Loop principal: 5 Hz ── */
"setInterval(safeFetch,200);"
"fetchData();"

"</script>"
"</body></html>";

/* ══════════════════════════════════════════════════════════════
 *  HTTP Handlers
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
 * @brief Handler GET /data — Retorna JSON con datos del giroscopio y MAS
 */
static esp_err_t data_get_handler(httpd_req_t *req)
{
    mpu6050_data_t sd = {0};
    esp_err_t ret = mpu6050_read_gyro_z(&sd);

    float temp_c = 0.0f;
    mpu6050_read_temperature(&temp_c);

    char json_buf[512];

    if (ret == ESP_OK) {
        snprintf(json_buf, sizeof(json_buf),
            "{\"gyro_z\":%.3f,\"raw_z\":%d,\"angle_z\":%.3f,"
            "\"timestamp\":%lld,\"temp_c\":%.1f,\"bias\":%.4f,"
            "\"mas_valid\":%s,\"period\":%.4f,\"freq\":%.4f,"
            "\"omega_n\":%.4f,\"amplitude\":%.3f,\"oscillations\":%lu}",
            sd.gyro_z_dps,
            (int)sd.gyro_z_raw,
            sd.angle_z_deg,
            (long long)sd.timestamp_ms,
            temp_c,
            sd.bias_z_dps,
            sd.mas_valid ? "true" : "false",
            sd.period_s,
            sd.frequency_hz,
            sd.omega_n,
            sd.amplitude_dps,
            (unsigned long)sd.oscillations
        );
    } else {
        snprintf(json_buf, sizeof(json_buf),
            "{\"error\":\"sensor_read_failed\",\"code\":%d}", (int)ret);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, json_buf, strlen(json_buf));
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

static const httpd_uri_t uri_data = {
    .uri       = "/data",
    .method    = HTTP_GET,
    .handler   = data_get_handler,
    .user_ctx  = NULL,
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
    config.max_open_sockets = 5;    /* LWIP_MAX_SOCKETS=8 en sdkconfig, HTTP usa 3 internos → max 5 */
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  Iniciando servidor HTTP en puerto %d", config.server_port);

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al iniciar servidor: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Registrar handlers */
    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_data);
    httpd_register_uri_handler(s_server, &uri_reset);
    httpd_register_uri_handler(s_server, &uri_calibrate);

    ESP_LOGI(TAG, "  Servidor HTTP iniciado ✓");
    ESP_LOGI(TAG, "  Endpoints:");
    ESP_LOGI(TAG, "    GET  /          → Interfaz web");
    ESP_LOGI(TAG, "    GET  /data      → JSON giroscopio + MAS");
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

    esp_err_t ret = httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "Servidor HTTP detenido");
    return ret;
}
