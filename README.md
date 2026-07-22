# 🎹 Piano Tiles BLE - TD P3

[![Jugar](https://img.shields.io/badge/🎮-JUGAR_AHORA-brightgreen?style=for-the-badge)](https://yamidgt.github.io/piano-tiles-TD-P3/)

---

## 📱 Escanea y juega

|                           |                                                                                                     |
| ------------------------- | --------------------------------------------------------------------------------------------------- |
| ![Código QR](qr-code.png) | **Escanea este código QR** desde tu móvil para jugar al instante, o haz clic en el botón de arriba. |


---

## 📖 Descripción

**Piano Tiles BLE** es un juego rítmico inspirado en *Piano Tiles* que combina la jugabilidad clásica con conectividad Bluetooth BLE para interactuar con un piano físico basado en ESP32-S3.

El juego desafía tu velocidad y precisión: las teclas (tiles) caen por la pantalla y debes presionar la nota correspondiente en el momento exacto.

---

## 🎮 Características principales

| Característica                    | Descripción                                                                                     |
| --------------------------------- | ----------------------------------------------------------------------------------------------- |
| 🎵 **Sistema de canciones**       | Múltiples melodías integradas: Twinkle Twinkle, Cumpleaños Feliz, Oda a la Alegría, entre otras |
| 🎨 **Personalización de colores** | Cambia el aspecto visual de las teclas con 8 presets o colores personalizados                   |
| 📊 **Telemetría en tiempo real**  | Gráficas que muestran actividad, volumen, carpeta seleccionada y notas presionadas (vía BLE)    |
| 🏆 **Rankings locales**           | Guarda tus puntuaciones más altas por modo de juego en el navegador                             |
| 🎹 **Bluetooth BLE**              | Conecta con un piano físico ESP32-S3 para jugar con teclas reales                               |
| ⌨️ **Teclado PC**                 | También puedes jugar con las teclas A S D F G H J K                                             |

---

## 🕹️ Modos de juego

| Modo            | Descripción                                          |
| --------------- | ---------------------------------------------------- |
| **🎯 PUNTOS**   | Acumula puntos mediante combos. Fallar resta puntos. |
| **💀 UN FALLO** | Un solo error y el juego termina. ¡Precisión máxima! |

### Dentro del modo PUNTOS

* **Infinito** → Juega sin límite de tiempo
* **1 Minuto** → Contrarreloj, consigue la máxima puntuación

---

## 🎨 Paleta de colores disponible

| Color   | Preview |
| ------- | ------- |
| Cyan    | 🔵      |
| Verde   | 🟢      |
| Rojo    | 🔴      |
| Dorado  | 🟡      |
| Morado  | 🟣      |
| Naranja | 🟠      |
| Rosa    | 🩷      |
| Blanco  | ⚪       |

Puedes seleccionarlos desde el **panel lateral** o desde el **menú principal**.

---

## 📡 Conectividad BLE con ESP32-S3

El juego se comunica con un piano físico mediante **Web Bluetooth**.

### UUIDs utilizados

| UUID                                   | Función                         |
| -------------------------------------- | ------------------------------- |
| `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | Servicio principal              |
| `6e400002-b5a3-f393-e0a9-e50e24dcca9e` | Envío de colores (PC → ESP32)   |
| `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | Recepción de notas (ESP32 → PC) |

### Protocolo de comunicación

**Envío de nota desde ESP32:**

```
Nota:3
```

o simplemente:

```
3
```

**Envío de telemetría:**

```
STATUS:ACT=1&NOT=5&VOL=20&FOL=1
```

**Recepción de color (desde el juego):**

```
COLOR:0,212,255
```

---

## 🔩 Firmware del piano físico (ESP32-S3)

| Versión | Carpeta | Motor de audio | Estado |
| ------- | ------- | --------------- | ------ |
| **V7 (actual)** | [`SmartPiano_V7/`](SmartPiano_V7/) | I2S polifónico propio (DAC UDA1334A + amplificador PAM8403) | ✅ En uso |
| V6 | [`prueba_sonido_8botones/SmartPiano_V6.ino`](prueba_sonido_8botones/SmartPiano_V6.ino) | DFPlayer Mini (una sola nota a la vez) | 🗄️ Versión anterior |
| Prototipo de audio | [`prueba_sonido_8botones/`](prueba_sonido_8botones/) | Pruebas del motor I2S en protoboard | 🧪 Base para V7 |

El DFPlayer Mini (V6) solo reproducía una nota a la vez y con más latencia. El firmware **V7** reemplaza ese módulo por un motor de audio I2S corriendo directamente en el ESP32-S3, que mezcla hasta 8 sonidos simultáneos (polifonía real: se pueden tocar varias teclas a la vez) manteniendo el resto del sistema sin cambios (BLE, NeoPixels, sensor láser VL53L0X, PIR, servo y todos los pines de botones/sensores).

Por ahora solo está cargado el instrumento **Piano**; la estructura de sonidos ya está preparada para añadir **Flauta** y **Guitarra** más adelante sin rediseñar el firmware.

---

## 🖥️ Controles por teclado

| Tecla | Nota |
| ----- | ---- |
| A     | DO   |
| S     | RE   |
| D     | MI   |
| F     | FA   |
| G     | SOL  |
| H     | LA   |
| J     | SI   |
| K     | DO²  |

`ESC` → Pausa / Reanudar

---

## 🧪 Prueba el juego

👉 **[JUGAR AHORA](https://yamidgt.github.io/piano-tiles-TD-P3/)** 👈

### Requisitos para BLE

* Navegador: **Chrome** o **Edge** (escritorio o Android)
* **NO compatible** con iOS (Safari no soporta Web Bluetooth)

---

## 📂 Estructura del proyecto

```
piano-tiles-TD-P3/
├── index.html
├── qr-code.png
├── README.md
├── prueba_sonido_8botones/     # Prototipo de audio I2S + firmware V6 (histórico, DFPlayer Mini)
│   ├── prueba_sonido_8botones.ino
│   ├── SmartPiano_V6.ino
│   └── audio_data1.h ... audio_data8.h
└── SmartPiano_V7/              # Firmware actual del piano físico (audio I2S polifónico)
    ├── SmartPiano_V7.ino
    └── audio_piano_1.h ... audio_piano_8.h
```

---


## 🛠️ Tecnologías utilizadas

* HTML5
* CSS3
* JavaScript (ES6+)
* Web Bluetooth API
* Canvas API
* Web Audio API

---

## 📈 Telemetría en tiempo real

Cuando el piano ESP32-S3 envía datos vía BLE, el juego muestra:

* 📊 Actividad PIR (movimiento detectado)
* 📏 Volumen (distancia inferida por sensor VL53L0X)
* 🎹 Carpeta o instrumento seleccionado (1-4)
* 🎵 Historial de notas (gráfico de barras y línea temporal)

---

## 🏆 Rankings

Las puntuaciones se guardan en `localStorage` y se clasifican en:

* **Puntos Infinito** (`points_inf`)
* **Puntos 1 Minuto** (`points_1min`)
* **Un Fallo** (`death`)

---

## 🐛 Reportar problemas

Si encuentras algún error o tienes sugerencias, abre un **Issue** en este repositorio.

---

## 📄 Licencia

Este proyecto es de uso educativo y personal.

---

## 👨‍💻 Autor

Desarrollado como parte del proyecto **TD P3 - Integración de Hardware y Software con ESP32-S3**.

🌟 ¡Diviértete y logra el puntaje más alto!

[![Volver al juego](https://img.shields.io/badge/🔙-VOLVER_AL_JUEGO-cyan?style=for-the-badge)](https://yamidgt.github.io/piano-tiles-TD-P3/)
