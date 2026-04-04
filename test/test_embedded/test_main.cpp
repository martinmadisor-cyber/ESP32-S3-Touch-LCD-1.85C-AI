#include <Arduino.h>
#include <unity.h>
#include <WiFi.h>

#include "lv_conf.h"
#include "lvgl.h"
#include "LVGL_ST77916.h"
#include "Touch_CST816.h"
#include "GUI/GUI.h"
#include "Audio.h"

// Instantiate the actual audio object
Audio test_audio;

void setUp(void) {
  // Reset pointers for freshness on every test run if needed
}

void tearDown(void) {
  // clean stuff up here
}

void test_wifi_mac(void) {
  // Verify basic hardware config
  String mac = WiFi.macAddress();
  TEST_ASSERT_NOT_EQUAL(0, mac.length());
}

void test_gui_initialization(void) {
  // Test if LVGL and display driver initialize correctly
  Lvgl_Init();
  TEST_ASSERT_NOT_NULL(disp); // 'disp' is declared in LVGL_ST77916.h
  
  // Test if our GUI routing logic initializes properly
  GUI_Init(test_audio);
  TEST_ASSERT_EQUAL(true, true); // If we reach here without panic, init succeeded
}

void test_gui_main_screen_render(void) {
  GUI_CreateMainScreen();
  TEST_ASSERT_NOT_NULL(main_screen);
  TEST_ASSERT_TRUE(lv_obj_is_valid(main_screen));
}

void test_gui_chatbot_screen_render(void) {
  GUI_CreateChatbotScreen();
  TEST_ASSERT_NOT_NULL(chatbot_screen);
  TEST_ASSERT_TRUE(lv_obj_is_valid(chatbot_screen));
  
  // Ensure the AI label is created
  lv_obj_t* label = lv_obj_get_child(chatbot_screen, 0); 
  TEST_ASSERT_NOT_NULL(label);
  const char* text = lv_label_get_text(label);
  // It usually renders " START", verify it's there
  TEST_ASSERT_NOT_NULL(strstr(text, "START"));
}

void test_gui_alarm_screen_render(void) {
  GUI_CreateAlarmListScreen();
  TEST_ASSERT_NOT_NULL(alarm_screen);
  TEST_ASSERT_TRUE(lv_obj_is_valid(alarm_screen));
}

void test_gui_source_screen_render(void) {
  GUI_CreateSourceScreen();
  TEST_ASSERT_NOT_NULL(source_screen);
  TEST_ASSERT_TRUE(lv_obj_is_valid(source_screen));
}

void setup() {
  // Wait ~2 seconds before the Unity test runner starts
  delay(2000);

  UNITY_BEGIN();

  RUN_TEST(test_wifi_mac);
  RUN_TEST(test_gui_initialization);
  RUN_TEST(test_gui_main_screen_render);
  RUN_TEST(test_gui_chatbot_screen_render);
  RUN_TEST(test_gui_alarm_screen_render);
  RUN_TEST(test_gui_source_screen_render);

  UNITY_END();
}

void loop() {
  // Feed LVGL ticks and tasks just in case it's doing background refr
  lv_timer_handler();
  delay(5);
}
