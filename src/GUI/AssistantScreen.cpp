#include "GUI.h"
#include "AssistantScreen.h"
#include "MIC_MSM.h"


void GUI_CreateAssistantScreen() {
    if (assistant_screen) return;
    Serial.println("Creating GUI_CreateAssistantScreen");

    assistant_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(assistant_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(assistant_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(assistant_screen, lv_palette_main(LV_PALETTE_LIGHT_BLUE), 0);

    // --- Main SPEAK button ---
    lv_obj_t* speak_btn = lv_button_create(assistant_screen);
    lv_obj_set_size(speak_btn, 210, 210);
    lv_obj_center(speak_btn);
    lv_obj_set_style_bg_color(speak_btn, lv_palette_darken(LV_PALETTE_LIGHT_BLUE, 2), 0);
    lv_obj_set_style_bg_opa(speak_btn, LV_OPA_COVER, 0);

    lv_obj_t* label = lv_label_create(speak_btn);
    lv_label_set_text(label, "SPEAK");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_40, 0);
    lv_obj_center(label);

    // On press: start recording
    lv_obj_add_event_cb(speak_btn, [](lv_event_t* e) {
        lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
        filename = generateRotatingFileName();
        lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_RED), 0);

        MIC_StartRecording(filename.c_str(), 16000, 1, 16, MIC_MODE_TO_AI_CLIENT);
    }, LV_EVENT_PRESSED, NULL);

    // On release: stop + upload recording
    lv_obj_add_event_cb(speak_btn, [](lv_event_t* e) {
        lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
        Serial.println("Stop and play recording...");
        MIC_StopRecording();
        lv_obj_set_style_bg_color(btn, lv_palette_darken(LV_PALETTE_LIGHT_BLUE, 2), 0);
        delay(10);

        // No upload task: in assistant mode the audio already went out over
        // the WebSocket while you were holding the button.
    }, LV_EVENT_RELEASED, NULL);

    // --- STREAM button ---
    // The small STREAM button used to be the only one wired to the assistant
    // while the big SPEAK button just recorded to the card. Now SPEAK does the
    // talking, so the duplicate is gone.

    lv_obj_t* back_btn = lv_button_create(assistant_screen);
    lv_obj_set_size(back_btn, 260, 50);
    lv_obj_set_pos(back_btn, 55, 315);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        GUI_SwitchToScreen(GUI_CreateSourceScreen, &source_screen);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* label_back = lv_label_create(back_btn);
    lv_label_set_text(label_back, "< Back");
    lv_obj_center(label_back);
    Serial.println("GUI_CreateAssistantScreen created");
}
