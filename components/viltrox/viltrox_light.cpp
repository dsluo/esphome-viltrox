#include "viltrox_light.h"

#ifdef USE_ESP32

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include "esphome/components/light/light_state.h"
#include "esphome/core/helpers.h"

namespace esphome::viltrox {

// Command encoding (see PROTOCOL.md), mirroring
// BleLed.getAdvertisingData in the app. Byte 14 is the packet ID, filled in by
// the hub; byte 15 is always the brightness percent.

static constexpr uint8_t HEAD = 0xEF;
static constexpr uint8_t TAIL = 0xFE;

static uint8_t channel_group(uint8_t channel, uint8_t group) { return ((channel & 0x1F) << 3) | (group & 0x07); }

static Frame make_frame(std::initializer_list<int> body, uint8_t power) {
  Frame frame{};
  size_t i = 0;
  for (int b : body) {
    frame[i++] = b & 0xFF;
  }
  frame[FRAME_ID_INDEX] = 0;
  frame[15] = power;
  return frame;
}

// Power commands address every group on the channel (group bits 0). The app
// always sends 0x64 (100) in byte 4, the brightness byte of lighting commands;
// power-on sends the target brightness there instead, so the panel doesn't
// flash at 100% before the lighting command arrives.
static Frame encode_power(bool on, uint8_t channel, uint8_t power) {
  return make_frame({HEAD, channel_group(channel, 0), on ? 0x04 : 0x03, 0x00, on ? power : 0x64, 0xFF, 0xFF, 0xFF, 0x00,
                     0x00, 0x00, 0x11, 0x22, TAIL},
                    power);
}

static Frame encode_cct(uint8_t cg, uint8_t power, uint8_t cct, uint8_t cct_type, uint8_t rg) {
  return make_frame({HEAD, cg, 0x00, cct, power, cct_type, 0x00, rg, 0x00, 0x00, 0x00, 0x00, 0x02, TAIL}, power);
}

static Frame encode_hsi(uint8_t cg, uint8_t power, uint16_t hue, uint8_t sat) {
  return make_frame({HEAD, cg, 0x01, 0x00, power, 0x00, 0x00, 0x00, hue >> 8, hue & 0xFF, sat, 0x01, 0x02, TAIL},
                    power);
}

// `speed` is the wire value 1-3 (the app sends its 0-2 slider position + 1).
static Frame encode_scene(uint8_t cg, uint8_t power, uint8_t scene_id, uint8_t speed) {
  return make_frame({HEAD, cg, 0x02, scene_id, power, speed, scene_id, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, TAIL},
                    power);
}

static Frame encode_gel(uint8_t cg, uint8_t power, uint8_t cct, const Gel &gel) {
  return make_frame({HEAD, cg, 0x01, cct, power, 0x00, gel.brand, gel.index, gel.hue >> 8, gel.hue & 0xFF, gel.saturation,
                     0x07, 0x02, TAIL},
                    power);
}

light::LightTraits ViltroxLight::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::RGB, light::ColorMode::COLOR_TEMPERATURE});
  traits.set_min_mireds(this->cold_white_temperature_);
  traits.set_max_mireds(this->warm_white_temperature_);
  return traits;
}

bool ViltroxLight::next_frame(Frame &frame) {
  if (this->state_ == nullptr) {
    return false;
  }
  const auto &values = this->state_->remote_values;
  const bool on = values.is_on();
  const auto power = static_cast<uint8_t>(std::clamp<long>(std::lround(values.get_brightness() * 100.0f), 0, 100));

  if (this->sent_on_ != on) {
    frame = encode_power(on, this->channel_, power);
    this->sent_on_ = on;
    // Re-send the lighting command after every power change rather than
    // relying on the panel to restore it.
    this->sent_lighting_.reset();
    return true;
  }
  if (!on) {
    return false;
  }

  const Frame lighting = this->lighting_frame_(power);
  if (this->sent_lighting_ == lighting) {
    return false;
  }
  frame = lighting;
  this->sent_lighting_ = lighting;
  return true;
}

