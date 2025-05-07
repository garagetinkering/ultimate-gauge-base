/* Basic Driver setup for the ESP32-S3 2.1 inch LCD Driver board  */
/* Author: Andy Valentine - Garage Tinkering                      */
/*                                                                */
/* Requires library dependancy available at                       */
/* - https://github.com/garagetinkering/ultimate-gauge-global     */

#include <Arduino.h>
#include "CANBus_Driver.h"
#include "LVGL_Driver.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>

// Library dependancy
#include "UltimateGauge.h"

// Intialise memory
Preferences prefs;

// Incoming ESPNow data
typedef struct struct_data {
  uint8_t flag;
  // ...
  // add as neeeded
} struct_data;

struct_buttons ButtonData;

// Generic control variables
bool initial_load         = false; // has the first data been received
bool fade_in_complete     = false; // brightness fade finished
volatile bool data_ready  = false; // new espnow data (resets)
bool button_pressed       = false; // track button presses

// Struct Objects
struct_data ESPNowData;

// Screens
lv_obj_t *overlay_scr;  // special case - always on top, good for alerts
lv_obj_t *main_scr;

// Global components
lv_obj_t *spinner;  // simple demo


void WiFi_init(void) {
  WiFi.mode(WIFI_STA);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
}

void Drivers_Init(void) {
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
  // Flags in UltimateGauge dependancy
  switch (incomingData[0]) {
    case (FLAG_SET_CHANNEL):
      new_channel = incomingData[1];
      esp_wifi_set_channel(new_channel, WIFI_SECOND_CHAN_NONE);
      break;
    case (FLAG_BUTTONS):
      memcpy(&ButtonData, incomingData, sizeof(ButtonData));  // copy incoming button data to ESPNowData
      button_pressed = true;
      break;
    case (FLAG_CANBUS):
      memcpy(&ESPNowData, incomingData, sizeof(ESPNowData));  // copy incoming data to ESPNowData
      data_ready = true;
      break;
  }
}

void Style_Init(void) {
  // set global styles
}

// I use this function to test various things.
// It's a series of buttons connected to an ESP32 and I send the button press over ESPNow
// https://github.com/garagetinkering/350z-button-controller/blob/main/Gauges_Remote.ino
void Handle_Button_Press(void) {

  // 350z - Top left button
  if (ButtonData.button == BUTTON_SETTING) {
    switch (ButtonData.press_type) {
      case CLICK_EVENT_CLICK:
        // Simulate lights off
        Toggle_Brightness(true);
        break;
      case CLICK_EVENT_HOLD:
        // TBC
        break;
    }
  }

  // 350z - Bottom left button
  if (ButtonData.button == BUTTON_MODE) {
    switch (ButtonData.press_type) {
      case CLICK_EVENT_CLICK:
        // Simulate lights on
        Toggle_Brightness(false);
        break;
      case CLICK_EVENT_HOLD:
        // TBC
        break;
    }
  }

  // 350z - Top right button
  if (ButtonData.button == BUTTON_BRIGHTNESS_UP) {
    switch (ButtonData.press_type) {
      case CLICK_EVENT_CLICK:
        // Simulate current mode brightness up
        Update_New_Brightness(true);
        break;
    }
  }

  // 350z - Bottom right button
  if (ButtonData.button == BUTTON_BRIGHTNESS_DOWN) {
    switch (ButtonData.press_type) {
      case CLICK_EVENT_CLICK:
        // Simulate current mode brightness dowm
        Update_New_Brightness(false);
        break;
    }
  }
}

// load from 0 to correct saved brightness at startup
void Start_Screen_Fade_In() {
  int target = Brightness.is_dimmed ? Brightness.nighttime : Brightness.daytime;
  Start_Brightness_Fade(0, target,
                        Brightness.is_dimmed ? &Brightness.nighttime : &Brightness.daytime,
                        Brightness.is_dimmed ? "brightness_n" : "brightness_d");
}

// avoiding blocking LVGL
void Brightness_Fade_Timer_Callback(lv_timer_t* timer) {
  if (fade_steps_done >= fade_steps_total) {
    // Done fading
    lv_timer_del(brightness_fade_timer);
    brightness_fade_timer = nullptr;

    fade_current = fade_end;
    Set_Backlight(fade_current);

    *fade_brightness_ptr = fade_end;
    prefs.putInt(fade_key, fade_end);

    Serial.print("Brightness saved as ");
    Serial.println(fade_end);
    return;
  }

  fade_current += fade_step;
  Set_Backlight(fade_current);
  fade_steps_done++;
}

