#pragma once

#ifdef USE_ESP32

#include <cmath>
#include <vector>

#include "esphome/components/button/button.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

// Extra entities a viltrox light can expose. The light reads them when it
// builds its next command, so a change goes out on the hub's next poll.

namespace esphome::viltrox {

class ViltroxLight;

/// Speed (Slow/Medium/Fast) for scene effects that don't set their own.
class ViltroxSpeedSelect : public select::Select, public Component {
 public:
  void setup() override {
    this->pref_ = this->make_entity_preference<uint32_t>();
    uint32_t index = DEFAULT_INDEX;
    if (!this->pref_.load(&index) || !this->has_index(index)) {
      index = DEFAULT_INDEX;
    }
    this->publish_state(static_cast<size_t>(index));
  }

  /// Wire value for the scene speed byte: 1-3.
  uint8_t get_speed() const { return this->active_index().value_or(DEFAULT_INDEX) + 1; }

 protected:
  static constexpr uint32_t DEFAULT_INDEX = 1;  // Medium

  void control(size_t index) override {
    this->publish_state(index);
    uint32_t stored = index;
    this->pref_.save(&stored);
  }

  ESPPreferenceObject pref_;
};

/// Red/green tint (-10 to 10) for color temperature commands.
class ViltroxTintNumber : public number::Number, public Component {
 public:
  void setup() override {
    this->pref_ = this->make_entity_preference<float>();
    float value = this->initial_value_;
    if (!this->pref_.load(&value)) {
      value = this->initial_value_;
    }
    this->publish_state(value);
  }

  void set_initial_value(float initial_value) { this->initial_value_ = initial_value; }
  int8_t get_tint() const { return std::isnan(this->state) ? this->initial_value_ : std::lround(this->state); }

 protected:
  void control(float value) override {
    this->publish_state(value);
    this->pref_.save(&value);
  }

  float initial_value_{0};
  ESPPreferenceObject pref_;
};

/// One of the app's color chips (Rosco or Lee gels).
struct Gel {
  uint8_t brand;  // 0 = Rosco, 1 = Lee
  uint8_t index;  // position in the app's list for that brand
  uint16_t hue;   // the app sends the gel's approximate color along with it
  uint8_t saturation;
};

/// Gel preset for a light. Option 0 is "None"; option i is gels_[i - 1].
///
/// Picking a gel turns the light on and stops any scene. Changing the light's
/// color or starting a scene afterwards sets this back to None; brightness
/// changes keep the gel. Not restored across reboots.
class ViltroxGelSelect : public select::Select, public Component, public light::LightRemoteValuesListener {
 public:
  void setup() override { this->publish_state(static_cast<size_t>(0)); }

  void add_gel(uint8_t brand, uint8_t index, uint16_t hue, uint8_t saturation) {
    this->gels_.push_back({brand, index, hue, saturation});
  }
  void set_light_state(light::LightState *state) {
    this->state_ = state;
    state->add_remote_values_listener(this);
  }

  /// The selected gel, or nullptr for None.
  const Gel *get_gel() const {
    const size_t index = this->active_index().value_or(0);
    return index == 0 || index > this->gels_.size() ? nullptr : &this->gels_[index - 1];
  }

  void on_light_remote_values_update() override;

 protected:
  void control(size_t index) override;
  bool color_changed_() const;

  std::vector<Gel> gels_;
  light::LightState *state_{nullptr};
  // The light's color when the gel was picked; a different one clears the gel.
  light::ColorMode color_mode_{};
  float red_{}, green_{}, blue_{}, color_temperature_{};
};

/// Sends the light's current state again, e.g. after the panel was changed with
/// its own buttons or the app, which the light can't see.
class ViltroxResendButton : public button::Button {
 public:
  explicit ViltroxResendButton(ViltroxLight *light) : light_(light) {}

 protected:
  void press_action() override;

  ViltroxLight *light_;
};

}  // namespace esphome::viltrox

#endif  // USE_ESP32
