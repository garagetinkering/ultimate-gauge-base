#include <Arduino.h>
#include "CANBus_Driver.h"
#include "LVGL_Driver.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>

#include "UltimateGauge.h"

// Intialise memory
Preferences prefs;

// incoming ESPNow data (to be replaced with CAN)
typedef struct struct_data {
  uint8_t flag;
} struct_data;

// Generic control variables
bool initial_load               = false;  // has the first data been received
bool fade_in_complete           = false;  // brightness fade finished
bool button_pressed             = false;  // track button presses
volatile bool data_ready        = false;  // new espnow data (resets)

// Struct Objects
struct_data IncomingData;

// Screens
lv_obj_t *overlay_scr;  // special case - always on top
lv_obj_t *main_scr;

// Global components
lv_obj_t *spinner;


void WiFi_init() {
  WiFi.mode(WIFI_STA);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
}

void Drivers_Init() {
  I2C_Init();
  LCD_Init();
  WiFi_init();
  CANBus_Init();
  Lvgl_Init();
}

// ESPNow received
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {

  int8_t new_channel = 0;

  // Write to the correct structure based on ESPNow flag
  switch (incomingData[0]) {
    case (FLAG_SET_CHANNEL):
      new_channel = incomingData[1];
      esp_wifi_set_channel(new_channel, WIFI_SECOND_CHAN_NONE);
      break;
    case (FLAG_CANBUS):
      memcpy(&IncomingData, incomingData, sizeof(IncomingData)); // store incoming data
      data_ready = true;
      break;
  }
}

void Style_Init(void) {
  // set global styles
}

// load from 0 to saved brightness at startup
void Fade_In_Screen(void *pvParameter) {
  int delay_step = (dimmer_lv == 0) ? BACKLIGHT_INTRO_TIME : BACKLIGHT_INTRO_TIME / (dimmer_lv * 10);

  for (int i = 1; i <= (dimmer_lv * 10); i++) {
    Set_Backlight(i);
    vTaskDelay(pdMS_TO_TICKS(delay_step));
  }
  fade_in_complete = true;
  vTaskDelete(NULL);
}

// start off and create task to fade in
void Brightness_Init() {
  Set_Backlight(0);

  // use Core 1 so to not block
  xTaskCreatePinnedToCore(Fade_In_Screen, "FadeInScreen", 2048, NULL, 1, NULL, 1);
}

// update with incoming values from ESPNow
void Update_Values() {
}

// create the elements on the scr
void Main_Scr_Init() {
  spinner = lv_spinner_create(main_scr);
  lv_obj_set_size(spinner, 240, 240);
  lv_obj_center(spinner);
  lv_spinner_set_anim_params(spinner, 1000, 200);
}

void Screen_Init() {
  overlay_scr = lv_layer_top(); // special screen - always on top

  main_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(main_scr, PALETTE_BLACK, 0);

  lv_screen_load(main_scr);
}

void Values_Init() {
  // initialise goal and current the same
  dimmer_lv = prefs.getInt("dimmer_lv", 10);
  current_brightness = dimmer_lv;
}

void Make_Initial_UI() {
  Screen_Init();
  Main_Scr_Init();
}

void setup() { 
  Serial.begin(115200);
  Serial.println("begin");

  // initialise data memory
  prefs.begin("status_store", false);
  
  Drivers_Init();
  Style_Init();

  Values_Init();

  Make_Initial_UI();
  Brightness_Init();
}

void loop() {
  lv_timer_handler();

  CAN_FRAME can_message;

  if (CAN0.read(can_message)) {
    digitalWrite(ACC_LED_PIN, HIGH);
    Serial.print("CAN MSG: 0x");
    Serial.print(can_message.id, HEX);
    Serial.print(" [");
    Serial.print(can_message.length, DEC);
    Serial.print("] <");
    for (int i = 0; i < can_message.length; i++)
    {
      if (i != 0) Serial.print(":");
      Serial.print(can_message.data.byte[i], HEX);
    }
    Serial.println(">");
  }

  if (data_ready) {
    Serial.println("data ready");
    Update_Values();
  }

  vTaskDelay(pdMS_TO_TICKS(5));
}