// Fade step calculation
void Start_Brightness_Fade(int from, int to, int* store_ptr, const char* key) {
  if (from == to) {
    Set_Backlight(to);
    if (store_ptr) *store_ptr = to;
    if (key) prefs.putInt(key, to);
    return;
  }

  fade_start = from;
  fade_end = to;
  fade_brightness_ptr = store_ptr;
  fade_key = key;

  fade_steps_total = 10;
  fade_current = fade_start;
  fade_step = (fade_end - fade_start) / fade_steps_total;
  fade_steps_done = 0;

  if (brightness_fade_timer) {
    lv_timer_del(brightness_fade_timer);
  }

  brightness_fade_timer = lv_timer_create(Brightness_Fade_Timer_Callback, 50, NULL);
}

// switch daytime to nighttime mode
void Toggle_Brightness(bool set_nighttime) {
   int from = Brightness.is_dimmed ? Brightness.nighttime : Brightness.daytime;
  int to = set_nighttime ? Brightness.nighttime : Brightness.daytime;
  Brightness.is_dimmed = set_nighttime;

  Start_Brightness_Fade(from, to,
                        set_nighttime ? &Brightness.nighttime : &Brightness.daytime,
                        set_nighttime ? "brightness_n" : "brightness_d");
}

// to change brightness, simple pass true to increase 1 step, or false to decrease
// functions will figure out the rest
void Update_New_Brightness(bool increase_brightness) {
  int* ptr = Brightness.is_dimmed ? &Brightness.nighttime : &Brightness.daytime;
  const char* key = Brightness.is_dimmed ? "brightness_n" : "brightness_d";

  int current = *ptr;
  const int STEP_SIZE = 10;
  int target = current;

  if (increase_brightness && current <= BRIGHTNESS_LEVEL_MAX - STEP_SIZE) {
    target += STEP_SIZE;
  } else if (!increase_brightness && current >= BRIGHTNESS_LEVEL_MIN + STEP_SIZE) {
    target -= STEP_SIZE;
  } else {
    Serial.println("Brightness adjustment out of range.");
    return;
  }

  Start_Brightness_Fade(current, target, ptr, key);
}

// start with backlight off and create task to fade in
void Brightness_Init(void) {
  Set_Backlight(0);

  // use Core 1 so to not block
  Start_Screen_Fade_In();
}

// update with incoming values from ESPNow
void Update_Values(void) {
  // uses ESPNowData.your_options
}

// create the elements on the main scr
void Main_Scr_UI(void) {
  spinner = lv_spinner_create(main_scr);
  lv_obj_set_size(spinner, 240, 240);
  lv_obj_center(spinner);
  lv_spinner_set_anim_params(spinner, 1000, 200);
}

void Screen_Init(void) {
  overlay_scr = lv_layer_top();

  main_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(main_scr, PALETTE_BLACK, 0);

  lv_screen_load(main_scr);
}

void Values_Init(void) {
  Brightness.daytime   = prefs.getInt("brightness_d", BRIGHTNESS_DAYTIME_DEFAULT);    // screen brightness in day mode
  Brightness.nighttime = prefs.getInt("brightness_n", BRIGHTNESS_NIGHTTIME_DEFAULT);  // screen brightness in night mode
  Brightness.is_dimmed = false;                                                       // assume lights start off
}

void Make_Initial_UI(void) {
  Screen_Init();
  Main_Scr_UI();
}

unsigned long brightness_init_time = 0;
bool brightness_updated = false;

void setup(void) {
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

void loop(void) {
  lv_timer_handler();

  CAN_FRAME can_message;

  if (CAN0.read(can_message)) {
    digitalWrite(ACC_LED_PIN, HIGH);
    Serial.print("CAN MSG: 0x");
    Serial.print(can_message.id, HEX);
    Serial.print(" [");
    Serial.print(can_message.length, DEC);
    Serial.print("] <");
    for (int i = 0; i < can_message.length; i++) {
      if (i != 0) Serial.print(":");
      Serial.print(can_message.data.byte[i], HEX);
    }
    Serial.println(">");
  }

  // data acquired from ESPNow
  if (data_ready) {
    Serial.println("ESPNow data ready");
    Update_Values();
    data_ready = false;
  }

  if (button_pressed) {
    Handle_Button_Press();
    button_pressed = false;
  }

  vTaskDelay(pdMS_TO_TICKS(5));
}