/*
  Prueba de sonido con 8 botones - ESP32-S3-N16R8 + UDA1334A + PAM8403
  (Arduino core 3.x / ESP-IDF 5.x - usa la API nueva "i2s_std")
*/

#include "driver/i2s_std.h"
#include "audio_data1.h"
#include "audio_data2.h"
#include "audio_data3.h"
#include "audio_data4.h"
#include "audio_data5.h"
#include "audio_data6.h"
#include "audio_data7.h"
#include "audio_data8.h"

#define I2S_BCLK_PIN    4
#define I2S_LRC_PIN     5
#define I2S_DOUT_PIN    6

#define SAMPLE_RATE     22050
#define WAV_HEADER_SIZE 44
#define CHUNK_SIZE      256
#define NUM_SONIDOS     8
#define FADE_SAMPLES    100   // ~4.5 ms a 22050 Hz: desvanecimiento final suave
#define TRIM_END_SAMPLES 260  // recorta el "click" de fin de grabacion presente en los .wav originales

const uint32_t DEBOUNCE_MS = 200;

struct SoundState {
  const unsigned char* data;
  size_t length;
  volatile size_t position;
  volatile bool playing;
  volatile bool disparar;
  volatile uint32_t ultimoTiempo;
  int pin;
};

SoundState sonidos[NUM_SONIDOS];
const int PINES_BOTONES[NUM_SONIDOS] = { 7, 8, 9, 10, 11, 12, 13, 14 };
TaskHandle_t tareaMezcladoraHandle = NULL;
i2s_chan_handle_t tx_handle;

// ISR Segura usando el tiempo de FreeRTOS en lugar de millis()
static void IRAM_ATTR onBotonPresionado(void *arg) {
  int idx = (int)(intptr_t)arg;
  uint32_t ahora = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
  
  if (ahora - sonidos[idx].ultimoTiempo > DEBOUNCE_MS) {
    sonidos[idx].ultimoTiempo = ahora;
    sonidos[idx].disparar = true;
  }
}

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

static void tareaMezcladora(void *pvParameters) {
  (void)pvParameters;
  int16_t buffer[CHUNK_SIZE * 2];
  size_t bytesWritten;

  for (;;) {
    // 1. Revisar disparos
    for (int s = 0; s < NUM_SONIDOS; s++) {
      if (sonidos[s].disparar) {
        sonidos[s].disparar = false;
        sonidos[s].position = 0;
        sonidos[s].playing = true;
        Serial.printf("Boton %d -> reproduciendo\n", s + 1);
      }
    }

    // 2. Mezclar muestras
    bool hayAudioActivo = false;

    for (size_t n = 0; n < CHUNK_SIZE; n++) {
      int32_t mezcla = 0;
      int activos = 0;

      for (int s = 0; s < NUM_SONIDOS; s++) {
        if (sonidos[s].playing) {
          if (sonidos[s].position < sonidos[s].length) {
            // Conversión PCM 8-bit unsigned a 16-bit signed
            int32_t muestra = ((int16_t)sonidos[s].data[sonidos[s].position] - 128) << 8;

            // Fade-out suave en las ultimas muestras para evitar el "click/ts"
            // que se produce al cortar de golpe una senal que no termina en cero.
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

      // Normalización para prevenir clipping al presionar varios botones a la vez
      if (activos > 1) mezcla = mezcla / activos;
      if (mezcla > 32767) mezcla = 32767;
      if (mezcla < -32768) mezcla = -32768;

      int16_t muestra = (int16_t)mezcla;
      buffer[2 * n]     = muestra; // Izquierdo
      buffer[2 * n + 1] = muestra; // Derecho
    }

    // Transmitir al DMA de I2S
    i2s_channel_write(tx_handle, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);

    // Dar respiro al IDLE task si no se está procesando nada pesado
    if (!hayAudioActivo) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Iniciando prueba de sonido polifónica (ESP32-S3)...");

  const unsigned char* datos[NUM_SONIDOS] = {
    soundData1, soundData2, soundData3, soundData4,
    soundData5, soundData6, soundData7, soundData8
  };
  const size_t tamanos[NUM_SONIDOS] = {
    sizeof(soundData1), sizeof(soundData2), sizeof(soundData3), sizeof(soundData4),
    sizeof(soundData5), sizeof(soundData6), sizeof(soundData7), sizeof(soundData8)
  };

  for (int s = 0; s < NUM_SONIDOS; s++) {
    // Validación segura de tamaño
    if (tamanos[s] > WAV_HEADER_SIZE) {
      sonidos[s].data = datos[s] + WAV_HEADER_SIZE;
      sonidos[s].length = tamanos[s] - WAV_HEADER_SIZE;

      // Recorta el click de "fin de grabacion" presente al final del .wav original
      if (sonidos[s].length > TRIM_END_SAMPLES) {
        sonidos[s].length -= TRIM_END_SAMPLES;
      }
    } else {
      sonidos[s].data = NULL;
      sonidos[s].length = 0;
    }
    
    sonidos[s].position = 0;
    sonidos[s].playing = false;
    sonidos[s].disparar = false;
    sonidos[s].ultimoTiempo = 0;
    sonidos[s].pin = PINES_BOTONES[s];
    pinMode(sonidos[s].pin, INPUT_PULLUP);
  }

  iniciarI2S();

  // Asignar a Core 1 con prioridad 2
  xTaskCreatePinnedToCore(tareaMezcladora, "TareaMezcladora", 4096, NULL, 2, &tareaMezcladoraHandle, 1);

  for (int s = 0; s < NUM_SONIDOS; s++) {
    attachInterruptArg(digitalPinToInterrupt(sonidos[s].pin), onBotonPresionado, (void *)(intptr_t)s, FALLING);
  }

  Serial.println("Sistema listo. Presiona los botones para reproducir los sonidos.");

  // --- PRUEBA AUTOMATICA ---
  // Dispara el sonido 1 sin necesidad de presionar ningun boton.
  // Si escuchas esto, el I2S/DAC/amplificador estan bien cableados y el
  // problema esta en los botones/interrupciones.
  // Si NO escuchas nada aqui, el problema esta en el cableado I2S->DAC
  // o en el DAC->amplificador (revisa XSMT/SCK/FMT del modulo DAC).
  delay(1000);
  Serial.println(">>> Reproduciendo sonido de prueba automatico (boton 1) <<<");
  sonidos[0].disparar = true;
}

void loop() {
  vTaskDelete(NULL); // Liberar el loop principal para no gastar recursos
}