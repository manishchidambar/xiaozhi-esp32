#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "deskbuddy_face_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "settings.h"
#include "power_save_timer.h"
#include "press_to_talk_mcp_tool.h"
#include "adc_battery_monitor.h"
#include "math_tools.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <atomic>

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#define TAG "DeskBuddyC3Board"

// Touch sensor controls (TTP223 on GPIO10):
//   1 tap  : start / stop a conversation (click-to-talk mode, default)
//   hold   : talk while held (press-to-talk mode)
//   3 taps : tickle, the face laughs
//   5 taps : open the Wi-Fi setup hotspot (add a new network)
//   Switch modes by voice: "switch to press to talk mode" / "switch to click to talk mode".
// Every touch makes the face react and wakes the screen.
// Screen turns off after 2 minutes idle.
// Amp is switched off (SD pin LOW) whenever the bot is silent.
// Battery % shown on the OLED (needs 100k/100k divider to GPIO0).
// Volume by voice: "set volume to 60".
// Exact maths tools (calculator, statistics, equations, vectors, matrices, ML metrics)
// are registered for the AI in math_tools.cc.

// Duplex I2S codec that also switches the MAX98357A on/off through its SD pin.
// Xiaozhi turns speaker output off after 15 s of silence, so the amp is muted
// whenever the bot is not talking: no idle hiss and lower battery drain.
class DeskBuddyAudioCodec : public NoAudioCodecDuplex {
private:
    gpio_num_t amp_sd_pin_;

public:
    DeskBuddyAudioCodec(int input_rate, int output_rate, gpio_num_t bclk, gpio_num_t ws,
                        gpio_num_t dout, gpio_num_t din, gpio_num_t amp_sd_pin)
        : NoAudioCodecDuplex(input_rate, output_rate, bclk, ws, dout, din), amp_sd_pin_(amp_sd_pin) {
        if (amp_sd_pin_ != GPIO_NUM_NC) {
            gpio_config_t io_conf = {};
            io_conf.pin_bit_mask = 1ULL << amp_sd_pin_;
            io_conf.mode = GPIO_MODE_OUTPUT;
            gpio_config(&io_conf);
            gpio_set_level(amp_sd_pin_, 0);
        }
    }

    virtual void EnableOutput(bool enable) override {
        if (enable && amp_sd_pin_ != GPIO_NUM_NC) {
            gpio_set_level(amp_sd_pin_, 1);
        }
        NoAudioCodecDuplex::EnableOutput(enable);
        if (!enable && amp_sd_pin_ != GPIO_NUM_NC) {
            gpio_set_level(amp_sd_pin_, 0);
        }
    }
};

class DeskBuddyC3Board : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    DeskBuddyFaceDisplay* face_ = nullptr;
    Button boot_button_;
    Button touch_button_;
    PowerSaveTimer* power_save_timer_ = nullptr;
    PressToTalkMcpTool* press_to_talk_tool_ = nullptr;
    AdcBatteryMonitor* battery_monitor_ = nullptr;
    esp_timer_handle_t tap_timer_ = nullptr;
    std::atomic<int> tap_count_{0};

    bool IsPressToTalk() {
        return press_to_talk_tool_ && press_to_talk_tool_->IsPressToTalkEnabled();
    }

    void WakeScreen() {
        if (power_save_timer_) {
            power_save_timer_->WakeUp();
        }
    }

    void InitializePowerSaveTimer() {
        // -1 = keep CPU speed (no light sleep, so USB flashing always works).
        // Only the OLED is turned off after 120 s idle, to save battery and avoid burn-in.
        power_save_timer_ = new PowerSaveTimer(-1, 120);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeBatteryMonitor() {
        battery_monitor_ = new AdcBatteryMonitor(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL,
            BATTERY_UPPER_RESISTOR, BATTERY_LOWER_RESISTOR, GPIO_NUM_NC);
    }

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeOledDisplay() {
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .scl_speed_hz = 400 * 1000,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(display_i2c_bus_, &io_config, &panel_io_));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            // Bot still works (voice only) if the OLED is missing or miswired
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        face_ = new DeskBuddyFaceDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        display_ = face_;
    }

    void InitializeButtons() {
        // Onboard BOOT button (inside the case): same as before
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        // Touch sensor
        touch_button_.OnPressDown([this]() {
            WakeScreen();
            if (face_) {
                face_->OnTouch();
            }
            if (IsPressToTalk()) {
                Application::GetInstance().StartListening();
            }
        });
        touch_button_.OnPressUp([this]() {
            if (IsPressToTalk()) {
                Application::GetInstance().StopListening();
            }
        });
        // Taps are counted, and acted on 450 ms after the last one,
        // so 1, 3 and 5 taps can each do something different.
        touch_button_.OnClick([this]() {
            tap_count_++;
            esp_timer_stop(tap_timer_);
            esp_timer_start_once(tap_timer_, 450 * 1000);
        });
    }

    void InitializeTapTimer() {
        esp_timer_create_args_t args = {};
        args.callback = [](void* arg) {
            static_cast<DeskBuddyC3Board*>(arg)->HandleTaps();
        };
        args.arg = this;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "tap_timer";
        ESP_ERROR_CHECK(esp_timer_create(&args, &tap_timer_));
    }

    void HandleTaps() {
        int taps = tap_count_.exchange(0);
        auto& app = Application::GetInstance();

        if (taps >= 5) {
            EnterWifiConfigMode();
            return;
        }
        if (taps == 3) {
            if (face_) {
                face_->OnTickle();
            }
            return;
        }
        if (taps == 1) {
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            if (!IsPressToTalk()) {
                app.ToggleChatState();
            }
        }
        // 2 or 4 taps: just the touch reaction, no action
    }

    void InitializeTools() {
        press_to_talk_tool_ = new PressToTalkMcpTool();
        press_to_talk_tool_->Initialize();
        RegisterDeskBuddyMathTools();
    }

public:
    DeskBuddyC3Board() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO, true) {
        InitializeDisplayI2c();
        InitializeOledDisplay();
        InitializeTapTimer();
        InitializeButtons();
        InitializePowerSaveTimer();
        InitializeBatteryMonitor();
        InitializeTools();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static DeskBuddyAudioCodec audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_AMP_SD_GPIO);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    // Shows battery icon on the OLED, plus the built-in low battery popup and sound
    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (!battery_monitor_) {
            return false;
        }
        charging = battery_monitor_->IsCharging();
        discharging = battery_monitor_->IsDischarging();
        level = battery_monitor_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            WakeScreen();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(DeskBuddyC3Board);
