#pragma once

#include <lvgl.h>
#include <string>

// Lightweight helper that builds the center “Speaking” button shown on the LCD.
// The button uses a white background, black border, rounded corners, and black text.
class StikadooUI {
public:
    StikadooUI(lv_obj_t* parent, const lv_font_t* text_font);
    ~StikadooUI() = default;

    lv_obj_t* root() const { return button_; }
    lv_obj_t* label() const { return label_; }

    void SetText(const std::string& text);
    void Show();
    void Hide();
    void ApplyStyle();

private:
    lv_obj_t* button_ = nullptr;
    lv_obj_t* label_ = nullptr;
};

