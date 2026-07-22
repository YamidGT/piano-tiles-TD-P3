#include <Adafruit_NeoPixel.h>
#include <DFRobotDFPlayerMini.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>


#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

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


HardwareSerial mp3Serial(2);
DFRobotDFPlayerMini mp3;
#define MP3_RX 16
#define MP3_TX 17


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
        mp3.volume(volumenActual);
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


void tocarNota(int nota) {
  mp3.playFolder(carpetaActiva, nota + 1);
  ultimaNota          = millis();
  ultimoMovimiento    = millis();
  ultimaNotaTocadaNum = nota + 1;
  ledcWrite(SERVO_PIN, SERVO_MOVE);
  tiraLed.setPixelColor(nota, tiraLed.Color(0, 0, 255));
  tiraLed.show();
  pinMode(48, INPUT_PULLUP);
  enviarNotaBLE(nota + 1);
  Serial.printf("Nota %d -> Carpeta %02d (Vol: %d)\n", ultimaNotaTocadaNum, carpetaActiva, volumenActual);
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
        mp3.volume(volumenActual); 
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

      // Detección de flanco descendente (HIGH→LOW)
      for (int i = 0; i < 8; i++) {
        bool estadoActual = digitalRead(botones[i]);
        if (estadoActual == LOW && estadoAnteriorBoton[i] == HIGH) {
          tocarNota(i);
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


void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== SMART PIANO PREMIUM V6 (ONLY-BLE MULTI-CORE) ===");

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

  mp3Serial.begin(9600, SERIAL_8N1, MP3_RX, MP3_TX);
  if (!mp3.begin(mp3Serial)) Serial.println("ALERTA: DFPlayer no encontrado.");
  else mp3.volume(volumenActual);

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
  xTaskCreatePinnedToCore(TaskPiano, "TaskPiano", 4096, NULL, 1, NULL, 1); // Núcleo 1 (Periféricos/Notas)
  xTaskCreatePinnedToCore(TaskBLE,   "TaskBLE",   4096, NULL, 1, NULL, 0); // Núcleo 0 (Radio BLE)
}

void loop() { 
  vTaskDelete(NULL); 
}