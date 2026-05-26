#include "SystemDiagnosticsScreen.h"
#include "GUI.h"
#include <Arduino.h>
#include "esp_freertos_hooks.h"
#include "SD_Card.h"

// Widget pointers
static lv_obj_t* heap_arc = nullptr;
static lv_obj_t* heap_pct_lbl = nullptr;
static lv_obj_t* heap_det_lbl = nullptr;

static lv_obj_t* psram_arc = nullptr;
static lv_obj_t* psram_pct_lbl = nullptr;
static lv_obj_t* psram_det_lbl = nullptr;

static lv_obj_t* cpu0_arc = nullptr;
static lv_obj_t* cpu0_pct_lbl = nullptr;

static lv_obj_t* cpu1_arc = nullptr;
static lv_obj_t* cpu1_pct_lbl = nullptr;

static lv_obj_t* sd_arc = nullptr;
static lv_obj_t* sd_pct_lbl = nullptr;
static lv_obj_t* sd_det_lbl = nullptr;

static lv_obj_t* uptime_label = nullptr;
static lv_obj_t* info_label_1 = nullptr;
static lv_obj_t* info_label_2 = nullptr;

static lv_timer_t* diagnostics_timer = nullptr;

// Idle Hook variables
static volatile uint32_t idle_count_core0 = 0;
static volatile uint32_t idle_count_core1 = 0;
static uint32_t max_idle_count_core0 = 0;
static uint32_t max_idle_count_core1 = 0;
static bool hooks_registered = false;

static bool core0_idle_hook() {
    idle_count_core0 = idle_count_core0 + 1;
    return true;
}

static bool core1_idle_hook() {
    idle_count_core1 = idle_count_core1 + 1;
    return true;
}

