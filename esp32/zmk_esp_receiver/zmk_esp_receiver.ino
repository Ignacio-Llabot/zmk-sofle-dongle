// Recibe los mensajes que manda el teclado con &esp_msg N y controla una tira LED.
//
// El dongle emite durante ~500 ms anuncios BLE con manufacturer data:
//   FF FF 'Z' 'K' <id> <seq>
// <seq> cambia en cada pulsacion, asi que se usa para no procesar
// el mismo mensaje varias veces.
//
// Teclas (capa top):
//   N (ESP 1) -> prender      M (ESP 2) -> apagar
//   , (ESP 3) -> color anterior   . (ESP 4) -> color siguiente
//
// Librerias: BLE (viene con el core de ESP32) y FastLED (instalar desde el Library Manager).

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <FastLED.h>

#define LED_PIN 18
#define NUM_LEDS 300
#define MAX_BRIGHTNESS 102  // 40% de 255

// Colores para ciclar (hue de FastLED, 0-255)
static const uint8_t COLORS[] = {
    0,    // rojo
    32,   // naranja
    64,   // amarillo
    96,   // verde
    128,  // cian
    160,  // azul
    192,  // violeta
    224,  // rosa
};
#define NUM_COLORS (sizeof(COLORS) / sizeof(COLORS[0]))

static CRGB leds[NUM_LEDS];
static BLEScan *scan;

// Estado que cambia el callback BLE (otra tarea) y lee loop()
static volatile bool ledsOn = false;
static volatile uint8_t colorIndex = 0;

static int lastSeq = -1;
static unsigned long lastSeqTime = 0;

void onMessage(uint8_t id) {
  switch (id) {
    case 1:
      ledsOn = true;
      Serial.println("Mensaje 1: prender");
      break;
    case 2:
      ledsOn = false;
      Serial.println("Mensaje 2: apagar");
      break;
    case 3:
      colorIndex = (colorIndex + NUM_COLORS - 1) % NUM_COLORS;
      Serial.printf("Mensaje 3: color %u\n", colorIndex);
      break;
    case 4:
      colorIndex = (colorIndex + 1) % NUM_COLORS;
      Serial.printf("Mensaje 4: color %u\n", colorIndex);
      break;
    default:
      Serial.printf("Mensaje desconocido: %u\n", id);
      break;
  }
}

class ZmkCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    if (!dev.haveManufacturerData()) {
      return;
    }

    auto data = dev.getManufacturerData();
    if (data.length() < 6 || (uint8_t)data[0] != 0xFF || (uint8_t)data[1] != 0xFF ||
        data[2] != 'Z' || data[3] != 'K') {
      return;
    }

    uint8_t id = data[4];
    uint8_t seq = data[5];

    // Una rafaga dura ~500 ms: mismo seq dentro de 2 s = repeticion del mismo mensaje
    unsigned long now = millis();
    if (seq == lastSeq && now - lastSeqTime < 2000) {
      return;
    }
    lastSeq = seq;
    lastSeqTime = now;

    onMessage(id);
  }
};

// El escaneo es bloqueante, asi que corre en su propia tarea y loop() queda libre para animar
void scanTask(void *) {
  for (;;) {
    scan->start(5, false);
    scan->clearResults();
  }
}

// Ola: una onda de brillo que recorre la tira en el color elegido
void drawWave() {
  uint8_t hue = COLORS[colorIndex];
  uint8_t t = millis() / 8;  // velocidad

  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t wave = sin8(i * 6 - t);           // largo de la ola
    uint8_t v = map8(wave, 20, 255);          // nunca del todo apagado
    leds[i] = CHSV(hue, 255, v);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Esperando mensajes del teclado...");

  FastLED.addLeds<WS2815, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(MAX_BRIGHTNESS);
  FastLED.clear(true);

  BLEDevice::init("");
  scan = BLEDevice::getScan();
  // wantDuplicates = true: si no, solo llega el primer anuncio de esa direccion
  scan->setAdvertisedDeviceCallbacks(new ZmkCallbacks(), true);
  scan->setActiveScan(false);
  scan->setInterval(100);
  scan->setWindow(100);  // window == interval: escucha todo el tiempo

  xTaskCreatePinnedToCore(scanTask, "scan", 4096, NULL, 1, NULL, 0);
}

void loop() {
  static bool wasOn = false;

  if (ledsOn) {
    drawWave();
    FastLED.show();
    wasOn = true;
  } else if (wasOn) {
    FastLED.clear(true);
    wasOn = false;
  }

  delay(16);  // ~60 fps
}
