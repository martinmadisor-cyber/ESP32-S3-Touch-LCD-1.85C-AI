#include <unity.h>
#include <lvgl.h>
#include "GUI.h"

// Define global screen pointers (they are extern in GUI.h)
// We need to define them here for the linker in native mode
lv_obj_t* main_screen = nullptr;
lv_obj_t* config_screen = nullptr;
lv_obj_t* source_screen = nullptr;
lv_obj_t* internet_radio_screen = nullptr;
lv_obj_t* sdcard_mp3_screen = nullptr;
lv_obj_t* alarm_screen = nullptr;
lv_obj_t* alarm_screen_edit = nullptr;
lv_obj_t* assistant_screen = nullptr;
lv_obj_t* recorder_screen = nullptr;
lv_obj_t* chatbot_screen = nullptr;
lv_obj_t* clock_screen = nullptr;
lv_obj_t* wifi_info_screen = nullptr;
lv_obj_t* wifi_discovery_screen = nullptr;
lv_obj_t* wifi_password_screen = nullptr;
lv_obj_t* current_screen = nullptr;

// Other externs from GUI.h/cpp
lv_group_t* global_input_group = nullptr;
volatile bool last_wifi_connected = false;
volatile bool backend_connected = false;
volatile bool g_gui_transitioning = false;
volatile uint32_t g_last_screen_load_ms = 0;

// Dummy Serial and Audio for native testing (since we don't have Arduino here)
class MockSerial {
public:
    void print(const char* s) {}
    void println(const char* s) {}
    void printf(const char* fmt, ...) {}
};
MockSerial Serial;

// Mock display driver for headless testing
static void dummy_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
    lv_display_flush_ready(disp);
}

void setUp(void) {
}

void tearDown(void) {
}

void test_gui_init_headless() {
    lv_init();
    lv_display_t * disp = lv_display_create(240, 320);
    lv_display_set_flush_cb(disp, dummy_flush_cb);
    TEST_ASSERT_NOT_NULL(disp);
}

void test_chatbot_screen_creation() {
    GUI_CreateChatbotScreen();
    // After calling, the global chatbot_screen should be set
    TEST_ASSERT_NOT_NULL(chatbot_screen);
    TEST_ASSERT_TRUE(lv_obj_is_valid(chatbot_screen));
}

void test_main_screen_creation() {
    GUI_CreateMainScreen();
    TEST_ASSERT_NOT_NULL(main_screen);
    TEST_ASSERT_TRUE(lv_obj_is_valid(main_screen));
}

void test_chatbot_elements() {
    GUI_CreateChatbotScreen();
    // Use LVGL tree discovery to find the status label (the first label on this screen typically)
    lv_obj_t* label = lv_obj_get_child(chatbot_screen, 0); // Status label 
    TEST_ASSERT_NOT_NULL(label);
    const char* text = lv_label_get_text(label);
    TEST_ASSERT_EQUAL_STRING("START", text);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_gui_init_headless);
    RUN_TEST(test_chatbot_screen_creation);
    RUN_TEST(test_chatbot_elements);
    RUN_TEST(test_main_screen_creation);
    RUN_TEST(test_source_screen_creation);
    UNITY_END();
    return 0;
}
