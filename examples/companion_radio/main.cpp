#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include "MyMesh.h"

#if defined(SHIPPING_MODE_ENABLED) && defined(ESP32)
  #include <esp_sleep.h>
#endif

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
    DataStore store(InternalFS, QSPIFlash, rtc_clock);
  #else
  #if defined(EXTRAFS)
    #include <CustomLFS.h>
    CustomLFS ExtraFS(0xD4000, 0x19000, 128);
    DataStore store(InternalFS, ExtraFS, rtc_clock);
  #else
    DataStore store(InternalFS, rtc_clock);
  #endif
  #endif
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  DataStore store(LittleFS, rtc_clock);
#elif defined(ESP32)
  #include <SPIFFS.h>
  DataStore store(SPIFFS, rtc_clock);
#endif

#ifdef ESP32
  #ifdef WIFI_SSID
    #include <helpers/esp32/SerialWifiInterface.h>
    SerialWifiInterface serial_interface;
    #ifndef TCP_PORT
      #define TCP_PORT 5000
    #endif
  #elif defined(BLE_PIN_CODE)
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #elif defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(RP2040_PLATFORM)
  //#ifdef WIFI_SSID
  //  #include <helpers/rp2040/SerialWifiInterface.h>
  //  SerialWifiInterface serial_interface;
  //  #ifndef TCP_PORT
  //    #define TCP_PORT 5000
  //  #endif
  // #elif defined(BLE_PIN_CODE)
  //   #include <helpers/rp2040/SerialBLEInterface.h>
  //   SerialBLEInterface serial_interface;
  #if defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(NRF52_PLATFORM)
  #ifdef BLE_PIN_CODE
    #include <helpers/nrf52/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(STM32_PLATFORM)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface serial_interface;
#else
  #error "need to define a serial interface"
#endif

/* GLOBAL OBJECTS */
#ifdef DISPLAY_CLASS
  #include "UITask.h"
  UITask ui_task(&board, &serial_interface);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
   #ifdef DISPLAY_CLASS
      , &ui_task
   #endif
);

/* END GLOBAL OBJECTS */

#ifdef SHIPPING_MODE_ENABLED

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #define SHIP_FS InternalFS
#elif defined(RP2040_PLATFORM)
  #define SHIP_FS LittleFS
#elif defined(ESP32)
  #define SHIP_FS SPIFFS
#endif

// SHIPPING_BUILD_STAMP is set to $UNIX_TIME by platformio.ini, ensuring a
// unique value per build even for incremental builds (forces recompilation).
#ifdef SHIPPING_BUILD_STAMP
  #define _SHIP_STR(x) #x
  #define SHIP_STR(x) _SHIP_STR(x)
  static const char SHIPPING_BUILD_ID[] = SHIP_STR(SHIPPING_BUILD_STAMP);
#else
  static const char SHIPPING_BUILD_ID[] = __DATE__ " " __TIME__;
#endif

