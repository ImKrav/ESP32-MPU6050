# Péndulo de Torsión — Giroscopio Eje Z

Proyecto de ESP-IDF v6.0 que lee la velocidad angular del eje Z del giroscopio del MPU-6050,
conectado a un ESP32 WROOM 32D, y muestra los datos en tiempo real en una interfaz web
hosteada directamente desde el microcontrolador.

## Descripción

Este sistema utiliza un sensor MPU-6050 para medir la velocidad angular en el eje Z
de un péndulo de torsión. Los datos se transmiten a un servidor web embebido en el
ESP32 que muestra:

- **Velocidad angular** (°/s) en tiempo real
- **Gráfica temporal** de los últimos 200 puntos
- **Gauge visual** con gradiente de colores
- **Ángulo acumulado** por integración numérica
- **Parámetros del MAS** (período T, frecuencia f, ωₙ)
- **Temperatura** del sensor

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

## Uso

1. **Flashear** el firmware al ESP32
2. **Conectar** al WiFi creado por el ESP32:
   - **SSID:** `PenduloTorsion`
   - **Contraseña:** `fisica2026`
3. **Abrir** en el navegador: `http://192.168.4.1`
4. La interfaz mostrará los datos del giroscopio en tiempo real

> 💡 El ESP32 crea su **propia red WiFi** (modo SoftAP), no necesitas
> un router ni conexión a internet. Cualquier dispositivo (celular,
> laptop, tablet) puede conectarse directamente.

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
    └── web_server.c        # Servidor HTTP + UI embebida
```

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

## Endpoints HTTP

| Endpoint | Método | Respuesta |
|---|---|---|
| `/` | GET | Página HTML con interfaz completa |
| `/data` | GET | JSON: `{"gyro_z", "raw_z", "angle_z", "timestamp", "temp_c", "bias", "mas_valid", ...}` |
| `/reset` | POST | Reinicia el ángulo acumulado, MAS y gráficas |
| `/calibrate` | POST | Recalibra el bias del giroscopio (~5s, sensor en reposo) |

## Notas Técnicas

- **I2C Driver:** Usa la nueva API `driver/i2c_master.h` de ESP-IDF v6.0 (el driver legacy está deprecado)
- **WiFi Mode:** SoftAP — el ESP32 crea su red, IP fija `192.168.4.1`
- **Filtro DLPF:** Configurado a 42 Hz para reducir ruido en mediciones del péndulo
- **Integración:** El ángulo se obtiene por integración numérica simple (trapezoidal)
- **Refresh Rate:** La interfaz web actualiza datos 5 veces por segundo (200ms)
- **Seguridad WiFi:** WPA2-PSK con PMF habilitado

## Licencia

Proyecto académico — Práctica de Física 2026