// Timer Callback
static void diagnostics_timer_cb(lv_timer_t* timer) {
    // 1. Heap Memory
    uint32_t free_heap = ESP.getFreeHeap();
    uint32_t total_heap = ESP.getHeapSize();
    uint32_t used_heap = total_heap - free_heap;
    int heap_pct = total_heap > 0 ? (used_heap * 100 / total_heap) : 0;

    char heap_pct_buf[16];
    snprintf(heap_pct_buf, sizeof(heap_pct_buf), "%d%%", heap_pct);
    if (heap_pct_lbl) lv_label_set_text(heap_pct_lbl, heap_pct_buf);

    char heap_det_buf[32];
    snprintf(heap_det_buf, sizeof(heap_det_buf), "%d/%dK", used_heap / 1024, total_heap / 1024);
    if (heap_det_lbl) lv_label_set_text(heap_det_lbl, heap_det_buf);

    if (heap_arc) lv_arc_set_value(heap_arc, heap_pct);

    // 2. PSRAM Memory
    if (psramFound()) {
        uint32_t free_psram = ESP.getFreePsram();
        uint32_t total_psram = ESP.getPsramSize();
        uint32_t used_psram = total_psram - free_psram;
        int psram_pct = total_psram > 0 ? (used_psram * 100 / total_psram) : 0;

        char psram_pct_buf[16];
        snprintf(psram_pct_buf, sizeof(psram_pct_buf), "%d%%", psram_pct);
        if (psram_pct_lbl) lv_label_set_text(psram_pct_lbl, psram_pct_buf);

        char psram_det_buf[32];
        snprintf(psram_det_buf, sizeof(psram_det_buf), "%.1f/%.1fM", (float)used_psram / (1024 * 1024), (float)total_psram / (1024 * 1024));
        if (psram_det_lbl) lv_label_set_text(psram_det_lbl, psram_det_buf);

        if (psram_arc) lv_arc_set_value(psram_arc, psram_pct);
    } else {
        if (psram_pct_lbl) lv_label_set_text(psram_pct_lbl, "N/A");
        if (psram_det_lbl) lv_label_set_text(psram_det_lbl, "No PSRAM");
        if (psram_arc) lv_arc_set_value(psram_arc, 0);
    }

    // 3. CPU Core Load calculation
    uint32_t current_core0 = idle_count_core0;
    idle_count_core0 = 0;
    uint32_t current_core1 = idle_count_core1;
    idle_count_core1 = 0;

    if (current_core0 > max_idle_count_core0) {
        max_idle_count_core0 = current_core0;
    }
    if (current_core1 > max_idle_count_core1) {
        max_idle_count_core1 = current_core1;
    }

    int load_core0 = 0;
    if (max_idle_count_core0 > 0) {
        if (current_core0 >= max_idle_count_core0) {
            load_core0 = 0;
        } else {
            load_core0 = 100 - (current_core0 * 100 / max_idle_count_core0);
        }
    }

    int load_core1 = 0;
    if (max_idle_count_core1 > 0) {
        if (current_core1 >= max_idle_count_core1) {
            load_core1 = 0;
        } else {
            load_core1 = 100 - (current_core1 * 100 / max_idle_count_core1);
        }
    }

    char cpu0_buf[16];
    snprintf(cpu0_buf, sizeof(cpu0_buf), "%d%%", load_core0);
    if (cpu0_pct_lbl) lv_label_set_text(cpu0_pct_lbl, cpu0_buf);
    if (cpu0_arc) lv_arc_set_value(cpu0_arc, load_core0);

    char cpu1_buf[16];
    snprintf(cpu1_buf, sizeof(cpu1_buf), "%d%%", load_core1);
    if (cpu1_pct_lbl) lv_label_set_text(cpu1_pct_lbl, cpu1_buf);
    if (cpu1_arc) lv_arc_set_value(cpu1_arc, load_core1);

    // 4. Uptime
    unsigned long up_sec = millis() / 1000;
    int hours = up_sec / 3600;
    int mins = (up_sec % 3600) / 60;
    int secs = up_sec % 60;

    char uptime_buf[32];
    snprintf(uptime_buf, sizeof(uptime_buf), "Uptime: %02d:%02d:%02d", hours, mins, secs);
    if (uptime_label) lv_label_set_text(uptime_label, uptime_buf);

    // 5. SD Card Memory
    static int sd_check_counter = 0;
    if (sd_check_counter == 0) {
        if (SDCard_Flag) {
            uint64_t total_sd = SD_MMC.totalBytes();
            uint64_t used_sd = SD_MMC.usedBytes();
            int sd_pct = total_sd > 0 ? (used_sd * 100 / total_sd) : 0;

            char sd_pct_buf[16];
            snprintf(sd_pct_buf, sizeof(sd_pct_buf), "%d%%", sd_pct);
            if (sd_pct_lbl) lv_label_set_text(sd_pct_lbl, sd_pct_buf);

            char sd_det_buf[32];
            if (total_sd >= 1024 * 1024 * 1024ULL) {
                snprintf(sd_det_buf, sizeof(sd_det_buf), "%.1f/%.1fG", (float)used_sd / (1024 * 1024 * 1024ULL), (float)total_sd / (1024 * 1024 * 1024ULL));
            } else {
                snprintf(sd_det_buf, sizeof(sd_det_buf), "%.1f/%.1fM", (float)used_sd / (1024 * 1024), (float)total_sd / (1024 * 1024));
            }
            if (sd_det_lbl) lv_label_set_text(sd_det_lbl, sd_det_buf);

            if (sd_arc) lv_arc_set_value(sd_arc, sd_pct);
        } else {
            if (sd_pct_lbl) lv_label_set_text(sd_pct_lbl, "N/A");
            if (sd_det_lbl) lv_label_set_text(sd_det_lbl, "No SD");
            if (sd_arc) lv_arc_set_value(sd_arc, 0);
        }
    }
    sd_check_counter = (sd_check_counter + 1) % 5;
}

