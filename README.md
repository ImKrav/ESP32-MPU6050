# Péndulo de Torsión — Giroscopio Eje Z

Proyecto de ESP-IDF v6.0 que lee la velocidad angular del eje Z del giroscopio del MPU-6050,
conectado a un ESP32 WROOM 32D, y muestra los datos en tiempo real en una interfaz web
hosteada directamente desde el microcontrolador.

## Descripción

Este sistema utiliza un sensor MPU-6050 para medir la velocidad angular en el eje Z
de un péndulo de torsión. Los datos se transmiten en **tiempo real vía WebSocket a ~50 Hz**
(push desde el ESP32) hacia una interfaz web premium embebida con visualización de alto
rendimiento.

### Características principales

- **WebSocket en tiempo real** — datos push a 50 Hz, sin polling
- **Gráfica temporal de alto rendimiento** de ω(t) y θ(t) con 300 puntos (canvas optimizado)
- **Tres modos de visualización** — ω(t), θ(t), o ambas simultáneamente
- **Gauge SVG** de velocidad angular con gradiente de colores
- **Ángulo acumulado** por integración numérica (trapezoidal)
- **Parámetros del MAS** — período T, frecuencia f, ωₙ, amplitud
- **Detección automática de oscilaciones** con badge de estado
- **Temperatura** del sensor en tiempo real
- **Exportación de datos a CSV** — descarga todas las muestras grabadas
- **Control de grabación** — Rec/Pausa con contador de muestras
- **Reconexión automática** del WebSocket con backoff exponencial
- **Interfaz oscura premium** con gradientes, animaciones y diseño responsivo

## Hardware Requerido

| Componente | Descripción |
|---|---|
| ESP32 WROOM 32D Devkit | Microcontrolador principal |
| MPU-6050 | Sensor giroscopio/acelerómetro (módulo breakout) |
| Cables Dupont | 4 cables hembra-hembra |
| Cable USB | Para alimentación y programación |

## Conexiones

```
ESP32 WROOM 32D          MPU-6050
═══════════════          ════════
    3.3V  ──────────────  VCC
    GND   ──────────────  GND
    GPIO 21 (SDA) ──────  SDA
    GPIO 22 (SCL) ──────  SCL
    GND   ──────────────  AD0  (dirección I2C = 0x68)
```

> ⚠️ **Importante:** Conectar VCC del MPU-6050 a **3.3V**, NO a 5V.
> Aunque algunos módulos tienen regulador, es más seguro usar 3.3V.

## Software Requerido

- **ESP-IDF v6.0** — [Guía de instalación](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)
- **Python 3.8+** (incluido con ESP-IDF)
- **CMake 3.22+** (incluido con ESP-IDF)

## Compilación y Flasheo

```bash
# 1. Configurar el target (solo la primera vez)
idf.py set-target esp32

# 2. (Opcional) Configurar opciones del proyecto
idf.py menuconfig

# 3. Compilar
idf.py build

# 4. Flashear al ESP32 (reemplazar COMx con tu puerto)
idf.py -p COM3 flash

# 5. Ver el monitor serial
idf.py -p COM3 monitor

# O compilar + flashear + monitor en un solo comando:
idf.py -p COM3 flash monitor
```

> ⚠️ **Si modificas `sdkconfig.defaults` o la configuración de red**, ejecuta
> `idf.py fullclean` antes de compilar para regenerar el `sdkconfig` correctamente.

## Uso

1. **Flashear** el firmware al ESP32
2. **Conectar** al WiFi creado por el ESP32:
   - **SSID:** `PenduloTorsion`
   - **Contraseña:** `fisica2026`
3. **Abrir** en el navegador: `http://192.168.4.1`
4. La interfaz conecta automáticamente por WebSocket y muestra los datos a 50 Hz

> 💡 El ESP32 crea su **propia red WiFi** (modo SoftAP), no necesitas
> un router ni conexión a internet. Cualquier dispositivo (celular,
> laptop, tablet) puede conectarse directamente.

### Controles de la interfaz

| Control | Función |
|---|---|
| **Tabs ω / θ / Ambas** | Cambia el modo de visualización de la gráfica |
| **↺ Reiniciar** | Reinicia el ángulo acumulado, MAS y gráficas (POST /reset) |
| **⚖ Calibrar Bias** | Recalibra el bias del giroscopio (~5s, sensor en reposo) |
| **● Rec / ⏸ Pausado** | Inicia/pausa la grabación de muestras en el buffer |
| **📥 Exportar CSV** | Descarga todas las muestras grabadas como archivo `.csv` |

