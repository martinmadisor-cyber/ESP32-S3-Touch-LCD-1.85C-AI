#include "GUI.h"
#include "MusicSpectrumScreen.h"

static lv_obj_t* bars[6] = {nullptr};
static lv_obj_t* track_label = nullptr;
static lv_timer_t* spectrum_timer = nullptr;

static lv_style_t style_bar_bg;
static lv_style_t style_bass_indic;
static lv_style_t style_mid_indic;
static lv_style_t style_high_indic;
static bool styles_initialized = false;

// --- Timer Callback to Update Spectrum Bars ---
static void spectrum_timer_cb(lv_timer_t* timer) {
    bool active = audio_ptr && audio_ptr->isRunning();
    uint8_t* spec = audio_ptr ? audio_ptr->getSpectrum() : nullptr;

    for (int i = 0; i < 6; i++) {
        if (!bars[i]) continue;
        
        int32_t current_val = lv_bar_get_value(bars[i]);
        int32_t target_val = 0;
        if (active && spec) {
            target_val = ((int32_t)spec[i] * 100) / 255;
        }

        // Apply a smooth fall-down (decay) if the target value is lower than current
        int32_t val = target_val;
        if (val < current_val) {
            val = current_val - 8; // decay step
            if (val < target_val) val = target_val;
        }
        
        if (val < 0) val = 0;
        if (val > 100) val = 100;
        
        lv_bar_set_value(bars[i], val, LV_ANIM_OFF);
    }

    // Dynamic track title update from the global message label
    if (track_label && message_label) {
        const char* track_text = lv_label_get_text(message_label);
        if (track_text && strlen(track_text) > 0) {
            if (strcmp(lv_label_get_text(track_label), track_text) != 0) {
                lv_label_set_text(track_label, track_text);
            }
        } else {
            if (strcmp(lv_label_get_text(track_label), "No Track Info") != 0) {
                lv_label_set_text(track_label, "No Track Info");
            }
        }
    }
}

