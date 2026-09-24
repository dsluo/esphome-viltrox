#pragma once

#ifdef USE_ESP32

#include <optional>
#include <vector>

#include "esphome/components/light/light_effect.h"
#include "esphome/components/light/light_output.h"
#include "viltrox.h"
#include "controls.h"

namespace esphome::viltrox {

/// A panel group as a Home Assistant light.
///
/// Commands follow the light's target values (remote_values), not the values
/// mid-transition: panels can't be updated fast enough to animate one, so only
/// the end state is sent.
class ViltroxLight : public light::LightOutput {
 public:
  light::LightTraits get_traits() override;
  void setup_state(light::LightState *state) override {
    this->state_ = state;
    if (this->gel_select_ != nullptr) {
      this->gel_select_->set_light_state(state);
    }
  }
  // Nothing to do here: the hub polls next_frame() on its own schedule.
  void write_state(light::LightState *state) override {}

  /// Fills `frame` (packet ID left 0) with the next command needed to bring the
  /// panel to the light's target state. Returns false if the panel is in sync.
  bool next_frame(Frame &frame);

  void set_channel(uint8_t channel) { this->channel_ = channel; }
  void set_group(uint8_t group) { this->group_ = group; }
  void set_cold_white_temperature(float mireds) { this->cold_white_temperature_ = mireds; }
  void set_warm_white_temperature(float mireds) { this->warm_white_temperature_ = mireds; }
  void set_cct_type(uint8_t cct_type) { this->cct_type_ = cct_type; }
  void set_tint(int8_t tint) { this->tint_ = tint; }
  void set_speed_select(ViltroxSpeedSelect *speed_select) { this->speed_select_ = speed_select; }
  void set_tint_number(ViltroxTintNumber *tint_number) { this->tint_number_ = tint_number; }
  void set_gel_select(ViltroxGelSelect *gel_select) { this->gel_select_ = gel_select; }

  /// Forget what the panel was last told, so the full state is sent again.
  void resend() {
    this->sent_on_.reset();
    this->sent_lighting_.reset();
  }

  uint8_t get_channel() const { return this->channel_; }
  uint8_t get_group() const { return this->group_; }

 protected:
  Frame lighting_frame_(uint8_t power);

  light::LightState *state_{nullptr};
  uint8_t channel_{1};
  uint8_t group_{1};  // wire value: 1-6 for A-F
  float cold_white_temperature_{};
  float warm_white_temperature_{};
  uint8_t cct_type_{0};
  int8_t tint_{0};
  ViltroxSpeedSelect *speed_select_{nullptr};
  ViltroxTintNumber *tint_number_{nullptr};
  ViltroxGelSelect *gel_select_{nullptr};

  // What the panel was last told, so only changes are sent.
  std::optional<bool> sent_on_;
  std::optional<Frame> sent_lighting_;
};

/// Runs one of the panel's built-in scenes. The panel animates it by itself, so
/// the effect does nothing locally; ViltroxLight sends the scene command while
/// it is active.
class ViltroxSceneEffect : public light::LightEffect {
 public:
  explicit ViltroxSceneEffect(const char *name);

  void apply() override {}

  void set_scene(uint8_t scene_id, uint8_t speed) {
    this->scene_id_ = scene_id;
    this->speed_ = speed;
  }
  uint8_t get_scene_id() const { return this->scene_id_; }
  /// Wire speed 1-3, or 0 to follow the light's effect speed select.
  uint8_t get_speed() const { return this->speed_; }

  /// Returns `effect` as a scene effect, or nullptr if it's some other effect.
  /// (ESPHome builds without RTTI, so dynamic_cast isn't available.)
  static ViltroxSceneEffect *find(light::LightEffect *effect);

 protected:
  static std::vector<ViltroxSceneEffect *> &instances_();

  uint8_t scene_id_{0};
  uint8_t speed_{0};
};

}  // namespace esphome::viltrox

#endif  // USE_ESP32