### Formato del CSV exportado

El archivo exportado contiene las siguientes columnas:

```
Timestamp_ms, Gyro_Z_dps, Raw_Z, Angulo_Z_deg, Bias_dps, Temp_C,
MAS_Valido, Periodo_s, Frecuencia_Hz, Omega_n_rads, Amplitud_dps, Oscilaciones
```

El nombre del archivo se genera automáticamente con la fecha y hora:
`pendulo_torsion_YYYYMMDD_HHmmss.csv`

## Estructura del Proyecto

```
ESP32-MPU6050/
├── CMakeLists.txt          # Configuración CMake del proyecto
├── sdkconfig.defaults      # Configuración por defecto del SDK
├── README.md               # Este archivo
└── main/
    ├── CMakeLists.txt      # CMake del componente principal
    ├── main.c              # Punto de entrada (app_main)
    ├── mpu6050.h           # Header del driver MPU-6050
    ├── mpu6050.c           # Driver I2C (nueva API v6.0)
    ├── wifi_softap.h       # Header WiFi SoftAP (modo usado)
    ├── wifi_softap.c       # Configuración WiFi AP
    ├── wifi_sta.h          # Header WiFi Station (alternativo)
    ├── wifi_sta.c          # WiFi STA — conectar a red existente
    ├── web_server.h        # Header del servidor web
    └── web_server.c        # Servidor HTTP + WebSocket + UI embebida (~47 KB)
```

## Arquitectura

```
┌────────────────────────────────────────────────────────────────┐
│                     ESP32 (Firmware)                           │
│                                                                │
│  ┌──────────────┐     ┌──────────────────┐    ┌────────────┐  │
│  │  MPU-6050    │─I2C─│  ws_push_task     │───▶│ WebSocket  │──┼──▶ Navegador
│  │  (100 Hz)    │     │  (FreeRTOS, 50Hz) │    │ /ws push   │  │
│  └──────────────┘     └──────────────────┘    └────────────┘  │
│                                                                │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  HTTP Server (esp_http_server)                          │   │
│  │    GET  /          → Página HTML+CSS+JS (~47 KB chunks) │   │
│  │    WS   /ws        → WebSocket datos tiempo real        │   │
│  │    POST /reset     → Reiniciar ángulo + MAS             │   │
│  │    POST /calibrate → Recalibrar bias del gyro           │   │
│  └─────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────┐
│                   Navegador (Frontend)                         │
│                                                                │
│  WebSocket.onmessage(JSON)                                     │
│       │                                                        │
│       ├──▶ handleData()  → Actualiza DOM (refs cacheadas)      │
│       ├──▶ schedDraw()   → requestAnimationFrame → drawChart() │
│       ├──▶ updateGauge() → SVG arc dasharray                   │
│       └──▶ exportBuf[]   → Acumula para CSV (si grabando)      │
│                                                                │
│  drawChart() ──▶ Canvas 2D (opaco, sin shadowBlur)             │
│                  drawSeries() × 1 o 2 según modo               │
└────────────────────────────────────────────────────────────────┘
```

### Flujo de datos

1. La tarea FreeRTOS `ws_push_task` lee el MPU-6050 cada 20 ms (50 Hz)
2. Serializa la lectura a JSON compacto (claves de 2 letras: `gz`, `az`, `rz`, etc.)
3. Envía el frame por WebSocket a todos los clientes conectados (máx. 4)
4. El frontend recibe el dato, actualiza las variables de estado y programa un redibujado
5. `requestAnimationFrame` ejecuta `drawChart()` una vez por frame (~60 FPS visual)

## API del Sensor

### Registros MPU-6050 utilizados

| Registro | Dirección | Uso |
|---|---|---|
| WHO_AM_I | 0x75 | Verificación del sensor (retorna 0x68) |
| PWR_MGMT_1 | 0x6B | Despertar sensor (limpiar bit SLEEP) |
| GYRO_CONFIG | 0x1B | Sensibilidad: ±250 °/s |
| CONFIG | 0x1A | DLPF: BW=42Hz (filtro de ruido) |
| SMPLRT_DIV | 0x19 | Sample rate: 100 Hz |
| GYRO_ZOUT_H/L | 0x47/0x48 | Datos del eje Z |

### Conversión de datos

```
Valor Raw: int16_t = (GYRO_ZOUT_H << 8) | GYRO_ZOUT_L
Velocidad Angular: gyro_z = raw / 131.0 [°/s]  (para ±250 °/s)
Ángulo: angle += gyro_z × Δt [°]  (integración numérica)
```

