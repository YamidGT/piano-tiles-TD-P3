/*
  SMART PIANO PREMIUM V7 - Motor de audio I2S polifónico (reemplaza DFPlayer Mini)
  ESP32-S3-N16R8 + BLE + NeoPixel + VL53L0X + PIR + Servo

  Motor de audio tomado de prueba_sonido_8botones (I2S "i2s_std", Arduino core 3.x /
  ESP-IDF 5.x) en vez del DFPlayer Mini: permite varias teclas sonando a la vez y
  reacciona más rápido que el DFPlayer.

  Pines: se mantienen EXACTAMENTE los mismos que SmartPiano_V6 para botones, sensores,
  servo y LEDs. El DFPlayer usaba los pines 16 (RX) y 17 (TX); al quitarlo quedan
  libres y se reutilizan para I2S (BCLK/LRC). Solo se añade un pin nuevo (15) para DOUT.

  Multi-instrumento: por ahora solo está cargado el Piano (instrumento 1, carpetaActiva=1).
  Flauta (2) y Guitarra (3) están reservadas en las tablas SONIDOS/LONGITUDES: cuando se
  generen sus audio_flauta_*.h / audio_guitarra_*.h basta con rellenar esas filas en
  setup(). Mientras no existan, tocarNota() cae de vuelta al sonido de Piano.
*/

#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "driver/i2s_std.h"
#include "audio_piano_1.h"
#include "audio_piano_2.h"
#include "audio_piano_3.h"
#include "audio_piano_4.h"
#include "audio_piano_5.h"
#include "audio_piano_6.h"
#include "audio_piano_7.h"
#include "audio_piano_8.h"

#define BLE_SERVICE_UUID  "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_TX_UUID  "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_RX_UUID  "6e400002-b5a3-f393-e0a9-e50e24dcca9e"


volatile bool movimientoDetectado = false;
bool          pianoActivo         = false;
unsigned long ultimoMovimiento    = 0;
unsigned long ultimaNota          = 0;
int           ultimaNotaTocadaNum = 0;

const unsigned long TIEMPO_INACTIVO = 30000;
const unsigned long TIEMPO_SERVO    = 1200;

int     carpetaActiva = 1;
int     brilloLed     = 100;
int     volumenActual = 20;
uint8_t rBase = 100, gBase = 100, bBase = 100;

unsigned long ultimoEnvioNube           = 0;
bool          pendienteEnvioInmediato   = false;


bool estadoAnteriorBoton[8] = { HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH };
unsigned long ultimoTiempoBoton[8]      = { 0, 0, 0, 0, 0, 0, 0, 0 };
const unsigned long DEBOUNCE_MS = 150;


#define LED_PIN  21
#define NUM_LEDS  8
Adafruit_NeoPixel tiraLed(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);


Adafruit_VL53L0X lox = Adafruit_VL53L0X();


const int botones[8] = { 11, 12, 48, 10, 7, 5, 47, 6 };
#define BOTON_CARPETA_PIN 13
#define PIR_PIN    4
#define SERVO_PIN  18
#define SERVO_FREQ 50
#define SERVO_RES  10
const int SERVO_STOP = 77;
const int SERVO_MOVE = 95;


// --- Audio I2S (reemplaza al DFPlayer Mini) ---
#define I2S_BCLK_PIN    16   // antes MP3_RX, libre al quitar el DFPlayer
#define I2S_LRC_PIN     17   // antes MP3_TX, libre al quitar el DFPlayer
#define I2S_DOUT_PIN    15   // único pin nuevo que se necesita

#define SAMPLE_RATE      22050
#define WAV_HEADER_SIZE  44
#define CHUNK_SIZE       256
#define NUM_TECLAS       8
#define FADE_SAMPLES     100   // ~4.5 ms a 22050 Hz: desvanecimiento final suave
#define TRIM_END_SAMPLES 260   // recorta el "click" de fin de grabación de los .wav originales

#define NUM_INSTRUMENTOS 4     // 1=Piano (listo), 2=Flauta, 3=Guitarra, 4=reservado

struct EstadoSonido {
  const unsigned char* data;
  size_t length;
  volatile size_t position;
  volatile bool playing;
};

