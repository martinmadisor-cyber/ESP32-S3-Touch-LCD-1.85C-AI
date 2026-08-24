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

    // --- The one button: hold it, talk, let go ---
    // There used to be a second, small STREAM button. Both did the same job
    // through different paths; only the streaming one reached the assistant,
    // so the big button now does that and the small one is gone.
    lv_obj_t* speak_btn = lv_button_create(assistant_screen);
    lv_obj_set_size(speak_btn, 250, 250);
    lv_obj_center(speak_btn);
    lv_obj_set_style_radius(speak_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(speak_btn, lv_palette_darken(LV_PALETTE_LIGHT_BLUE, 2), 0);
    lv_obj_set_style_bg_opa(speak_btn, LV_OPA_COVER, 0);

    lv_obj_t* label = lv_label_create(speak_btn);
    lv_label_set_text(label, "SPEAK");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_40, 0);
    lv_obj_center(label);

    // On press: stream the microphone straight to the assistant
    lv_obj_add_event_cb(speak_btn, [](lv_event_t* e) {
        lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
        filename = generateRotatingFileName();
        Serial.printf("[STREAM] Start streaming: %s\n", filename.c_str());
        lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_RED), 0);

        MIC_StartRecording(filename.c_str(), 16000, 1, 16, MIC_MODE_TO_AI_CLIENT);
    }, LV_EVENT_PRESSED, NULL);

    // On release: stop and let the backend answer
    lv_obj_add_event_cb(speak_btn, [](lv_event_t* e) {
        lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
        Serial.println("[STREAM] Stop streaming");
        MIC_StopRecording();
        lv_obj_set_style_bg_color(btn, lv_palette_darken(LV_PALETTE_LIGHT_BLUE, 2), 0);
    }, LV_EVENT_RELEASED, NULL);

    // --- Back button ---
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