## Endpoints

| Endpoint | Método | Protocolo | Descripción |
|---|---|---|---|
| `/` | GET | HTTP | Página HTML con interfaz completa (~47 KB, envío chunked) |
| `/ws` | GET | WebSocket | Datos en tiempo real a 50 Hz (push, JSON compacto) |
| `/reset` | POST | HTTP | Reinicia el ángulo acumulado, MAS y gráficas |
| `/calibrate` | POST | HTTP | Recalibra el bias del giroscopio (~5s, sensor en reposo) |

### Formato del JSON WebSocket

```json
{
  "gz": 12.34,      // Gyro Z (°/s) filtrado
  "rz": 1617,       // Raw Z (int16)
  "az": 45.67,      // Ángulo Z acumulado (°)
  "ts": 123456,     // Timestamp (ms)
  "bi": 0.1234,     // Bias (°/s)
  "tc": 25.3,       // Temperatura (°C)
  "mv": true,       // MAS válido
  "pe": 1.234,      // Período (s)
  "fr": 0.810,      // Frecuencia (Hz)
  "on": 5.092,      // ωₙ (rad/s)
  "am": 15.20,      // Amplitud (°/s)
  "oc": 5           // Oscilaciones detectadas
}
```

## Optimizaciones de Rendimiento

### Backend (ESP32)
- **WebSocket push** reemplaza polling HTTP — elimina overhead de petición/respuesta
- **Tarea dedicada** en Core 1 (`ws_push_task`) — no bloquea WiFi ni HTTP
- **JSON compacto** con claves de 2 letras — minimiza el ancho de banda
- **Envío chunked** del HTML — evita saturar el buffer TX (~47 KB total)
- **`max_open_sockets = 5`** — respeta el límite `LWIP_MAX_SOCKETS=10`

### Frontend (Navegador)
- **Canvas opaco** (`{alpha:false}`) — composición más rápida del navegador
- **Sin `shadowBlur`** — eliminado el efecto de sombra gaussiana (~10x faster)
- **Glow por trazo** — efecto visual con doble trazo semitransparente
- **DPR limitado a 1.5** — evita buffers innecesarios en pantallas 4K
- **DOM cacheado** — todas las referencias `getElementById` fuera del bucle de 50 Hz
- **`textContent` en vez de `innerHTML`** — evita re-parsing HTML en cada frame
- **`requestAnimationFrame` con throttle** — un solo redibujado por frame del monitor
- **Datos desacoplados del dibujo** — los datos se acumulan a 50 Hz, el canvas dibuja a vsync

## Notas Técnicas

- **I2C Driver:** Usa la nueva API `driver/i2c_master.h` de ESP-IDF v6.0 (el driver legacy está deprecado)
- **WiFi Mode:** SoftAP — el ESP32 crea su red, IP fija `192.168.4.1`
- **Filtro DLPF:** Configurado a 42 Hz para reducir ruido en mediciones del péndulo
- **Integración:** El ángulo se obtiene por integración numérica simple (trapezoidal)
- **Data Rate:** WebSocket push a 50 Hz (20 ms por muestra)
- **Visualización:** Canvas 2D con hasta 300 puntos, auto-rango dinámico
- **Seguridad WiFi:** WPA2-PSK con PMF habilitado
- **Clientes WS:** Máximo 4 clientes WebSocket simultáneos
- **Reconexión:** Automática con backoff exponencial (500 ms → 5 s)

## Troubleshooting

### Error `ESP_ERR_INVALID_ARG` al iniciar el servidor

```
Config option max_open_sockets is too large (max allowed 5, 3 sockets used by HTTP server internally)
```

**Causa:** `max_open_sockets` excede el límite de `LWIP_MAX_SOCKETS` (por defecto 10).
El servidor HTTP usa 3 sockets internamente, así que el máximo es `10 - 3 = 7`, pero
configuramos `5` para dejar margen.

**Solución:** Ejecutar `idf.py fullclean` y luego `idf.py build` para regenerar el `sdkconfig`.

### Las gráficas se ven lentas o con lag

El motor de renderizado ya está optimizado. Si persiste el problema:
1. Verificar que el navegador no tenga muchas pestañas abiertas
2. Usar Chrome o Edge (mejor rendimiento de Canvas 2D)
3. Reducir `MAX_PTS` en el JavaScript si el dispositivo es de gama baja

## Licencia

Proyecto académico — Práctica de Física 2026
