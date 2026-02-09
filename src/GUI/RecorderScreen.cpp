#include "GUI.h"
#include "RecorderScreen.h"
#include "MIC_MSM.h"
#include "SD_Card.h"

static String lastRecordedFile = "";

void GUI_CreateRecorderScreen() {
    if (recorder_screen) return;

    recorder_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(recorder_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(recorder_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(recorder_screen, lv_palette_main(LV_PALETTE_GREY), 0);


    // --- Small Play Button (on top) ---
    lv_obj_t* play_btn = lv_button_create(recorder_screen);
    lv_obj_set_size(play_btn, 80, 60);
    lv_obj_align(play_btn, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_color(play_btn, lv_palette_main(LV_PALETTE_GREEN), 0);

    lv_obj_t* play_label = lv_label_create(play_btn);
    lv_label_set_text(play_label, "PLAY");
    lv_obj_center(play_label);

    lv_obj_add_event_cb(play_btn, [](lv_event_t* e) {
        if (lastRecordedFile.length() > 0 && audio_ptr) {
            audio_ptr->connecttoFS(SD_MMC, lastRecordedFile.c_str());
        }
    }, LV_EVENT_CLICKED, NULL);

    // --- Big Record Button ---
    lv_obj_t* record_btn = lv_button_create(recorder_screen);
    lv_obj_set_size(record_btn, 210, 210);
    lv_obj_align(record_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(record_btn, lv_palette_main(LV_PALETTE_RED), 0);

    lv_obj_t* record_label = lv_label_create(record_btn);
    lv_label_set_text(record_label, "RECORD");
    lv_obj_set_style_text_font(record_label, &lv_font_montserrat_40, 0);
    lv_obj_center(record_label);

    // On press: start recording
    lv_obj_add_event_cb(record_btn, [](lv_event_t* e) {
        lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
        lastRecordedFile = generateRotatingFileName();
        lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_BLUE), 0);
        MIC_StartRecording(lastRecordedFile.c_str(), 16000, 1, 16, MIC_MODE_TO_FILE);
    }, LV_EVENT_PRESSED, NULL);

    // On release: stop recording
    lv_obj_add_event_cb(record_btn, [](lv_event_t* e) {
        lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
        MIC_StopRecording();
        lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_RED), 0);
    }, LV_EVENT_RELEASED, NULL);

    // --- Back button ---
    lv_obj_t* back_btn = lv_button_create(recorder_screen);
    lv_obj_set_size(back_btn, 260, 50);
    lv_obj_set_pos(back_btn, 55, 315);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        GUI_SwitchToScreen(GUI_CreateSourceScreen, &source_screen);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* label_back = lv_label_create(back_btn);
    lv_label_set_text(label_back, "< Back");
    lv_obj_center(label_back);
    Serial.println("GUI_CreateRecorderScreen created");
}