// If firmware changed since last boot, delete the unlock file so shipping mode re-activates.
static void shippingCheckNewFirmware() {
  char buf[sizeof(SHIPPING_BUILD_ID)] = {0};
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  File f = SHIP_FS.open("/shipping_build_id", FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  File f = SHIP_FS.open("/shipping_build_id", "r");
#else
  File f = SHIP_FS.open("/shipping_build_id", "r", false);
#endif
  if (f) {
    f.read((uint8_t*)buf, sizeof(buf) - 1);
    f.close();
    if (strcmp(buf, SHIPPING_BUILD_ID) == 0) return;  // same firmware
  }
  // New firmware (or first boot) — remove unlock file and write new build ID
  SHIP_FS.remove("/shipping_unlocked");
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  SHIP_FS.remove("/shipping_build_id");
  File fw = SHIP_FS.open("/shipping_build_id", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File fw = SHIP_FS.open("/shipping_build_id", "w");
#else
  File fw = SHIP_FS.open("/shipping_build_id", "w", true);
#endif
  if (fw) { fw.write((const uint8_t*)SHIPPING_BUILD_ID, strlen(SHIPPING_BUILD_ID)); fw.close(); }
}

static bool shippingUnlockFileExists() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  File f = SHIP_FS.open("/shipping_unlocked", FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  File f = SHIP_FS.open("/shipping_unlocked", "r");
#else
  File f = SHIP_FS.open("/shipping_unlocked", "r", false);
#endif
  if (f) { f.close(); return true; }
  return false;
}

static void shippingWriteUnlockFile() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  SHIP_FS.remove("/shipping_unlocked");
  File f = SHIP_FS.open("/shipping_unlocked", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File f = SHIP_FS.open("/shipping_unlocked", "w");
#else
  File f = SHIP_FS.open("/shipping_unlocked", "w", true);
#endif
  if (f) { f.write((uint8_t)1); f.close(); }
}

static void shippingModeSleep() {
  // E-ink retains its image without power — just cut display controller power
#ifdef DISPLAY_CLASS
  display.turnOff();
#endif

#if defined(ESP32) && defined(PIN_USER_BTN)
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_USER_BTN, 0);
  esp_deep_sleep_start();
#elif defined(NRF52_PLATFORM) && defined(PIN_USER_BTN)
  // Configure GPIO sense so button press wakes from SYSTEMOFF.
  // SYSTEMOFF draws ~0.3 µA. Wake triggers a full reboot.
  // PIN_USER_BTN (GPIO 42) is NOT the RESET button, so the Adafruit
  // bootloader will not enter DFU — it only checks for double-tap RESET.
  nrf_gpio_cfg_sense_input(PIN_USER_BTN,
    NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
  board.powerOff();  // turns off LEDs, backlight, PWR_EN, then sd_power_system_off()
#else
  board.powerOff();
#endif

  while(1) { delay(1000); }  // should never reach here
}

static void checkShippingMode() {
  shippingCheckNewFirmware();
  if (shippingUnlockFileExists()) return;

#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);

  // Draw unlock prompt — on e-ink this persists across SYSTEMOFF sleeps
#ifdef DISPLAY_CLASS
  display.startFrame();
  display.setTextSize(2);
  display.drawTextCentered(display.width()/2, 10, "Installeer eerst");
  display.drawTextCentered(display.width()/2, 25, "de Antenne !");
  display.setTextSize(1);
  display.drawTextCentered(display.width()/2, 62, "Daarna de onderste");
  display.drawTextCentered(display.width()/2, 74, "knop 3s inhouden om");
  display.drawTextCentered(display.width()/2, 86, "te ontgrendelen");
  display.setTextSize(2);
  display.drawTextCentered(10, 110, "<<");
  display.endFrame();
#endif

  // Wait up to 5s for a 3-second continuous hold
  unsigned long start = millis();
  unsigned long held_since = 0;
  const unsigned long UNLOCK_HOLD_MS = 3000;
  const unsigned long WAIT_TIMEOUT_MS = 30000;

  while (millis() - start < WAIT_TIMEOUT_MS) {
    bool pressed = (digitalRead(PIN_USER_BTN) == LOW);

    if (pressed) {
      if (held_since == 0) held_since = millis();

      if (millis() - held_since >= UNLOCK_HOLD_MS) {
        shippingWriteUnlockFile();

        #ifdef DISPLAY_CLASS
          display.startFrame();
          display.setTextSize(2);
          display.drawTextCentered(display.width()/2, 28, "Ontgrendeld!");
          display.endFrame();
          delay(1000);
        #endif

        board.reboot();
        while(1);
      }
    } else {
      held_since = 0;
    }

    delay(50);
  }

  // Timeout — show shutoff message on e-ink before sleeping
  #ifdef DISPLAY_CLASS
    display.startFrame();
    display.setTextSize(1);
    display.drawTextCentered(display.width()/2, 10, "<< Druk op deze knop");
    display.drawTextCentered(display.width()/2, 22, "om te starten");
    display.setTextSize(2);
    display.drawTextCentered(display.width()/2, 50, "Slaapstand...");
    display.endFrame();
  #endif
#endif

  // Button not held long enough (or no button) — deep sleep.
  // On NRF52: SYSTEMOFF (~0.3 µA), e-ink retains message. Button wakes as full reboot.
  // On ESP32: deep sleep with GPIO wake.
  shippingModeSleep();
}

#if !defined(PIN_USER_BTN)
  #warning "SHIPPING_MODE_ENABLED but PIN_USER_BTN not defined - device cannot be unlocked via button"
#endif

#endif // SHIPPING_MODE_ENABLED

void halt() {
  while (1) ;
}

void setup() {
  Serial.begin(115200);

  board.begin();

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  if (display.begin()) {
    disp = &display;
    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
    disp->endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_get_rng_seed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  #if defined(QSPIFLASH)
    if (!QSPIFlash.begin()) {
      // debug output might not be available at this point, might be too early. maybe should fall back to InternalFS here?
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: failed to initialize");
    } else {
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: initialized successfully");
    }
  #else
  #if defined(EXTRAFS)
      ExtraFS.begin();
  #endif
  #endif
#ifdef SHIPPING_MODE_ENABLED
  checkShippingMode();
#endif
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#ifdef BLE_PIN_CODE
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
#else
  serial_interface.begin(Serial);
#endif
  the_mesh.startInterface(serial_interface);
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
#ifdef SHIPPING_MODE_ENABLED
  checkShippingMode();
#endif
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

  //#ifdef WIFI_SSID
  //  WiFi.begin(WIFI_SSID, WIFI_PWD);
  //  serial_interface.begin(TCP_PORT);
  // #elif defined(BLE_PIN_CODE)
  //   char dev_name[32+16];
  //   sprintf(dev_name, "%s%s", BLE_NAME_PREFIX, the_mesh.getNodeName());
  //   serial_interface.begin(dev_name, the_mesh.getBLEPin());
  #if defined(SERIAL_RX)
    companion_serial.setPins(SERIAL_RX, SERIAL_TX);
    companion_serial.begin(115200);
    serial_interface.begin(companion_serial);
  #else
    serial_interface.begin(Serial);
  #endif
    the_mesh.startInterface(serial_interface);
#elif defined(ESP32)
  SPIFFS.begin(true);
#ifdef SHIPPING_MODE_ENABLED
  checkShippingMode();
#endif
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#ifdef WIFI_SSID
  board.setInhibitSleep(true);   // prevent sleep when WiFi is active
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  serial_interface.begin(TCP_PORT);
#elif defined(BLE_PIN_CODE)
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
#elif defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  serial_interface.begin(companion_serial);
#else
  serial_interface.begin(Serial);
#endif
  the_mesh.startInterface(serial_interface);
#else
  #error "need to define filesystem"
#endif

  sensors.begin();

#if ENV_INCLUDE_GPS == 1
  the_mesh.applyGpsPrefs();
#endif

#ifdef DISPLAY_CLASS
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());  // still want to pass this in as dependency, as prefs might be moved
#endif
}

void loop() {
  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
}