Frame ViltroxLight::lighting_frame_(uint8_t power) {
  const auto &values = this->state_->remote_values;
  const uint8_t cg = channel_group(this->channel_, this->group_);

  const uint32_t effect_index = this->state_->get_current_effect_index();
  if (effect_index != 0) {
    auto *scene = ViltroxSceneEffect::find(this->state_->get_effects()[effect_index - 1]);
    if (scene != nullptr) {
      uint8_t speed = scene->get_speed();
      if (speed == 0) {
        speed = this->speed_select_ != nullptr ? this->speed_select_->get_speed() : 2;
      }
      return encode_scene(cg, power, scene->get_scene_id(), speed);
    }
  }

  // The panel takes Kelvin / 100 (e.g. 55 for 5500 K).
  const auto cct = static_cast<uint8_t>(std::clamp<long>(std::lround(values.get_color_temperature_kelvin() / 100.0f), 0, 255));

  if (this->gel_select_ != nullptr) {
    const Gel *gel = this->gel_select_->get_gel();
    if (gel != nullptr) {
      // The app sends its current color temperature with a gel, too.
      return encode_gel(cg, power, std::clamp<uint8_t>(cct, 25, 85), *gel);
    }
  }

  if (values.get_color_mode() == light::ColorMode::COLOR_TEMPERATURE) {
    const int8_t tint = this->tint_number_ != nullptr ? this->tint_number_->get_tint() : this->tint_;
    return encode_cct(cg, power, cct, this->cct_type_, tint + 10);
  }

  int hue;
  float saturation, value;
  rgb_to_hsv(values.get_red(), values.get_green(), values.get_blue(), hue, saturation, value);
  const auto sat = static_cast<uint8_t>(std::clamp<long>(std::lround(saturation * 100.0f), 0, 100));
  return encode_hsi(cg, power, hue % 360, sat);
}

void ViltroxResendButton::press_action() { this->light_->resend(); }

void ViltroxGelSelect::control(size_t index) {
  this->publish_state(index);
  if (index == 0 || this->state_ == nullptr) {
    return;
  }
  const auto &values = this->state_->remote_values;
  this->color_mode_ = values.get_color_mode();
  this->red_ = values.get_red();
  this->green_ = values.get_green();
  this->blue_ = values.get_blue();
  this->color_temperature_ = values.get_color_temperature();
  // Turn on and stop any scene. The color stays the same, so this call
  // doesn't clear the gel we just picked.
  const uint32_t no_effect = 0;
  this->state_->make_call().set_state(true).set_effect(no_effect).perform();
}

bool ViltroxGelSelect::color_changed_() const {
  const auto &values = this->state_->remote_values;
  return values.get_color_mode() != this->color_mode_ || values.get_red() != this->red_ ||
         values.get_green() != this->green_ || values.get_blue() != this->blue_ ||
         values.get_color_temperature() != this->color_temperature_;
}

void ViltroxGelSelect::on_light_remote_values_update() {
  if (this->get_gel() == nullptr) {
    return;
  }
  if (this->state_->get_current_effect_index() != 0 || this->color_changed_()) {
    this->publish_state(static_cast<size_t>(0));
  }
}

ViltroxSceneEffect::ViltroxSceneEffect(const char *name) : LightEffect(name) { instances_().push_back(this); }

std::vector<ViltroxSceneEffect *> &ViltroxSceneEffect::instances_() {
  static std::vector<ViltroxSceneEffect *> instances;
  return instances;
}

ViltroxSceneEffect *ViltroxSceneEffect::find(light::LightEffect *effect) {
  for (auto *scene : instances_()) {
    if (scene == effect) {
      return scene;
    }
  }
  return nullptr;
}

}  // namespace esphome::viltrox

#endif  // USE_ESP32
