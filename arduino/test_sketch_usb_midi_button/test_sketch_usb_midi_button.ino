#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>

// ---------- Nokia 5110 pins ----------
#define PIN_SCLK 10
#define PIN_DIN  11
#define PIN_DC   14
#define PIN_CS   13
#define PIN_RST  15

// ---------- NeoPixel ----------
#define LED_PIN   16
#define LED_COUNT 1

// ---------- Button ----------
#define BUTTON_PIN 12

// ---------- MIDI ----------
#define MIDI_CH   1
#define MIDI_NOTE 60
#define MIDI_VEL  100

Adafruit_PCD8544 display(PIN_SCLK, PIN_DIN, PIN_DC, PIN_CS, PIN_RST);
Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// USB MIDI object
Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

bool lastButtonState = HIGH;

void showStatus(const char* line4, const char* line5) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);

  display.setCursor(0, 0);
  display.println("RP2040 ZERO");

  display.setCursor(0, 10);
  display.println("Nokia 5110");

  display.setCursor(0, 20);
  display.println(line4);

  display.setCursor(0, 30);
  display.println(line5);

  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  led.begin();
  led.setBrightness(40);
  led.clear();
  led.show();

  display.begin();
  display.setContrast(55);

  // TinyUSB init
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }

  usb_midi.setStringDescriptor("RP2040 MIDI Button");
  MIDI.begin(MIDI_CHANNEL_OMNI);

  showStatus("USB MIDI INIT", "WAIT PC ENUM");

  // Give host time to enumerate
  delay(1500);

  if (TinyUSBDevice.mounted()) {
    showStatus("MIDI READY", "BUTTON TEST");
    Serial.println("USB MIDI mounted");
  } else {
    showStatus("NOT MOUNTED", "CHECK USB STACK");
    Serial.println("USB MIDI not mounted");
  }
}

void loop() {
#ifdef TINYUSB_NEED_POLLING_TASK
  TinyUSBDevice.task();
#endif

  bool buttonState = digitalRead(BUTTON_PIN);

  if (buttonState != lastButtonState) {
    delay(20); // simple debounce
    buttonState = digitalRead(BUTTON_PIN);

    if (buttonState != lastButtonState) {
      lastButtonState = buttonState;

      if (buttonState == LOW) {
        led.setPixelColor(0, led.Color(255, 255, 255));
        led.show();

        showStatus("MIDI READY", "NOTE ON C4");
        Serial.println("Button pressed");

        if (TinyUSBDevice.mounted()) {
          MIDI.sendNoteOn(MIDI_NOTE, MIDI_VEL, MIDI_CH);
        }
      } else {
        led.setPixelColor(0, led.Color(0, 0, 0));
        led.show();

        showStatus("MIDI READY", "NOTE OFF C4");
        Serial.println("Button released");

        if (TinyUSBDevice.mounted()) {
          MIDI.sendNoteOff(MIDI_NOTE, 0, MIDI_CH);
        }
      }
    }
  }

  MIDI.read();
}