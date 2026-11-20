#ifndef DESKBUDDY_FACE_DISPLAY_H
#define DESKBUDDY_FACE_DISPLAY_H

#include "lvgl_display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#include <string>

// Animated robot face for a 128x64 monochrome OLED (SH1106 / SSD1306).
//
// Layout:
//   y  0..15  top bar: Wi-Fi icon | status text | mute + battery icons
//   y 16..47  two animated eyes
//   y 48..63  bottom line: subtitle / thinking dots / listening bars / clock
//
// The face follows the device state (idle, listening, thinking, speaking),
// shows the emotion the AI sends with each reply, and reacts to touch.
class DeskBuddyFaceDisplay : public LvglDisplay {
public:
    enum Expr {
        EXPR_NEUTRAL = 0,
        EXPR_HAPPY,
        EXPR_LAUGH,
        EXPR_SAD,
        EXPR_CRY,
        EXPR_ANGRY,
        EXPR_SURPRISED,
        EXPR_SLEEPY,
        EXPR_LOVE,
        EXPR_WINK,
        EXPR_COOL,
        EXPR_CONFUSED,
        EXPR_THINKING,
        EXPR_LISTENING,
    };

    DeskBuddyFaceDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                         int width, int height, bool mirror_x, bool mirror_y);
    ~DeskBuddyFaceDisplay();

    virtual void SetupUI() override;
    virtual void SetStatus(const char* status) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void RemoveChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual bool SetEmotion(const char* emotion) override;
    virtual void SetTheme(Theme* theme) override;
    virtual bool IsMonochrome() const override { return true; }
    virtual void SetPowerSaveMode(bool on) override;

    // Called by the board on touch events
    void OnTouch();     // quick surprised squish
    void OnTickle();    // laughing face for a moment

protected:
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

private:
    struct EyeShape {
        float w, h, r, dx, dy;
    };

    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    // UI objects
    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* eye_[2] = {nullptr, nullptr};
    lv_obj_t* lid_box_[2] = {nullptr, nullptr};     // OFF rounded box, cuts happy crescents
    lv_obj_t* lid_line_[2] = {nullptr, nullptr};    // OFF thick line, cuts angry / sad slants
    lv_obj_t* tear_ = nullptr;
    lv_obj_t* bridge_ = nullptr;                    // sunglasses bridge
    lv_obj_t* bars_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    lv_obj_t* bottom_label_ = nullptr;
    lv_timer_t* anim_timer_ = nullptr;

    lv_point_precise_t lid_pts_[2][2];
    lv_point_precise_t bridge_pts_[2];

    // Animation state (touched only from the LVGL task, or under the display lock)
    EyeShape cur_[2];
    Expr emotion_expr_ = EXPR_NEUTRAL;
    uint32_t emotion_ms_ = 0;
    uint32_t touch_until_ms_ = 0;
    uint32_t tickle_until_ms_ = 0;
    uint32_t next_blink_ms_ = 0;
    uint32_t blink_until_ms_ = 0;
    uint32_t next_look_ms_ = 0;
    uint32_t look_until_ms_ = 0;
    float look_dx_ = 0;
    uint32_t idle_since_ms_ = 0;
    int last_state_ = -1;
    int dots_phase_ = 0;
    uint32_t tick_count_ = 0;
    std::string subtitle_;
    std::string bottom_text_;
    bool sleeping_ = false;

    static void AnimTimerCb(lv_timer_t* timer);
    void Tick();
    void TargetFor(Expr e, int eye, EyeShape& out);
    void LayoutEye(int eye, Expr e);
    void SetBottomText(const std::string& text, bool scroll);
    lv_obj_t* MakeBlock(lv_obj_t* parent, bool on);
    static Expr ExprFromName(const char* name);
    static uint32_t NowMs();
    static uint32_t Rand(uint32_t min, uint32_t max);
};

#endif  // DESKBUDDY_FACE_DISPLAY_H
