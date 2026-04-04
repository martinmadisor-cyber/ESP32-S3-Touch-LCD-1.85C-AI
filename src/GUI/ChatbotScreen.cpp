#include "GUI.h"
#include "ChatbotScreen.h"
#include "Chatbot.h"
#include "MIC_MSM.h"

static lv_obj_t* status_label = nullptr;

void GUI_CreateChatbotScreen() {
    if (chatbot_screen) return;
    Serial.println("Creating GUI_CreateChatbotScreen");

    chatbot_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(chatbot_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(chatbot_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(chatbot_screen, lv_color_hex(0x1a1a2e), 0);

    // --- Title ---
    lv_obj_t* title = lv_label_create(chatbot_screen);
    lv_label_set_text(title, "Voice Chatbot");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // --- Status label ---
    status_label = lv_label_create(chatbot_screen);
    lv_label_set_text(status_label, "Tap START to begin");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_18, 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, -80);

    // --- Big Start/Stop button ---
    lv_obj_t* action_btn = lv_button_create(chatbot_screen);
    lv_obj_set_size(action_btn, 200, 200);
    lv_obj_align(action_btn, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_radius(action_btn, 100, 0);  // Circle
    lv_obj_set_style_bg_color(action_btn, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_set_style_bg_opa(action_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(action_btn, 20, 0);
    lv_obj_set_style_shadow_color(action_btn, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_set_style_shadow_opa(action_btn, LV_OPA_50, 0);

    lv_obj_t* btn_label = lv_label_create(action_btn);
    lv_label_set_text(btn_label, LV_SYMBOL_PLAY " START");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_28, 0);
    lv_obj_center(btn_label);

    auto update_ui_cb = [](lv_timer_t* t) {
        lv_obj_t* btn = (lv_obj_t*)lv_timer_get_user_data(t);
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        bool running = Chatbot_IsActive();

        if (running) {
            lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_RED), 0);
            lv_obj_set_style_shadow_color(btn, lv_palette_main(LV_PALETTE_RED), 0);
            lv_label_set_text(lbl, LV_SYMBOL_STOP " STOP");
            if (status_label) {
                lv_label_set_text(status_label, "Listening...");
                lv_obj_set_style_text_color(status_label, lv_color_hex(0xAAAAAA), 0);
            }
        } else {
            lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_GREEN), 0);
            lv_obj_set_style_shadow_color(btn, lv_palette_main(LV_PALETTE_GREEN), 0);
            lv_label_set_text(lbl, LV_SYMBOL_PLAY " START");
            
            if (status_label) {
                const char* err = Chatbot_GetLastError();
                if (err) {
                    lv_label_set_text(status_label, err);
                    lv_obj_set_style_text_color(status_label, lv_palette_main(LV_PALETTE_RED), 0);
                } else {
                    lv_label_set_text(status_label, "Tap START to begin");
                    lv_obj_set_style_text_color(status_label, lv_color_hex(0xAAAAAA), 0);
                }
            }
        }
    };

    lv_timer_t* status_timer = lv_timer_create(update_ui_cb, 500, action_btn);

    lv_obj_add_event_cb(action_btn, [](lv_event_t* e) {
        if (!Chatbot_IsActive()) Chatbot_Start();
        else Chatbot_Stop();
    }, LV_EVENT_CLICKED, NULL);

    // Clean up timer when screen is deleted
    lv_obj_add_event_cb(chatbot_screen, [](lv_event_t* e) {
        lv_timer_t* t = (lv_timer_t*)lv_event_get_user_data(e);
        if (t) lv_timer_delete(t);
        status_label = nullptr;
    }, LV_EVENT_DELETE, status_timer);

    // --- Back button ---
    lv_obj_t* back_btn = lv_button_create(chatbot_screen);
    lv_obj_set_size(back_btn, 260, 50);
    lv_obj_set_pos(back_btn, 55, 315);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        // Stop chatbot if running before navigating away
        if (Chatbot_IsActive()) Chatbot_Stop();
        GUI_SwitchToScreen(GUI_CreateSourceScreen, &source_screen);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "< Back");
    lv_obj_center(back_label);

    Serial.println("GUI_CreateChatbotScreen created");
}