EstadoSonido sonidos[NUM_TECLAS];
i2s_chan_handle_t tx_handle;
TaskHandle_t tareaMezcladoraHandle = NULL;

// Banco de sonidos por instrumento. Las filas de Flauta/Guitarra quedan en NULL
// hasta que se generen sus audio_*.h; tocarNota() usa el Piano como respaldo mientras tanto.
const unsigned char* SONIDOS[NUM_INSTRUMENTOS][NUM_TECLAS] = { { nullptr } };
size_t                LONGITUDES[NUM_INSTRUMENTOS][NUM_TECLAS] = { { 0 } };


void ledsBase();


BLEServer* pBLEServer   = nullptr;
BLECharacteristic* pTxChar      = nullptr;
bool               bleConnected = false;

class BLEServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer*)    override {
    bleConnected = true;
    Serial.println("BLE: cliente conectado ✓");
  }
  void onDisconnect(BLEServer*) override {
    bleConnected = false;
    Serial.println("BLE: desconectado — reiniciando advertising");
    BLEDevice::startAdvertising();
  }
};

class BLERxCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String msg = c->getValue().c_str();
    msg.trim();
    Serial.printf("BLE RX: %s\n", msg.c_str());


    if (msg.startsWith("COLOR:")) {
      int r, g, b;
      if (sscanf(msg.c_str(), "COLOR:%d,%d,%d", &r, &g, &b) == 3) {
        rBase = (uint8_t)constrain(r, 0, 255);
        gBase = (uint8_t)constrain(g, 0, 255);
        bBase = (uint8_t)constrain(b, 0, 255);
        if (pianoActivo) ledsBase();
        Serial.printf("Color LED actualizado vía BLE → R:%d G:%d B:%d\n", rBase, gBase, bBase);
      }
    }

    else if (msg.startsWith("VOL:")) {
      int v = msg.substring(4).toInt();
      if (v >= 5 && v <= 30) {
        volumenActual = v;
        Serial.printf("Volumen actualizado vía BLE → %d\n", volumenActual);
      }
    }

    else if (msg.startsWith("FOLDER:")) {
      int f = msg.substring(7).toInt();
      if (f >= 1 && f <= 4) {
        carpetaActiva = f;
        ultimoMovimiento = millis();
        pendienteEnvioInmediato = true;
        Serial.printf("Instrumento cambiado vía BLE → Carpeta %02d\n", carpetaActiva);
        if (pianoActivo) ledsBase();
      }
    }
  }
};


void IRAM_ATTR pirISR() {
  movimientoDetectado = true;
}


void ledsBase() {
  for (int i = 0; i < NUM_LEDS; i++)
    tiraLed.setPixelColor(i, tiraLed.Color(rBase, gBase, bBase));
  tiraLed.show();
  pinMode(48, INPUT_PULLUP);
}

void apagarLeds() {
  tiraLed.clear();
  tiraLed.show();
  pinMode(48, INPUT_PULLUP);
}


void animacionWakeUp() {
  for (int j = 0; j < 2; j++) {
    for (int i = 0; i < NUM_LEDS; i++) tiraLed.setPixelColor(i, tiraLed.Color(0, 255, 0));
    tiraLed.show(); delay(200);
    tiraLed.clear(); tiraLed.show(); delay(200);
  }
  ledsBase();
}

void animacionSleep() {
  for (int j = 0; j < 2; j++) {
    for (int i = 0; i < NUM_LEDS; i++) tiraLed.setPixelColor(i, tiraLed.Color(255, 0, 0));
    tiraLed.show(); delay(250);
    tiraLed.clear(); tiraLed.show(); delay(250);
  }
  apagarLeds();
}


void enviarNotaBLE(int notaNum) {
  if (!bleConnected || pTxChar == nullptr) return;
  String msg = "Nota: " + String(notaNum) + "\n";
  pTxChar->setValue(msg.c_str());
  pTxChar->notify();
  Serial.printf("BLE TX: %s", msg.c_str());
}


// --- Motor de audio I2S ---

