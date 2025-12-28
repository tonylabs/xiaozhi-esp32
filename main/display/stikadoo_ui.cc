#include "stikadoo_ui.h"

#include <esp_log.h>

static const char* TAG = "StikadooUI";

StikadooUI::StikadooUI(lv_obj_t* parent, const lv_font_t* text_font) {
    if (parent == nullptr) {
        ESP_LOGE(TAG, "Parent object is null, cannot create StikadooUI");
        return;
    }

    button_ = lv_btn_create(parent);
    lv_obj_center(button_);
    lv_obj_set_size(button_, 200, 90);

    label_ = lv_label_create(button_);
    lv_label_set_text(label_, "Speaking");
    lv_obj_center(label_);

    if (text_font != nullptr) {
        lv_obj_set_style_text_font(label_, text_font, 0);
    }

    ApplyStyle();
}

void StikadooUI::SetText(const std::string& text) {
    if (label_ == nullptr) {
        ESP_LOGW(TAG, "Attempted to set text but label is null");
        return;
    }
    lv_label_set_text(label_, text.c_str());
}

void StikadooUI::Show() {
    if (button_ != nullptr) {
        lv_obj_clear_flag(button_, LV_OBJ_FLAG_HIDDEN);
    }
}

void StikadooUI::Hide() {
    if (button_ != nullptr) {
        lv_obj_add_flag(button_, LV_OBJ_FLAG_HIDDEN);
    }
}

void StikadooUI::ApplyStyle() {
    if (button_ == nullptr) {
        return;
    }

    // White background with black border and rounded corners
    lv_obj_set_style_bg_color(button_, lv_color_white(), 0);
    lv_obj_set_style_border_color(button_, lv_color_black(), 0);
    lv_obj_set_style_border_width(button_, 6, 0);
    lv_obj_set_style_radius(button_, 12, 0);
    lv_obj_set_style_shadow_width(button_, 0, 0);

    if (label_ != nullptr) {
        lv_obj_set_style_text_color(label_, lv_color_black(), 0);
    }
}

