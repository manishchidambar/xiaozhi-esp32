#include "deskbuddy_face_display.h"

#include "application.h"
#include "assets/lang_config.h"
#include "lvgl_font.h"
#include "lvgl_theme.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <string>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_random.h>
#include <esp_timer.h>

#define TAG "DeskBuddyFace"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_material_symbols_30_1);
LV_FONT_DECLARE(font_noto_emoji_30_1);

// On this monochrome LVGL port, black renders as a lit pixel (same as the stock OLED UI).
#define PIX_ON  lv_color_black()
#define PIX_OFF lv_color_white()

// Face geometry (128x64)
static constexpr int kFaceCenterY = 32;
static constexpr int kEyeCenterX[2] = {40, 88};
static constexpr int kBottomY = 48;
static constexpr int kTickMs = 50;

DeskBuddyFaceDisplay::DeskBuddyFaceDisplay(esp_lcd_panel_io_handle_t panel_io,
                                           esp_lcd_panel_handle_t panel, int width, int height,
                                           bool mirror_x, bool mirror_y)
    : panel_io_(panel_io), panel_(panel) {
    width_ = width;
    height_ = height;

    for (int i = 0; i < 2; i++) {
        cur_[i] = {30, 24, 8, 0, 0};
    }

    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_material_symbols_30_1);
    auto emoji_font = std::make_shared<LvglBuiltInFont>(&font_noto_emoji_30_1);

    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(large_icon_font);
    dark_theme->set_emoji_font(emoji_font);

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("dark", dark_theme);
    current_theme_ = dark_theme;

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.task_stack = 6144;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * height_),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = true,
        .rotation =
            {
                .swap_xy = false,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .flags =
            {
                .buff_dma = 1,
                .buff_spiram = 0,
                .sw_rotate = 0,
                .full_refresh = 0,
                .direct_mode = 0,
            },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
    }
}

DeskBuddyFaceDisplay::~DeskBuddyFaceDisplay() {
    if (anim_timer_ != nullptr) {
        lv_timer_delete(anim_timer_);
    }
    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
    lvgl_port_deinit();
}

bool DeskBuddyFaceDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }

void DeskBuddyFaceDisplay::Unlock() { lvgl_port_unlock(); }

uint32_t DeskBuddyFaceDisplay::NowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

uint32_t DeskBuddyFaceDisplay::Rand(uint32_t min, uint32_t max) {
    if (max <= min) {
        return min;
    }
    return min + (esp_random() % (max - min + 1));
}

lv_obj_t* DeskBuddyFaceDisplay::MakeBlock(lv_obj_t* parent, bool on) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, on ? PIX_ON : PIX_OFF, 0);
    return obj;
}

void DeskBuddyFaceDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping");
        return;
    }
    Display::SetupUI();

    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen, PIX_OFF, 0);
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, PIX_ON, 0);

    /* ---------- Face (created first so the top bar draws above any lid overflow) ---------- */
    for (int i = 0; i < 2; i++) {
        eye_[i] = MakeBlock(screen, true);
        lv_obj_set_size(eye_[i], 30, 24);
        lv_obj_set_style_radius(eye_[i], 8, 0);
    }
    for (int i = 0; i < 2; i++) {
        lid_box_[i] = MakeBlock(screen, false);
        lv_obj_add_flag(lid_box_[i], LV_OBJ_FLAG_HIDDEN);

        lid_pts_[i][0] = {0, 0};
        lid_pts_[i][1] = {1, 1};
        lid_line_[i] = lv_line_create(screen);
        lv_line_set_points(lid_line_[i], lid_pts_[i], 2);
        lv_obj_set_style_line_width(lid_line_[i], 26, 0);
        lv_obj_set_style_line_color(lid_line_[i], PIX_OFF, 0);
        lv_obj_set_style_line_rounded(lid_line_[i], false, 0);
        lv_obj_add_flag(lid_line_[i], LV_OBJ_FLAG_HIDDEN);
    }

    tear_ = MakeBlock(screen, true);
    lv_obj_set_size(tear_, 4, 6);
    lv_obj_set_style_radius(tear_, 2, 0);
    lv_obj_add_flag(tear_, LV_OBJ_FLAG_HIDDEN);

    bridge_pts_[0] = {0, 0};
    bridge_pts_[1] = {1, 0};
    bridge_ = lv_line_create(screen);
    lv_line_set_points(bridge_, bridge_pts_, 2);
    lv_obj_set_style_line_width(bridge_, 3, 0);
    lv_obj_set_style_line_color(bridge_, PIX_ON, 0);
    lv_obj_add_flag(bridge_, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 5; i++) {
        bars_[i] = MakeBlock(screen, true);
        lv_obj_set_size(bars_[i], 4, 4);
        lv_obj_set_style_radius(bars_[i], 1, 0);
        lv_obj_add_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* ---------- Bottom line: subtitle / dots / clock ---------- */
    bottom_label_ = lv_label_create(screen);
    lv_obj_set_width(bottom_label_, width_);
    lv_obj_set_pos(bottom_label_, 0, kBottomY);
    lv_obj_set_style_text_align(bottom_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(bottom_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(bottom_label_, "");

    static lv_anim_t scroll_anim;
    lv_anim_init(&scroll_anim);
    lv_anim_set_delay(&scroll_anim, 800);
    lv_anim_set_repeat_count(&scroll_anim, LV_ANIM_REPEAT_INFINITE);
    lv_obj_set_style_anim(bottom_label_, &scroll_anim, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(bottom_label_, lv_anim_speed_clamped(60, 300, 60000),
                                   LV_PART_MAIN);

    /* ---------- Top bar: icons ---------- */
    top_bar_ = lv_obj_create(screen);
    lv_obj_set_size(top_bar_, width_, 16);
    lv_obj_set_pos(top_bar_, 0, 0);
    lv_obj_set_style_radius(top_bar_, 0, 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar_, 0, 0);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_SCROLLABLE);

    network_label_ = lv_label_create(top_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(right_icons, LV_OBJ_FLAG_SCROLLABLE);

    mute_label_ = lv_label_create(right_icons);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    /* Status / notification text, centered over the top bar */
    lv_obj_t* status_bar = lv_obj_create(screen);
    lv_obj_set_size(status_bar, width_ - 40, 16);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 0, 0);
    lv_obj_set_scrollbar_mode(status_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    notification_label_ = lv_label_create(status_bar);
    lv_obj_set_width(notification_label_, width_ - 40);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(notification_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar);
    lv_obj_set_width(status_label_, width_ - 40);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

    /* Low battery popup (shown by LvglDisplay::UpdateStatusBar) */
    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, width_ * 9 / 10, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, PIX_ON, 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, PIX_OFF, 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    uint32_t now = NowMs();
    next_blink_ms_ = now + 2000;
    next_look_ms_ = now + 5000;
    idle_since_ms_ = now;
    emotion_ms_ = now;

    anim_timer_ = lv_timer_create(AnimTimerCb, kTickMs, this);
}

void DeskBuddyFaceDisplay::AnimTimerCb(lv_timer_t* timer) {
    auto self = static_cast<DeskBuddyFaceDisplay*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->Tick();
    }
}

DeskBuddyFaceDisplay::Expr DeskBuddyFaceDisplay::ExprFromName(const char* name) {
    if (name == nullptr) {
        return EXPR_NEUTRAL;
    }
    struct Map {
        const char* name;
        Expr expr;
    };
    static const Map kMap[] = {
        {"neutral", EXPR_NEUTRAL},     {"relaxed", EXPR_NEUTRAL},   {"confident", EXPR_NEUTRAL},
        {"happy", EXPR_HAPPY},         {"delicious", EXPR_HAPPY},   {"funny", EXPR_HAPPY},
        {"silly", EXPR_HAPPY},         {"laughing", EXPR_LAUGH},    {"sad", EXPR_SAD},
        {"crying", EXPR_CRY},          {"angry", EXPR_ANGRY},       {"surprised", EXPR_SURPRISED},
        {"shocked", EXPR_SURPRISED},   {"embarrassed", EXPR_SURPRISED},
        {"sleepy", EXPR_SLEEPY},       {"loving", EXPR_LOVE},       {"kissy", EXPR_LOVE},
        {"winking", EXPR_WINK},        {"cool", EXPR_COOL},         {"confused", EXPR_CONFUSED},
        {"thinking", EXPR_THINKING},
    };
    for (const auto& m : kMap) {
        if (strcmp(name, m.name) == 0) {
            return m.expr;
        }
    }
    return EXPR_NEUTRAL;
}

void DeskBuddyFaceDisplay::TargetFor(Expr e, int eye, EyeShape& t) {
    t = {30, 24, 8, 0, 0};
    switch (e) {
        case EXPR_LISTENING: t = {32, 28, 10, 0, 0}; break;
        case EXPR_THINKING:  t = {28, 18, 8, 6, -5}; break;
        case EXPR_HAPPY:
        case EXPR_LAUGH:     t = {30, 22, 11, 0, 0}; break;
        case EXPR_SAD:
        case EXPR_CRY:       t = {30, 22, 8, 0, 2}; break;
        case EXPR_ANGRY:     t = {30, 22, 6, 0, 0}; break;
        case EXPR_SURPRISED: t = {24, 30, 12, 0, 0}; break;
        case EXPR_SLEEPY:    t = {30, 5, 2, 0, 7}; break;
        case EXPR_LOVE:      t = {30, 26, 13, 0, 0}; break;
        case EXPR_WINK:
            if (eye == 1) t = {30, 3, 1, 0, 4};
            break;
        case EXPR_COOL:      t = {34, 12, 2, 0, -2}; break;
        case EXPR_CONFUSED:
            if (eye == 1) t = {22, 16, 7, 0, -3};
            break;
        default: break;
    }
}

void DeskBuddyFaceDisplay::SetBottomText(const std::string& text, bool scroll) {
    if (bottom_label_ == nullptr || text == bottom_text_) {
        return;
    }
    bottom_text_ = text;
    lv_label_set_long_mode(bottom_label_,
                           scroll ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
    lv_label_set_text(bottom_label_, text.c_str());
}

void DeskBuddyFaceDisplay::LayoutEye(int i, Expr e) {
    int w = std::max(2, (int)lroundf(cur_[i].w));
    int h = std::max(2, (int)lroundf(cur_[i].h));
    int r = std::min((int)lroundf(cur_[i].r), std::min(w, h) / 2);
    int x = kEyeCenterX[i] - w / 2 + (int)lroundf(cur_[i].dx);
    int y = kFaceCenterY - h / 2 + (int)lroundf(cur_[i].dy);

    lv_obj_set_pos(eye_[i], x, y);
    lv_obj_set_size(eye_[i], w, h);
    lv_obj_set_style_radius(eye_[i], r, 0);

    // Happy / laughing: cover the lower part with a rounded OFF box -> arch shaped eye
    if (e == EXPR_HAPPY || e == EXPR_LAUGH) {
        lv_obj_set_pos(lid_box_[i], x - 4, y + h / 2);
        lv_obj_set_size(lid_box_[i], w + 8, h);
        lv_obj_set_style_radius(lid_box_[i], h / 2, 0);
        lv_obj_remove_flag(lid_box_[i], LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(lid_box_[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Angry / sad / crying: thick OFF line cuts a slanted brow off the top of the eye.
    // The line is shifted up by half its width so everything above the brow is removed.
    bool angry = (e == EXPR_ANGRY);
    bool sad = (e == EXPR_SAD || e == EXPR_CRY);
    if (angry || sad) {
        const int shift = 13;
        int hi = y - 5 - shift;          // brow end above the eye
        int lo = y + h / 2 - shift;      // brow end cutting into the eye
        int x0 = x - 3;
        int x1 = x + w + 3;
        // Eye 0 is on the left, so its inner side is the right end (x1).
        // angry: inner brow end low (V shape), sad: inner brow end high (A shape)
        bool inner_low = angry;
        int y_left, y_right;
        if (i == 0) {
            y_left = inner_low ? hi : lo;
            y_right = inner_low ? lo : hi;
        } else {
            y_left = inner_low ? lo : hi;
            y_right = inner_low ? hi : lo;
        }
        lid_pts_[i][0] = {(lv_value_precise_t)x0, (lv_value_precise_t)y_left};
        lid_pts_[i][1] = {(lv_value_precise_t)x1, (lv_value_precise_t)y_right};
        lv_line_set_points(lid_line_[i], lid_pts_[i], 2);
        lv_obj_remove_flag(lid_line_[i], LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(lid_line_[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void DeskBuddyFaceDisplay::Tick() {
    if (eye_[0] == nullptr || sleeping_) {
        return;
    }

    uint32_t now = NowMs();
    tick_count_++;

    auto& app = Application::GetInstance();
    int state = (int)app.GetDeviceState();
    if (state != last_state_) {
        if (state == kDeviceStateIdle) {
            idle_since_ms_ = now;
        }
        last_state_ = state;
    }
    bool idle = (state == kDeviceStateIdle);
    bool listening = (state == kDeviceStateListening);
    bool speaking = (state == kDeviceStateSpeaking);
    bool connecting = (state == kDeviceStateConnecting);

    /* ---- Pick the expression ---- */
    Expr e = emotion_expr_;
    if (idle && now - emotion_ms_ > 20000) {
        e = EXPR_NEUTRAL;
    }
    if (listening) {
        e = EXPR_LISTENING;
    } else if (connecting) {
        e = EXPR_THINKING;
    }
    if (now < touch_until_ms_) {
        e = EXPR_SURPRISED;
    }
    if (now < tickle_until_ms_) {
        e = EXPR_LAUGH;
    }

    /* ---- Blink ---- */
    if (now >= next_blink_ms_) {
        blink_until_ms_ = now + 130;
        next_blink_ms_ = now + Rand(2500, 6000);
    }
    bool can_blink = !(e == EXPR_HAPPY || e == EXPR_LAUGH || e == EXPR_SLEEPY ||
                       e == EXPR_COOL || e == EXPR_WINK);
    bool blinking = can_blink && now < blink_until_ms_;

    /* ---- Glance left / right while idle ---- */
    if (idle && e == EXPR_NEUTRAL) {
        if (now >= next_look_ms_) {
            look_dx_ = (Rand(0, 1) == 0) ? -8.0f : 8.0f;
            look_until_ms_ = now + Rand(800, 1600);
            next_look_ms_ = now + Rand(4000, 9000);
        }
    } else {
        look_until_ms_ = 0;
    }
    float look = (now < look_until_ms_) ? look_dx_ : 0.0f;

    /* ---- Motion ---- */
    float t = (float)tick_count_;
    float bounce = 0;
    if (speaking) {
        bounce = sinf(t * 0.6f) * 2.0f;
    }
    if (e == EXPR_LAUGH) {
        bounce = sinf(t * 1.2f) * 2.5f;
    }
    float pulse = (e == EXPR_LOVE) ? (1.0f + 0.10f * sinf(t * 0.5f)) : 1.0f;

    for (int i = 0; i < 2; i++) {
        EyeShape tgt;
        TargetFor(e, i, tgt);
        tgt.w *= pulse;
        tgt.h *= pulse;
        tgt.dx += look;
        tgt.dy += bounce;
        if (blinking) {
            tgt.h = 2;
            tgt.r = 1;
        }
        const float k = 0.5f;
        cur_[i].w += (tgt.w - cur_[i].w) * k;
        cur_[i].h += (tgt.h - cur_[i].h) * k;
        cur_[i].r += (tgt.r - cur_[i].r) * k;
        cur_[i].dx += (tgt.dx - cur_[i].dx) * k;
        cur_[i].dy += (tgt.dy - cur_[i].dy) * k;
        LayoutEye(i, e);
    }

    /* ---- Tear (crying) ---- */
    if (e == EXPR_CRY) {
        int ex = kEyeCenterX[0] + 8 + (int)cur_[0].dx;
        int ey = kFaceCenterY + (int)(cur_[0].h / 2) + (int)cur_[0].dy + 1;
        // Falls from just under the eye down over the bottom line, then repeats
        int fall = (int)((tick_count_ * 2) % 16);
        lv_obj_set_pos(tear_, ex, std::min(ey + fall, 58));
        lv_obj_remove_flag(tear_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(tear_, LV_OBJ_FLAG_HIDDEN);
    }

    /* ---- Sunglasses bridge (cool) ---- */
    if (e == EXPR_COOL) {
        int y = kFaceCenterY - (int)(cur_[0].h / 2) + (int)cur_[0].dy + 3;
        int x0 = kEyeCenterX[0] + (int)(cur_[0].w / 2) + (int)cur_[0].dx;
        int x1 = kEyeCenterX[1] - (int)(cur_[1].w / 2) + (int)cur_[1].dx;
        bridge_pts_[0] = {(lv_value_precise_t)x0, (lv_value_precise_t)y};
        bridge_pts_[1] = {(lv_value_precise_t)x1, (lv_value_precise_t)y};
        lv_line_set_points(bridge_, bridge_pts_, 2);
        lv_obj_remove_flag(bridge_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(bridge_, LV_OBJ_FLAG_HIDDEN);
    }

    /* ---- Bottom line ---- */
    if (listening) {
        // Sound bars
        SetBottomText("", false);
        if (tick_count_ % 2 == 0) {
            const int bar_w = 4, gap = 3;
            int total = 5 * bar_w + 4 * gap;
            int x = (width_ - total) / 2;
            for (int b = 0; b < 5; b++) {
                int bh = (int)Rand(2, 12);
                lv_obj_set_pos(bars_[b], x + b * (bar_w + gap), 62 - bh);
                lv_obj_set_size(bars_[b], bar_w, bh);
                lv_obj_remove_flag(bars_[b], LV_OBJ_FLAG_HIDDEN);
            }
        }
    } else {
        for (int b = 0; b < 5; b++) {
            lv_obj_add_flag(bars_[b], LV_OBJ_FLAG_HIDDEN);
        }

        bool show_subtitle = !subtitle_.empty() && (speaking || (idle && now - idle_since_ms_ < 8000));
        if (e == EXPR_THINKING && !speaking) {
            if (tick_count_ % 8 == 0) {
                dots_phase_ = (dots_phase_ + 1) % 4;
            }
            static const char* kDots[] = {"", ".", "..", "..."};
            SetBottomText(kDots[dots_phase_], false);
        } else if (show_subtitle) {
            SetBottomText(subtitle_, true);
        } else if (idle && now - idle_since_ms_ >= 30000) {
            time_t raw = time(nullptr);
            struct tm* tm_now = localtime(&raw);
            if (tm_now != nullptr && tm_now->tm_year >= 2025 - 1900) {
                char buf[8];
                strftime(buf, sizeof(buf), "%H:%M", tm_now);
                SetBottomText(buf, false);
            } else {
                SetBottomText("", false);
            }
        } else {
            SetBottomText("", false);
        }
    }
}

void DeskBuddyFaceDisplay::SetStatus(const char* status) {
    // The base class shows HH:MM in the status bar while idle. The clock lives under
    // the eyes on this face, so keep the top bar clean instead of showing it twice.
    if (status != nullptr && strlen(status) == 5 && status[2] == ':') {
        LvglDisplay::SetStatus("");
        return;
    }
    LvglDisplay::SetStatus(status);
}

void DeskBuddyFaceDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    std::string text = (content != nullptr) ? content : "";
    std::replace(text.begin(), text.end(), '\n', ' ');
    subtitle_ = text;
}

void DeskBuddyFaceDisplay::RemoveChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (content == nullptr) {
        return;
    }
    std::string text = content;
    std::replace(text.begin(), text.end(), '\n', ' ');
    if (text == subtitle_) {
        subtitle_.clear();
    }
}

void DeskBuddyFaceDisplay::ClearChatMessages() {
    DisplayLockGuard lock(this);
    subtitle_.clear();
}

bool DeskBuddyFaceDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);
    emotion_expr_ = ExprFromName(emotion);
    emotion_ms_ = NowMs();
    return true;
}

void DeskBuddyFaceDisplay::OnTouch() {
    DisplayLockGuard lock(this);
    touch_until_ms_ = NowMs() + 350;
}

void DeskBuddyFaceDisplay::OnTickle() {
    DisplayLockGuard lock(this);
    tickle_until_ms_ = NowMs() + 2500;
    touch_until_ms_ = 0;
}

void DeskBuddyFaceDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);
    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    lv_obj_set_style_text_font(lv_screen_active(), lvgl_theme->text_font()->font(), 0);
}

void DeskBuddyFaceDisplay::SetPowerSaveMode(bool on) {
    {
        DisplayLockGuard lock(this);
        sleeping_ = on;
        if (anim_timer_ != nullptr) {
            if (on) {
                lv_timer_pause(anim_timer_);
            } else {
                uint32_t now = NowMs();
                idle_since_ms_ = now;
                next_blink_ms_ = now + 1000;
                lv_timer_resume(anim_timer_);
            }
        }
    }
    if (panel_ != nullptr) {
        esp_lcd_panel_disp_on_off(panel_, !on);
    }
}