static void iniciarI2S() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  esp_err_t err;

  err = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
  Serial.printf("i2s_new_channel: %s\n", esp_err_to_name(err));
  if (err != ESP_OK) { Serial.println("!!! FALLO al crear canal I2S !!!"); return; }

  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_BCLK_PIN,
      .ws   = (gpio_num_t)I2S_LRC_PIN,
      .dout = (gpio_num_t)I2S_DOUT_PIN,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };

  err = i2s_channel_init_std_mode(tx_handle, &std_cfg);
  Serial.printf("i2s_channel_init_std_mode: %s\n", esp_err_to_name(err));
  if (err != ESP_OK) { Serial.println("!!! FALLO al inicializar modo std I2S !!!"); return; }

  err = i2s_channel_enable(tx_handle);
  Serial.printf("i2s_channel_enable: %s\n", esp_err_to_name(err));
  if (err != ESP_OK) { Serial.println("!!! FALLO al habilitar canal I2S !!!"); return; }

  Serial.println("I2S inicializado correctamente.");
}

// Dispara la nota `nota` con el sonido del instrumento activo (carpetaActiva).
// Si ese instrumento aún no tiene datos cargados (flauta/guitarra pendientes),
// usa el Piano como respaldo para que el sistema siga sonando mientras se completan.
void dispararNota(int nota) {
  int instrumento = carpetaActiva - 1;
  const unsigned char* datos = SONIDOS[instrumento][nota];
  size_t len = LONGITUDES[instrumento][nota];

  if (datos == nullptr) {
    datos = SONIDOS[0][nota];
    len   = LONGITUDES[0][nota];
  }
  if (datos == nullptr) return;

  sonidos[nota].data     = datos;
  sonidos[nota].length   = len;
  sonidos[nota].position = 0;
  sonidos[nota].playing  = true;
}

static void tareaMezcladora(void *pvParameters) {
  (void)pvParameters;
  int16_t buffer[CHUNK_SIZE * 2];
  size_t bytesWritten;

  for (;;) {
    bool hayAudioActivo = false;

    for (size_t n = 0; n < CHUNK_SIZE; n++) {
      int32_t mezcla = 0;
      int activos = 0;

      for (int s = 0; s < NUM_TECLAS; s++) {
        if (sonidos[s].playing) {
          if (sonidos[s].position < sonidos[s].length) {
            // Conversión PCM 8-bit unsigned a 16-bit signed
            int32_t muestra = ((int16_t)sonidos[s].data[sonidos[s].position] - 128) << 8;

            // Fade-out suave en las últimas muestras para evitar el "click" al cortar
            size_t restantes = sonidos[s].length - sonidos[s].position;
            if (restantes < FADE_SAMPLES) {
              muestra = muestra * (int32_t)restantes / (int32_t)FADE_SAMPLES;
            }

            mezcla += muestra;
            sonidos[s].position++;
            activos++;
            hayAudioActivo = true;
          } else {
            sonidos[s].playing = false;
          }
        }
      }

      // Normalización para prevenir clipping al presionar varias teclas a la vez
      if (activos > 1) mezcla = mezcla / activos;

      // Volumen por software: el DFPlayer ya no existe, así que se aplica aquí
      // (mismo rango 5-30 que antes controlaba mp3.volume())
      mezcla = mezcla * volumenActual / 30;

      if (mezcla > 32767) mezcla = 32767;
      if (mezcla < -32768) mezcla = -32768;

      int16_t muestra = (int16_t)mezcla;
      buffer[2 * n]     = muestra; // Izquierdo
      buffer[2 * n + 1] = muestra; // Derecho
    }

    i2s_channel_write(tx_handle, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);

    if (!hayAudioActivo) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}


void tocarNota(int nota) {
  dispararNota(nota);
  ultimaNota          = millis();
  ultimoMovimiento    = millis();
  ultimaNotaTocadaNum = nota + 1;
  ledcWrite(SERVO_PIN, SERVO_MOVE);
  tiraLed.setPixelColor(nota, tiraLed.Color(0, 0, 255));
  tiraLed.show();
  pinMode(48, INPUT_PULLUP);
  enviarNotaBLE(nota + 1);
  Serial.printf("Nota %d -> Instrumento %d (Vol: %d)\n", ultimaNotaTocadaNum, carpetaActiva, volumenActual);
}


void procesarVolumenLaser() {
  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false);
  if (measure.RangeStatus != 4) {
    int d = measure.RangeMilliMeter;
    if (d >= 40 && d <= 250) {
      ultimoMovimiento = millis();
      int nv = constrain(map(d, 40, 250, 5, 30), 5, 30);
      if (abs(nv - volumenActual) > 1) {
        volumenActual = nv;
        pendienteEnvioInmediato = true;
      }
    }
  }
}