// Helper to create circular gauge and its labels
static lv_obj_t* create_gauge(lv_obj_t* parent, int cx, int cy, const char* name, lv_style_t* indic_style, lv_obj_t** pct_lbl_out, lv_obj_t** det_lbl_out) {
    // Create container centered at (cx, cy)
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_size(container, 100, 100);
    lv_obj_set_pos(container, cx - 50, cy - 50);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Name label (top mid)
    lv_obj_t* name_lbl = lv_label_create(container);
    lv_label_set_text(name_lbl, name);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(name_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align(name_lbl, LV_ALIGN_TOP_MID, 0, 0);

    // 2. Arc (bottom mid, size 70x70)
    lv_obj_t* arc = lv_arc_create(container);
    lv_obj_set_size(arc, 70, 70);
    lv_obj_align(arc, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    
    // Set Styles
    static lv_style_t style_arc_bg;
    static bool base_style_inited = false;
    if (!base_style_inited) {
        lv_style_init(&style_arc_bg);
        lv_style_set_arc_color(&style_arc_bg, lv_color_hex(0x2A2A2A));
        lv_style_set_arc_width(&style_arc_bg, 5);
        base_style_inited = true;
    }
    lv_obj_add_style(arc, &style_arc_bg, LV_PART_MAIN);
    lv_obj_add_style(arc, indic_style, LV_PART_INDICATOR);

    // 3. Pct label (center of arc)
    lv_obj_t* pct_lbl = lv_label_create(container);
    lv_label_set_text(pct_lbl, "--%");
    lv_obj_set_style_text_font(pct_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pct_lbl, lv_color_hex(0xFFFFFF), 0);
    
    if (det_lbl_out) {
        lv_obj_align_to(pct_lbl, arc, LV_ALIGN_CENTER, 0, -6);

        // 4. Det label (bottom center of arc)
        lv_obj_t* det_lbl = lv_label_create(container);
        lv_label_set_text(det_lbl, "--/--");
        lv_obj_set_style_text_font(det_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(det_lbl, lv_color_hex(0xAAAAAA), 0);
        lv_obj_align_to(det_lbl, arc, LV_ALIGN_CENTER, 0, 8);
        *det_lbl_out = det_lbl;
    } else {
        lv_obj_align_to(pct_lbl, arc, LV_ALIGN_CENTER, 0, 0);
    }
    *pct_lbl_out = pct_lbl;

    return arc;
}

// Create System Diagnostics Screen
void GUI_CreateSystemDiagnosticsScreen() {
    if (system_diagnostics_screen) return;
    Serial.println("Creating GUI_CreateSystemDiagnosticsScreen");

    // Main Screen Container
    system_diagnostics_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(system_diagnostics_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(system_diagnostics_screen, LV_SCROLLBAR_MODE_OFF);
    
    // Sleek Dark Background
    lv_obj_set_style_bg_color(system_diagnostics_screen, lv_color_hex(0x121212), 0);
    lv_obj_set_style_bg_opa(system_diagnostics_screen, LV_OPA_COVER, 0);

    // Initialize custom indicator styles once
    static bool styles_initialized = false;
    static lv_style_t style_heap_indic;
    static lv_style_t style_psram_indic;
    static lv_style_t style_cpu0_indic;
    static lv_style_t style_cpu1_indic;
    static lv_style_t style_sd_indic;

    if (!styles_initialized) {
        lv_style_init(&style_heap_indic);
        lv_style_set_arc_color(&style_heap_indic, lv_color_hex(0x00F5FF)); // Cyan
        lv_style_set_arc_width(&style_heap_indic, 5);

        lv_style_init(&style_psram_indic);
        lv_style_set_arc_color(&style_psram_indic, lv_color_hex(0xE91E63)); // Pink
        lv_style_set_arc_width(&style_psram_indic, 5);

        lv_style_init(&style_cpu0_indic);
        lv_style_set_arc_color(&style_cpu0_indic, lv_color_hex(0xFF6D00)); // Orange
        lv_style_set_arc_width(&style_cpu0_indic, 5);

        lv_style_init(&style_cpu1_indic);
        lv_style_set_arc_color(&style_cpu1_indic, lv_color_hex(0x00E676)); // Green
        lv_style_set_arc_width(&style_cpu1_indic, 5);

        lv_style_init(&style_sd_indic);
        lv_style_set_arc_color(&style_sd_indic, lv_color_hex(0xFFEB3B)); // Yellow
        lv_style_set_arc_width(&style_sd_indic, 5);

        styles_initialized = true;
    }

    // Register FreeRTOS idle hooks if not already registered
    if (!hooks_registered) {
        if (esp_register_freertos_idle_hook_for_cpu(core0_idle_hook, 0) == ESP_OK) {
            Serial.println("Core 0 idle hook registered");
        }
        if (esp_register_freertos_idle_hook_for_cpu(core1_idle_hook, 1) == ESP_OK) {
            Serial.println("Core 1 idle hook registered");
        }
        hooks_registered = true;
    }

    // Teardown event handler
    lv_obj_add_event_cb(system_diagnostics_screen, [](lv_event_t* e) {
        if (diagnostics_timer) {
            lv_timer_delete(diagnostics_timer);
            diagnostics_timer = nullptr;
        }
        if (hooks_registered) {
            esp_deregister_freertos_idle_hook_for_cpu(core0_idle_hook, 0);
            esp_deregister_freertos_idle_hook_for_cpu(core1_idle_hook, 1);
            hooks_registered = false;
        }
        heap_arc = nullptr;
        heap_pct_lbl = nullptr;
        heap_det_lbl = nullptr;
        psram_arc = nullptr;
        psram_pct_lbl = nullptr;
        psram_det_lbl = nullptr;
        cpu0_arc = nullptr;
        cpu0_pct_lbl = nullptr;
        cpu1_arc = nullptr;
        cpu1_pct_lbl = nullptr;
        sd_arc = nullptr;
        sd_pct_lbl = nullptr;
        sd_det_lbl = nullptr;
        uptime_label = nullptr;
        info_label_1 = nullptr;
        info_label_2 = nullptr;
        system_diagnostics_screen = nullptr;
        Serial.println("System Diagnostics screen resources released");
    }, LV_EVENT_DELETE, NULL);

    // Title label
    lv_obj_t* title_label = lv_label_create(system_diagnostics_screen);
    lv_label_set_text(title_label, "System Info");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 25);

    // Uptime label
    uptime_label = lv_label_create(system_diagnostics_screen);
    lv_label_set_text(uptime_label, "Uptime: 00:00:00");
    lv_obj_set_style_text_font(uptime_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(uptime_label, lv_color_hex(0x888888), 0);
    lv_obj_align(uptime_label, LV_ALIGN_TOP_MID, 0, 50);

    // Create 2x2 grid of circular gauges + 1 in the middle
    // Centers: Top-Left (100, 135), Top-Right (260, 135), Bottom-Left (100, 225), Bottom-Right (260, 225), Middle (180, 177)
    heap_arc = create_gauge(system_diagnostics_screen, 85, 130, "HEAP", &style_heap_indic, &heap_pct_lbl, &heap_det_lbl);
    psram_arc = create_gauge(system_diagnostics_screen, 275, 130, "PSRAM", &style_psram_indic, &psram_pct_lbl, &psram_det_lbl);
    cpu0_arc = create_gauge(system_diagnostics_screen, 85, 225, "CORE 0", &style_cpu0_indic, &cpu0_pct_lbl, nullptr);
    lv_obj_set_style_text_font(cpu0_pct_lbl, &lv_font_montserrat_18, 0);
    cpu1_arc = create_gauge(system_diagnostics_screen, 275, 225, "CORE 1", &style_cpu1_indic, &cpu1_pct_lbl, nullptr);
    lv_obj_set_style_text_font(cpu1_pct_lbl, &lv_font_montserrat_18, 0);
    sd_arc = create_gauge(system_diagnostics_screen, 180, 177, "SD CARD", &style_sd_indic, &sd_pct_lbl, &sd_det_lbl);

    // CPU & Flash details text
    info_label_1 = lv_label_create(system_diagnostics_screen);
    char info1_buf[64];
    snprintf(info1_buf, sizeof(info1_buf), "CPU: %d MHz  |  Flash: %d MB", ESP.getCpuFreqMHz(), (int)(ESP.getFlashChipSize() / (1024 * 1024)));
    lv_label_set_text(info_label_1, info1_buf);
    lv_obj_set_style_text_font(info_label_1, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(info_label_1, lv_color_hex(0x666666), 0);
    lv_obj_align(info_label_1, LV_ALIGN_TOP_MID, 0, 275);

    // SDK details text
    info_label_2 = lv_label_create(system_diagnostics_screen);
    char info2_buf[64];
    snprintf(info2_buf, sizeof(info2_buf), "SDK: %s", ESP.getSdkVersion());
    lv_label_set_text(info_label_2, info2_buf);
    lv_obj_set_style_text_font(info_label_2, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(info_label_2, lv_color_hex(0x666666), 0);
    lv_obj_align(info_label_2, LV_ALIGN_TOP_MID, 0, 290);

    // Back Button
    lv_obj_t* back_btn = lv_button_create(system_diagnostics_screen);
    lv_obj_set_size(back_btn, 260, 50);
    lv_obj_set_pos(back_btn, 55, 315);
    
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        if (system_diagnostics_screen) {
            lv_obj_del_async(system_diagnostics_screen);
        }
        GUI_SwitchToScreen(GUI_CreateConfigScreen, &config_screen);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* label_back = lv_label_create(back_btn);
    lv_label_set_text(label_back, "< Back");
    lv_obj_center(label_back);

    // Initial update
    diagnostics_timer_cb(nullptr);

    // Create 1-second refresh timer
    diagnostics_timer = lv_timer_create(diagnostics_timer_cb, 1000, nullptr);
}
