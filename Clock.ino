#include <Adafruit_NeoPixel.h>
#include <TroykaButton.h>
#include <TroykaRTC.h>
#include <Wire.h>

// Uncomment the line below to source time from an MCU circuit rather than RTC.
// Use one having best precision in hardware.
// #define TIME_SOURCE_MCU

// Pin definition

constexpr uint8_t BUTTON_MODE_PIN = A3;
constexpr uint8_t BUTTON_OK_PIN = A2;
constexpr uint8_t BUTTON_UP_PIN = A1;
constexpr uint8_t BUTTON_DOWN_PIN = A0;
constexpr uint8_t WS2812_DOT_PIN = 9;
constexpr uint8_t WS2812_MATRIX_PIN = 2;

// Class objects

Adafruit_NeoPixel dot = Adafruit_NeoPixel(1, WS2812_DOT_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel matrix = Adafruit_NeoPixel(40, WS2812_MATRIX_PIN, NEO_GRB + NEO_KHZ800);
RTC clock;
TroykaButton button_mode(BUTTON_MODE_PIN, 1000, true, 200);
TroykaButton button_ok(BUTTON_OK_PIN, 1000, true, 200);
TroykaButton button_up(BUTTON_UP_PIN, 1000, true, 200);
TroykaButton button_down(BUTTON_DOWN_PIN, 1000, true, 200);

// Time variables

uint8_t current_hour = 0;
uint8_t current_minute = 0;
uint8_t target_hour = 0;
uint8_t target_minute = 0;

#ifdef TIME_SOURCE_MCU
volatile uint16_t isr_counter = 0; // Used for clock source from AVR hardware timer
volatile uint8_t isr_current_hour = 0; // Used for clock source from AVR hardware timer
volatile uint8_t isr_current_minute = 0; // Used for clock source from AVR hardware timer
#endif

// LED variables

uint32_t now_time = 0;

uint32_t digit_color[4] = { 0, 0, 0, 0 };
uint32_t digit_previous_color[4] = { 0, 0, 0, 0 };

uint8_t digit_current_led[4] = { 255, 255, 255, 255 };
uint8_t digit_previous_led[4] = { 255, 255, 255, 255 };
uint8_t digit_old_led[4] = { 255, 255, 255, 255 };

bool dot_blinking_state = false;

#ifndef TIME_SOURCE_MCU
uint32_t dot_blinking_last_time = 0;
#endif

constexpr uint16_t MATRIX_BLINKING_INTERVAL_MS = 250;
uint32_t matrix_blinking_last_time = 0;
bool matrix_blinking_state = false;

// Modes variables and definitions

enum WorkingMode {
    WORKING_MODE_CLOCK = 1,
    WORKING_MODE_SET_TIME_HOURS = 2,
    WORKING_MODE_SET_TIME_MINUTES = 3,
    WORKING_MODE_MENU_1 = 4,
    WORKING_MODE_MENU_2 = 5,
    WORKING_MODE_MENU_3 = 6,
};

uint8_t current_working_mode = WORKING_MODE_CLOCK;
bool new_working_mode = false;

uint8_t current_setting_1_value = 0;
uint8_t current_setting_2_value = 0;
uint8_t current_setting_3_value = 0;

uint8_t target_setting_1_value = 0;
uint8_t target_setting_2_value = 0;
uint8_t target_setting_3_value = 0;

constexpr uint8_t TOTAL_SETTING_1_VALUES = 17;
constexpr uint8_t TOTAL_SETTING_2_VALUES = 4;
constexpr uint8_t TOTAL_SETTING_3_VALUES = 3;

// Color variables

int16_t delta_hue[4] = { 0, 0, 0, 0 };
uint8_t digit_brightness[4] = { 255, 255, 255, 255 };
uint8_t digit_previous_brightness[4] = { 255, 255, 255, 255 };
uint8_t max_brightness = 255;

// Animation variables

enum ColorAnimation {
    COLOR_ANIMATION_NONE = 0,
    COLOR_ANIMATION_PENDULUM = 1,
    COLOR_ANIMATION_WAVE_PENDULUM = 2,
    COLOR_ANIMATION_DEEPNESS = 3,
};

constexpr float COLOR_ANIMATION_RATE_MS = 12.0 / 60000.0; // Changes in ms
constexpr float COLOR_ANIMATION_PHASE_SHIFT = 0.125;
uint32_t previous_color_animation_time = 0;
float color_animation_timer = 0;

enum DigitAnimation {
    DIGIT_ANIMATION_NONE = 0,
    DIGIT_ANIMATION_RUNNING_LED = 1,
    DIGIT_ANIMATION_FADE = 2,
};

bool digit_animation_is_preview = false;
uint32_t previous_digit_animation_fade_preview_time = 0;
uint32_t previous_digit_animation_running_led_preview_time = 0;
constexpr uint32_t DIGIT_ANIMATION_RUNNING_LED_PREVIEW_TIME_MS = 4000;
constexpr uint32_t DIGIT_ANIMATION_FADE_PREVIEW_TIME_MS = 6500;

uint8_t running_led[4] = { 255, 255, 255, 255 };
constexpr uint32_t DIGIT_ANIMATION_RUNNING_LED_RATE_MS = 30;
uint32_t previous_digit_animation_running_led_time = 0;

constexpr float FADE_ANIMATION_RATE_MS = 3000.0; // 3 sec
uint32_t previous_fade_animation_time[4] = { 0, 0, 0, 0 };
float fade_animation_timer[4] = { 0, 0, 0, 0 };
uint8_t brightness_by_hour[24] = {
    20,  // 0
    10, // 1
    10, // 2
    10, // 3
    10, // 4
    30, // 5
    50, // 6
    150, // 7
    255, // 8
    255, // 9
    255, // 10
    255, // 11
    255, // 12
    255, // 13
    255, // 14
    255, // 15
    255, // 16
    255, // 17
    255, // 18
    255, // 19
    230, // 20
    150, // 21
    50, // 22
    30, // 23
};

// Auxiliary

uint16_t degree_hue_to_uint16_hue(int16_t degree) {
    return (float)((degree % 360) / 360.0 * 65535.0);
}

// CLOCK

void read_time() {
    uint8_t previous_hour = current_hour;
    clock.read();
    current_hour = clock.getHour();
    current_minute = clock.getMinute();
#ifdef TIME_SOURCE_MCU
    set_volatile_data();
#endif
    max_brightness = brightness_by_hour[current_hour];
}

void read_ram_data() {
    uint8_t data = clock.getRAMData(0x0A); // Menu 1 setting
    current_setting_1_value = ((data > 0) && (data < TOTAL_SETTING_1_VALUES)) ? data : 0;
    data = clock.getRAMData(0x0B); // Menu 2 setting
    current_setting_2_value = ((data > 0) && (data < TOTAL_SETTING_2_VALUES)) ? data : 0;
    data = clock.getRAMData(0x0C); // Menu 3 setting
    current_setting_3_value = ((data > 0) && (data < TOTAL_SETTING_3_VALUES)) ? data : 0;
}

void set_ram_data() {
    clock.setRAMData(0x0A, current_setting_1_value); // Menu 1 setting
    clock.setRAMData(0x0B, current_setting_2_value); // Menu 2 setting
    clock.setRAMData(0x0C, current_setting_3_value); // Menu 3 setting
}

// MODE button

void button_mode_handler() {
    button_mode.read();

    if (button_mode.isClick()) {

        switch (current_working_mode) {
        case WORKING_MODE_CLOCK:
            current_working_mode = WORKING_MODE_SET_TIME_HOURS;
            break;
        case WORKING_MODE_SET_TIME_HOURS:
            clock.setHour(target_hour);
            clock.setSecond(0);
            read_time();
            current_working_mode = WORKING_MODE_SET_TIME_MINUTES;
            break;
        case WORKING_MODE_SET_TIME_MINUTES:
            clock.setMinute(target_minute);
            clock.setSecond(0);
            read_time();
            current_working_mode = WORKING_MODE_CLOCK;
            break;
        case WORKING_MODE_MENU_1:
            current_setting_1_value = target_setting_1_value;
            set_ram_data();
            current_working_mode = WORKING_MODE_MENU_2;
            break;
        case WORKING_MODE_MENU_2:
            current_setting_2_value = target_setting_2_value;
            set_ram_data();
            current_working_mode = WORKING_MODE_MENU_3;
            break;
        case WORKING_MODE_MENU_3:
            current_setting_3_value = target_setting_3_value;
            set_ram_data();
            current_working_mode = WORKING_MODE_CLOCK;
            break;
        default:
            break;
        }
        new_working_mode = true;
    } else if (button_mode.isHold()) {

        switch (current_working_mode) {
        case WORKING_MODE_CLOCK:
            current_working_mode = WORKING_MODE_MENU_1;
            break;
        default:
            break;
        }
        new_working_mode = true;
    }
}

// UP button

void button_up_handler() {
    button_up.read();

    if (!button_up.isClickOnHold() && !button_up.justPressed())
        return;

    switch (current_working_mode) {
    case WORKING_MODE_SET_TIME_HOURS:
        target_hour = (target_hour + 1) % 24;
        break;
    case WORKING_MODE_SET_TIME_MINUTES:
        target_minute = (target_minute + 1) % 60;
        break;
    case WORKING_MODE_MENU_1:
        target_setting_1_value = (target_setting_1_value + 1) % TOTAL_SETTING_1_VALUES;
        break;
    case WORKING_MODE_MENU_2:
        target_setting_2_value = (target_setting_2_value + 1) % TOTAL_SETTING_2_VALUES;
        break;
    case WORKING_MODE_MENU_3:
        target_setting_3_value = (target_setting_3_value + 1) % TOTAL_SETTING_3_VALUES;
        break;
    default:
        break;
    }
}

// DOWN button

void button_down_handler() {
    button_down.read();

    if (!button_down.isClickOnHold() && !button_down.justPressed())
        return;

    switch (current_working_mode) {
    case WORKING_MODE_SET_TIME_HOURS:
        target_hour = (target_hour - 1 + 24) % 24;
        break;
    case WORKING_MODE_SET_TIME_MINUTES:
        target_minute = (target_minute - 1 + 60) % 60;
        break;
    case WORKING_MODE_MENU_1:
        target_setting_1_value = (target_setting_1_value - 1 + TOTAL_SETTING_1_VALUES) % TOTAL_SETTING_1_VALUES;
        break;
    case WORKING_MODE_MENU_2:
        target_setting_2_value = (target_setting_2_value - 1 + TOTAL_SETTING_2_VALUES) % TOTAL_SETTING_2_VALUES;
        break;
    case WORKING_MODE_MENU_3:
        target_setting_3_value = (target_setting_3_value - 1 + TOTAL_SETTING_3_VALUES) % TOTAL_SETTING_3_VALUES;
        break;
    default:
        break;
    }
}

// OK button

void button_ok_handler() {
    button_ok.read();

    if (!button_ok.isClick())
        return;

    switch (current_working_mode) {
    case WORKING_MODE_SET_TIME_HOURS:
    case WORKING_MODE_SET_TIME_MINUTES:
        clock.setHour(target_hour);
        clock.setMinute(target_minute);
        clock.setSecond(0);
        read_time();
        current_working_mode = WORKING_MODE_CLOCK;
        break;
    case WORKING_MODE_MENU_1:
    case WORKING_MODE_MENU_2:
    case WORKING_MODE_MENU_3:
        current_setting_1_value = target_setting_1_value;
        current_setting_2_value = target_setting_2_value;
        current_setting_3_value = target_setting_3_value;
        set_ram_data();
        current_working_mode = WORKING_MODE_CLOCK;
        break;
    default:
        break;
    }
    new_working_mode = true;
}

void working_mode_handler() {
    if (!new_working_mode)
        return;

    switch (current_working_mode) {
    case WORKING_MODE_SET_TIME_HOURS:
        target_hour = current_hour;
        target_minute = current_minute;
        break;
    case WORKING_MODE_MENU_1:
        target_setting_1_value = current_setting_1_value;
        break;
    case WORKING_MODE_MENU_2:
        target_setting_2_value = current_setting_2_value;
        break;
    case WORKING_MODE_MENU_3:
        target_setting_3_value = current_setting_3_value;
        break;
    default:
        break;
    }
    new_working_mode = false;
}

uint32_t nth_preset_color(uint8_t current_color_setting, int16_t hue_change = 0, uint8_t brightness = 255) {
    return (current_color_setting < 16 && current_color_setting > 0) ? Adafruit_NeoPixel::ColorHSV(degree_hue_to_uint16_hue((current_color_setting - 1) * 24 + hue_change), 255, brightness) : 0xFFFFFFFF;
}

void dot_handler() {

    uint32_t dot_color;

    switch (current_working_mode) {
    case WORKING_MODE_CLOCK:
        if (dot_blinking_state) {
            dot_color = nth_preset_color(current_setting_1_value);
            dot.setPixelColor(0, max_brightness, max_brightness, max_brightness, max_brightness);
        } else {
            dot.setPixelColor(0, 0);
        }
        break;
    default:
        dot.setPixelColor(0, 0);
        break;
    }
    dot.show();
}

float saw_wave(float x) {
    float fract = fmod(x, 1);
    float y = (fract < 0.5) ? (fract / 0.5) : (((fract - 0.5) / -0.5) + 1.0);
    return y = 2 * y - 1;
}

void color_animation_handler() {

    uint32_t dt = now_time - previous_color_animation_time;
    color_animation_timer = color_animation_timer + (dt * COLOR_ANIMATION_RATE_MS);

    float a = 0;

    previous_color_animation_time = now_time;

    if (target_setting_2_value == COLOR_ANIMATION_PENDULUM || current_setting_2_value == COLOR_ANIMATION_PENDULUM) {
        a = saw_wave(color_animation_timer);
        for (uint8_t i = 0; i < 4; i++)
            delta_hue[i] = 24 * a;
    } else if (target_setting_2_value == COLOR_ANIMATION_WAVE_PENDULUM || current_setting_2_value == COLOR_ANIMATION_WAVE_PENDULUM) {
        for (uint8_t i = 0; i < 4; i++) {
            a = saw_wave(color_animation_timer + (i + 1) * COLOR_ANIMATION_PHASE_SHIFT);
            delta_hue[i] = 24 * a;
        }
    } else if (target_setting_2_value == COLOR_ANIMATION_DEEPNESS || current_setting_2_value == COLOR_ANIMATION_DEEPNESS) {
        for (uint8_t i = 0; i < 4; i++)
            delta_hue[i] = (digit_current_led[i] - 10 * i) * 24.0 / 10.0;
    } else // COLOR_ANIMATION_NONE
        for (uint8_t i = 0; i < 4; i++)
            delta_hue[i] = 0;
}

void animation_running_led_handler() {

    if (now_time - previous_digit_animation_running_led_time > DIGIT_ANIMATION_RUNNING_LED_RATE_MS) {
        for (uint8_t i = 0; i < 4; i++)
            if (running_led[i] > digit_old_led[i])
                running_led[i]--;
        previous_digit_animation_running_led_time = now_time;
    }

    for (uint8_t i = 0; i < 4; i++) {
        if (digit_current_led[i] != digit_old_led[i]) {
            running_led[i] = 10 * (i + 1) - 1;
            digit_old_led[i] = digit_current_led[i];
        }
    }

    // Preview

    if (digit_animation_is_preview && (now_time - previous_digit_animation_running_led_preview_time > DIGIT_ANIMATION_RUNNING_LED_PREVIEW_TIME_MS)) {
        for (uint8_t i = 0; i < 4; i++)
            running_led[i] = 10 * (i + 1) - 1;
        previous_digit_animation_running_led_preview_time = now_time;
    }

    // Show
    
    for (uint8_t i = 0; i < 4; i++)
        digit_current_led[i] = running_led[i];
}

float fade_wave(float x) {
    float fract = fmod(x, 1);
    return pow(fract == 0 ? 1 : fract, 2);
}

void animation_fade_handler() {

    for (uint8_t i = 0; i < 4; i++) {
        if (digit_current_led[i] != digit_old_led[i]) {
            digit_previous_led[i] = digit_old_led[i];
            if (fade_animation_timer[i] >= 1.0) {
                fade_animation_timer[i] = 0;
            }
        }
    }

    for (uint8_t i = 0; i < 4; i++) {
        if (fade_animation_timer[i] < 1.0) {
            uint32_t dt = now_time - previous_fade_animation_time[i];
            fade_animation_timer[i] = fade_animation_timer[i] + (dt * (1.0 / FADE_ANIMATION_RATE_MS));
        }
        if (fade_animation_timer[i] >= 1.0) {
            fade_animation_timer[i] = 1.0;
            digit_old_led[i] = digit_current_led[i];
        }
        previous_fade_animation_time[i] = now_time;
    }

    // Preview

    if (digit_animation_is_preview && (now_time - previous_digit_animation_fade_preview_time > DIGIT_ANIMATION_FADE_PREVIEW_TIME_MS)) {
        for (uint8_t i = 0; i < 4; i++) {
            digit_previous_led[i] = 255;
            fade_animation_timer[i] = 0;
        }
        previous_digit_animation_fade_preview_time = now_time;
    }

    // Show
    
    for (uint8_t i = 0; i < 4; i++) {
        float a = fade_wave(fade_animation_timer[i]);
        digit_brightness[i] = max_brightness * a;
        digit_previous_brightness[i] = max_brightness * (1 - a);
    }
}

void matrix_handler() {
    matrix.clear();

    memset(digit_brightness, max_brightness, 4 * sizeof(digit_brightness[0]));

    // Choose digits

    switch (current_working_mode) {
    case WORKING_MODE_SET_TIME_HOURS:
    case WORKING_MODE_SET_TIME_MINUTES:
        digit_current_led[0] = target_hour / 10;
        digit_current_led[1] = (target_hour % 10) + 10;
        digit_current_led[2] = (target_minute / 10) + 20;
        digit_current_led[3] = (target_minute % 10) + 30;
        break;
    case WORKING_MODE_CLOCK:
        digit_current_led[0] = current_hour / 10;
        digit_current_led[1] = (current_hour % 10) + 10;
        digit_current_led[2] = (current_minute / 10) + 20;
        digit_current_led[3] = (current_minute % 10) + 30;
        break;
    case WORKING_MODE_MENU_1:
        digit_current_led[0] = 1; // Menu 1
        digit_current_led[1] = 10;
        digit_current_led[2] = target_setting_1_value >= 9 ? (((target_setting_1_value + 1) / 10) + 20) : 20;
        digit_current_led[3] = ((target_setting_1_value + 1) % 10) + 30;
        break;
    case WORKING_MODE_MENU_2:
        digit_current_led[0] = 2; // Menu 2
        digit_current_led[1] = 10;
        digit_current_led[2] = target_setting_2_value >= 9 ? (((target_setting_2_value + 1) / 10) + 20) : 20;
        digit_current_led[3] = ((target_setting_2_value + 1) % 10) + 30;
        break;
    case WORKING_MODE_MENU_3:
        digit_current_led[0] = 3; // Menu 3
        digit_current_led[1] = 10;
        digit_current_led[2] = target_setting_3_value >= 9 ? (((target_setting_3_value + 1) / 10) + 20) : 20;
        digit_current_led[3] = ((target_setting_3_value + 1) % 10) + 30;
        break;
    default:
        break;
    }

    // Color animation

    color_animation_handler();

    switch (current_working_mode) {
    case WORKING_MODE_CLOCK:
        digit_animation_is_preview = false;
        if (current_setting_3_value == DIGIT_ANIMATION_RUNNING_LED)
            animation_running_led_handler();
        else if (current_setting_3_value == DIGIT_ANIMATION_FADE)
            animation_fade_handler();
        break;
    case WORKING_MODE_MENU_3:
        digit_animation_is_preview = true;
        if (target_setting_3_value == DIGIT_ANIMATION_RUNNING_LED)
            animation_running_led_handler();
        else if (target_setting_3_value == DIGIT_ANIMATION_FADE)
            animation_fade_handler();
        break;
    default:
        break;
    }

    // Choose colors

    switch (current_working_mode) {
    case WORKING_MODE_SET_TIME_HOURS:
    case WORKING_MODE_SET_TIME_MINUTES:
    case WORKING_MODE_CLOCK:
        for (uint8_t i = 0; i < 4; i++) {
            if (current_setting_1_value == 16) {
                digit_color[i] = Adafruit_NeoPixel::ColorHSV(degree_hue_to_uint16_hue(i * 48 + delta_hue[i]), 255, digit_brightness[i]);
                if (current_setting_3_value == DIGIT_ANIMATION_FADE)
                    digit_previous_color[i] = Adafruit_NeoPixel::ColorHSV(degree_hue_to_uint16_hue(i * 48 + delta_hue[i]), 255, digit_previous_brightness[i]);
            } else {
                digit_color[i] = nth_preset_color(current_setting_1_value, delta_hue[i], digit_brightness[i]);
                if (current_setting_3_value == DIGIT_ANIMATION_FADE)
                    digit_previous_color[i] = nth_preset_color(current_setting_1_value, delta_hue[i], digit_previous_brightness[i]);
            }
        }
        break;
    case WORKING_MODE_MENU_1:
    case WORKING_MODE_MENU_2:
    case WORKING_MODE_MENU_3:
        for (uint8_t i = 0; i < 4; i++)
            if (target_setting_1_value == 16)
                digit_color[i] = Adafruit_NeoPixel::ColorHSV(degree_hue_to_uint16_hue(i * 48 + delta_hue[i]), 255, digit_brightness[i]);
            else
                digit_color[i] = nth_preset_color(target_setting_1_value, delta_hue[i], digit_brightness[i]);
        break;
    default:
        break;
    }

    // Show digits

    switch (current_working_mode) {
    case WORKING_MODE_SET_TIME_HOURS:
    case WORKING_MODE_SET_TIME_MINUTES:

        if ((now_time - matrix_blinking_last_time) >= MATRIX_BLINKING_INTERVAL_MS) {
            matrix_blinking_last_time = now_time;
            matrix_blinking_state = !matrix_blinking_state;
        }

        if (button_up.isPressed() || button_down.isPressed())
            matrix_blinking_state = true;

        if (current_working_mode == WORKING_MODE_SET_TIME_HOURS) {
            matrix.setPixelColor(digit_current_led[0], matrix_blinking_state ? digit_color[0] : 0);
            matrix.setPixelColor(digit_current_led[1], matrix_blinking_state ? digit_color[1] : 0);
            matrix.setPixelColor(digit_current_led[2], digit_color[2]);
            matrix.setPixelColor(digit_current_led[3], digit_color[3]);
        } else if (current_working_mode == WORKING_MODE_SET_TIME_MINUTES) {
            matrix.setPixelColor(digit_current_led[0], digit_color[0]);
            matrix.setPixelColor(digit_current_led[1], digit_color[1]);
            matrix.setPixelColor(digit_current_led[2], matrix_blinking_state ? digit_color[2] : 0);
            matrix.setPixelColor(digit_current_led[3], matrix_blinking_state ? digit_color[3] : 0);
        }
        break;
    case WORKING_MODE_CLOCK:
        for (uint8_t i = 0; i < 4; i++) {
            matrix.setPixelColor(digit_current_led[i], digit_color[i]);
            if (current_setting_3_value == DIGIT_ANIMATION_FADE)
                matrix.setPixelColor(digit_previous_led[i], digit_previous_color[i]);
        }
        break;
    case WORKING_MODE_MENU_1:
        matrix.setPixelColor(digit_current_led[0], digit_color[0]);
        matrix.setPixelColor(digit_current_led[2], digit_color[2]);
        matrix.setPixelColor(digit_current_led[3], digit_color[3]);
        break;
    case WORKING_MODE_MENU_2:
        matrix.setPixelColor(digit_current_led[0], digit_color[0]);
        matrix.setPixelColor(digit_current_led[2], digit_color[2]);
        matrix.setPixelColor(digit_current_led[3], digit_color[3]);
        break;
    case WORKING_MODE_MENU_3:
        matrix.setPixelColor(digit_current_led[0], digit_color[0]);
        matrix.setPixelColor(digit_current_led[2], digit_color[2]);
        matrix.setPixelColor(digit_current_led[3], digit_color[3]);
        break;
    default:
        break;
    }

    matrix.setBrightness(255);
    matrix.show();
}

// 1Hz hardware timer

#ifdef TIME_SOURCE_MCU
ISR(TIMER1_COMPA_vect) {
    isr_counter++;

    if (isr_counter >= 60) {
        isr_counter = 0;
        isr_current_minute++;
    }
    if (isr_current_minute >= 60) {
        isr_current_minute = 0;
        isr_current_hour++;
    }
    if (isr_current_hour >= 24)
        isr_current_hour = 0;

    dot_blinking_state = !dot_blinking_state;
}

void start_blink_timer() {
    cli();
    TCCR1A = 0; // Set entire TCCR1A register to 0
    TCCR1B = 0; // Same for TCCR1B
    TCNT1 = 0; // Initialize counter value to 0
    // Set compare match register for 1hz increments
    OCR1A = 15624; // = (16*10^6) / (1*1024) - 1 (must be <65536)
    TCCR1B |= _BV(WGM12); // Turn on CTC mode
    TCCR1B |= _BV(CS12) | _BV(CS10); // Set CS12 and CS10 bits for 1024 prescaler
    TIMSK1 |= _BV(OCIE1A); // Enable blinking
    TIMSK1 |= _BV(OCIE1A);
    sei();
}

void stop_blink_timer() {
    TCCR1A = 0; // Set entire TCCR1A register to 0
    TCCR1B = 0; // Same for TCCR1B
    TCNT1 = 0; // Initialize counter value to 0
}

void fetch_volatile_data() {
    cli();
    current_hour = isr_current_hour;
    current_minute = isr_current_minute;
    sei();
}

void set_volatile_data() {
    cli();
    isr_current_hour = current_hour;
    isr_current_minute = current_minute;
    sei();
}
#endif

// Setup

void setup() {
    button_mode.begin();
    button_ok.begin();
    button_up.begin();
    button_down.begin();

    clock.begin();
    read_time();
    clock.set(current_hour, current_minute, 0, 3, 1, 2022, 1);

    read_ram_data();

    dot.begin();
    matrix.begin();
    dot.clear();
    dot.setBrightness(8);
    matrix.clear();
    
#ifdef TIME_SOURCE_MCU
    start_blink_timer();
#endif
}

void loop() {
    now_time = millis();

#ifdef TIME_SOURCE_MCU
    fetch_volatile_data();
#else
    read_time();
    if (now_time - dot_blinking_last_time >= 1000) {
      dot_blinking_state = !dot_blinking_state;      
      dot_blinking_last_time = now_time;
    }
#endif

    button_mode_handler();
    button_ok_handler();
    button_up_handler();
    button_down_handler();
    working_mode_handler();
    matrix_handler();
    dot_handler();
}