void revisarCambioCarpeta() {
  if (digitalRead(BOTON_CARPETA_PIN) == LOW) {
    vTaskDelay(pdMS_TO_TICKS(50));
    if (digitalRead(BOTON_CARPETA_PIN) == LOW) {
      if (++carpetaActiva > 4) carpetaActiva = 1;
      ultimoMovimiento = millis();
      pendienteEnvioInmediato = true;
      Serial.printf("Instrumento -> Carpeta %02d\n", carpetaActiva);
      for (int f = 0; f < carpetaActiva; f++) {
        for (int i = 0; i < NUM_LEDS; i++) tiraLed.setPixelColor(i, tiraLed.Color(150, 80, 0));
        tiraLed.show(); vTaskDelay(pdMS_TO_TICKS(150));
        tiraLed.clear(); tiraLed.show(); vTaskDelay(pdMS_TO_TICKS(150));
      }
      ledsBase();
      while (digitalRead(BOTON_CARPETA_PIN) == LOW) vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}


void TaskPiano(void *pv) {
  for (;;) {
    if (movimientoDetectado) {
      movimientoDetectado = false;
      if (!pianoActivo) {
        pianoActivo = true;
        pendienteEnvioInmediato = true;
        Serial.println("Piano DESPIERTO (PIR)");
        animacionWakeUp();
      }
      ultimoMovimiento = millis();
    }

    if (pianoActivo) {
      procesarVolumenLaser();
      revisarCambioCarpeta();

      // Detección de flanco descendente (HIGH→LOW) con antirrebote por botón
      for (int i = 0; i < 8; i++) {
        bool estadoActual = digitalRead(botones[i]);
        if (estadoActual == LOW && estadoAnteriorBoton[i] == HIGH) {
          unsigned long ahora = millis();
          if (ahora - ultimoTiempoBoton[i] > DEBOUNCE_MS) {
            ultimoTiempoBoton[i] = ahora;
            tocarNota(i);
          }
        }
        estadoAnteriorBoton[i] = estadoActual;
      }

      if (millis() - ultimaNota > TIEMPO_SERVO) ledcWrite(SERVO_PIN, SERVO_STOP);
      if (millis() - ultimaNota > 1500 && millis() - ultimaNota < 1550) ledsBase();
      if (millis() - ultimoMovimiento > TIEMPO_INACTIVO) {
        pianoActivo = false;
        pendienteEnvioInmediato = true;
        Serial.println("Piano SUSPENDIDO (inactividad)");
        ledcWrite(SERVO_PIN, SERVO_STOP);
        animacionSleep();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}


void TaskBLE(void *pv) {
  for (;;) {
    if (bleConnected && pTxChar != nullptr) {
      if ((millis() - ultimoEnvioNube > 15000) ||
          (pendienteEnvioInmediato && millis() - ultimoEnvioNube > 2000)) {

        ultimoEnvioNube         = millis();
        pendienteEnvioInmediato = false;

        int notaEnvio = (millis() - ultimaNota < 1800) ? ultimaNotaTocadaNum : 0;

        String payload = "STATUS:ACT=" + String(pianoActivo ? 1 : 0) +
                         "&NOT=" + String(notaEnvio) +
                         "&VOL=" + String(volumenActual) +
                         "&FOL=" + String(carpetaActiva) + "\n";

        pTxChar->setValue(payload.c_str());
        pTxChar->notify();
        Serial.printf("BLE Telemetría TX → %s", payload.c_str());
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}


void setupBLE() {
  BLEDevice::init("SmartPiano");
  pBLEServer = BLEDevice::createServer();
  pBLEServer->setCallbacks(new BLEServerCB());
  BLEService* pService = pBLEServer->createService(BLE_SERVICE_UUID);

  pTxChar = pService->createCharacteristic(BLE_CHAR_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  pTxChar->addDescriptor(new BLE2902());

  BLECharacteristic* pRxChar = pService->createCharacteristic(
    BLE_CHAR_RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pRxChar->setCallbacks(new BLERxCB());

  pService->start();
  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(BLE_SERVICE_UUID);
  pAdv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("BLE NUS activo — SmartPiano visible vía Bluetooth ✓");
}


// Carga el banco de sonidos del Piano (instrumento 1) recortando la cabecera WAV
// y el click final de grabación. Flauta (2) y Guitarra (3) se rellenarán aquí
// mismo cuando existan sus audio_flauta_*.h / audio_guitarra_*.h.
void cargarSonidosPiano() {
  const unsigned char* datosPiano[NUM_TECLAS] = {
    soundPiano1, soundPiano2, soundPiano3, soundPiano4,
    soundPiano5, soundPiano6, soundPiano7, soundPiano8
  };
  const size_t tamanosPiano[NUM_TECLAS] = {
    sizeof(soundPiano1), sizeof(soundPiano2), sizeof(soundPiano3), sizeof(soundPiano4),
    sizeof(soundPiano5), sizeof(soundPiano6), sizeof(soundPiano7), sizeof(soundPiano8)
  };

  for (int i = 0; i < NUM_TECLAS; i++) {
    if (tamanosPiano[i] > WAV_HEADER_SIZE) {
      const unsigned char* datos = datosPiano[i] + WAV_HEADER_SIZE;
      size_t len = tamanosPiano[i] - WAV_HEADER_SIZE;
      if (len > TRIM_END_SAMPLES) len -= TRIM_END_SAMPLES;
      SONIDOS[0][i]     = datos;
      LONGITUDES[0][i]  = len;
    } else {
      SONIDOS[0][i]    = nullptr;
      LONGITUDES[0][i] = 0;
    }
  }
}


void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== SMART PIANO PREMIUM V7 (I2S POLIFÓNICO + BLE MULTI-CORE) ===");

  Adafruit_NeoPixel ledInterno(1, 48, NEO_GRB + NEO_KHZ800);
  ledInterno.begin();
  ledInterno.setPixelColor(0, ledInterno.Color(0,0,0));
  ledInterno.show();
  delay(10);

  Wire.begin(8, 9);
  if (!lox.begin()) Serial.println("ALERTA: VL53L0X no detectado.");

  for (int i = 0; i < 8; i++) pinMode(botones[i], INPUT_PULLUP);
  pinMode(BOTON_CARPETA_PIN, INPUT_PULLUP);
  pinMode(PIR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirISR, RISING);

  tiraLed.begin();
  tiraLed.setBrightness(brilloLed);
  apagarLeds();

  cargarSonidosPiano();
  iniciarI2S();

  ledcAttach(SERVO_PIN, SERVO_FREQ, SERVO_RES);
  ledcWrite(SERVO_PIN, SERVO_STOP);

  // Calibración PIR
  Serial.println("Calibrando sensor de movimiento...");
  tiraLed.clear();
  for (int i = 0; i < NUM_LEDS; i++) {
    tiraLed.setPixelColor(i, tiraLed.Color(150, 80, 0));
    tiraLed.show(); delay(1250);
  }
  for (int j = 0; j < 3; j++) {
    tiraLed.clear(); tiraLed.show(); delay(150);
    for (int i = 0; i < NUM_LEDS; i++) tiraLed.setPixelColor(i, tiraLed.Color(150, 80, 0));
    tiraLed.show(); delay(150);
  }
  ledsBase();
  Serial.println("Sistema Local Listo ✓");

  setupBLE();

  // Asignación de tareas a los núcleos correspondientes
  xTaskCreatePinnedToCore(tareaMezcladora, "TareaMezcladora", 4096, NULL, 2, &tareaMezcladoraHandle, 1); // Núcleo 1 (Audio I2S, prioridad alta)
  xTaskCreatePinnedToCore(TaskPiano, "TaskPiano", 4096, NULL, 1, NULL, 1); // Núcleo 1 (Periféricos/Notas)
  xTaskCreatePinnedToCore(TaskBLE,   "TaskBLE",   4096, NULL, 1, NULL, 0); // Núcleo 0 (Radio BLE)
}

void loop() {
  vTaskDelete(NULL);
}