// --- Create the Music Spectrum Screen ---
void GUI_CreateMusicSpectrumScreen() {
    if (music_spectrum_screen) return;
    Serial.println("Creating GUI_CreateMusicSpectrumScreen");

    // Main Screen Container
    music_spectrum_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(music_spectrum_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(music_spectrum_screen, LV_SCROLLBAR_MODE_OFF);
    
    // Sleek Dark Background
    lv_obj_set_style_bg_color(music_spectrum_screen, lv_color_hex(0x121212), 0);
    lv_obj_set_style_bg_opa(music_spectrum_screen, LV_OPA_COVER, 0);

    // Initialize custom styles once
    if (!styles_initialized) {
        // Shared background/track style
        lv_style_init(&style_bar_bg);
        lv_style_set_bg_color(&style_bar_bg, lv_color_hex(0x1F1F1F));
        lv_style_set_bg_opa(&style_bar_bg, LV_OPA_COVER);
        lv_style_set_radius(&style_bar_bg, 4);

        // Bass Indicator (Pink -> Deep Purple)
        lv_style_init(&style_bass_indic);
        lv_style_set_bg_opa(&style_bass_indic, LV_OPA_COVER);
        lv_style_set_bg_color(&style_bass_indic, lv_color_hex(0x7A1FA2));     // top
        lv_style_set_bg_grad_color(&style_bass_indic, lv_color_hex(0xE91E63)); // bottom
        lv_style_set_bg_grad_dir(&style_bass_indic, LV_GRAD_DIR_VER);
        lv_style_set_radius(&style_bass_indic, 4);

        // Mids Indicator (Cyan -> Blue)
        lv_style_init(&style_mid_indic);
        lv_style_set_bg_opa(&style_mid_indic, LV_OPA_COVER);
        lv_style_set_bg_color(&style_mid_indic, lv_color_hex(0x007AFF));     // top
        lv_style_set_bg_grad_color(&style_mid_indic, lv_color_hex(0x00F5FF)); // bottom
        lv_style_set_bg_grad_dir(&style_mid_indic, LV_GRAD_DIR_VER);
        lv_style_set_radius(&style_mid_indic, 4);

        // Highs Indicator (Yellow -> Neon Green)
        lv_style_init(&style_high_indic);
        lv_style_set_bg_opa(&style_high_indic, LV_OPA_COVER);
        lv_style_set_bg_color(&style_high_indic, lv_color_hex(0x00E676));     // top
        lv_style_set_bg_grad_color(&style_high_indic, lv_color_hex(0xFFEE00)); // bottom
        lv_style_set_bg_grad_dir(&style_high_indic, LV_GRAD_DIR_VER);
        lv_style_set_radius(&style_high_indic, 4);

        styles_initialized = true;
    }

    // Teardown event handler
    lv_obj_add_event_cb(music_spectrum_screen, [](lv_event_t* e) {
        if (spectrum_timer) {
            lv_timer_delete(spectrum_timer);
            spectrum_timer = nullptr;
        }
        for (int i = 0; i < 6; i++) {
            bars[i] = nullptr;
        }
        track_label = nullptr;
        music_spectrum_screen = nullptr;
        Serial.println("Music Visualizer screen resources released");
    }, LV_EVENT_DELETE, NULL);

    // Title label
    lv_obj_t* title_label = lv_label_create(music_spectrum_screen);
    lv_label_set_text(title_label, "Visualizer");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 25);

    // Subtitle
    lv_obj_t* sub_label = lv_label_create(music_spectrum_screen);
    lv_label_set_text(sub_label, "6-Band Audio Spectrum");
    lv_obj_set_style_text_font(sub_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sub_label, lv_color_hex(0x555555), 0);
    lv_obj_align(sub_label, LV_ALIGN_TOP_MID, 0, 48);

    // Horizontal Flex Layout Container for the Columns (specifically centered for round display)
    lv_obj_t* flex_container = lv_obj_create(music_spectrum_screen);
    lv_obj_set_size(flex_container, 260, 130);
    lv_obj_align(flex_container, LV_ALIGN_TOP_MID, 0, 115);
    lv_obj_set_flex_flow(flex_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(flex_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Flex Container Styling (Invisible)
    lv_obj_set_style_bg_opa(flex_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(flex_container, 0, 0);
    lv_obj_set_style_pad_all(flex_container, 0, 0);
    lv_obj_set_style_pad_column(flex_container, 6, 0);

    // Band Names/Freq Labels
    const char* band_labels[6] = {"60", "250", "1K", "4K", "8K", "16K"};

    // Create 6 Visualizer Columns
    for (int i = 0; i < 6; i++) {
        // Individual Column Flex Container
        lv_obj_t* col = lv_obj_create(flex_container);
        lv_obj_set_size(col, 34, 125);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        // Column Styling (Invisible)
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_style_pad_row(col, 2, 0);

        // Bar Widget
        bars[i] = lv_bar_create(col);
        lv_obj_set_size(bars[i], 16, 95);
        lv_bar_set_range(bars[i], 0, 100);
        lv_bar_set_value(bars[i], 0, LV_ANIM_OFF);
        
        // Add shared background style
        lv_obj_add_style(bars[i], &style_bar_bg, LV_PART_MAIN);

        // Add custom indicator style based on band frequency
        if (i < 2) {
            lv_obj_add_style(bars[i], &style_bass_indic, LV_PART_INDICATOR);
        } else if (i < 4) {
            lv_obj_add_style(bars[i], &style_mid_indic, LV_PART_INDICATOR);
        } else {
            lv_obj_add_style(bars[i], &style_high_indic, LV_PART_INDICATOR);
        }

        // Freq Label
        lv_obj_t* lbl = lv_label_create(col);
        lv_label_set_text(lbl, band_labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);
    }

    // Dynamic track/metadata scrolling text (utilizes bottom space above back button)
    track_label = lv_label_create(music_spectrum_screen);
    lv_label_set_text(track_label, "No Track Info");
    lv_obj_set_style_text_font(track_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(track_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_align(track_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(track_label, 260);
    lv_label_set_long_mode(track_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(track_label, LV_ALIGN_TOP_MID, 0, 270);

    // Back Button
    lv_obj_t* back_btn = lv_button_create(music_spectrum_screen);
    lv_obj_set_size(back_btn, 260, 50);
    lv_obj_set_pos(back_btn, 55, 315);
    
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        if (music_spectrum_screen) {
            lv_obj_del_async(music_spectrum_screen);
        }
        GUI_SwitchToScreen(GUI_CreateMainScreen, &main_screen);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* label_back = lv_label_create(back_btn);
    lv_label_set_text(label_back, "< Back");
    lv_obj_center(label_back);

    // Initial update
    spectrum_timer_cb(nullptr);

    // Create 30ms refresh timer (33 FPS)
    spectrum_timer = lv_timer_create(spectrum_timer_cb, 30, nullptr);
}
