#pragma once

#ifdef USE_ESP32

#include <array>
#include <vector>

#ifndef CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
#include <esp_bt.h>
#endif
#include <esp_gap_ble_api.h>

#include "esphome/core/component.h"

namespace esphome::viltrox {

/// One 16-byte WeeyliteII command, broadcast as the iBeacon proximity UUID.
using Frame = std::array<uint8_t, 16>;

/// Byte of a Frame holding the rolling packet ID; the hub fills it in.
static constexpr size_t FRAME_ID_INDEX = 14;

class ViltroxLight;

/// Owns the BLE radio and broadcasts one command at a time.
///
/// Lights don't push commands. Every command_interval the hub asks each light
/// (round robin) for the next frame it needs sent, so bursts of changes such as
/// a dragged slider collapse into the latest state, and panels never see
/// commands faster than they accept them. The last frame stays on air until the
/// next one, like the WeeyliteII app.
class ViltroxComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

  void register_light(ViltroxLight *light) { this->lights_.push_back(light); }

  void set_major(uint16_t major) { this->major_ = major; }
  void set_minor(uint16_t minor) { this->minor_ = minor; }
  void set_measured_power(int8_t measured_power) { this->measured_power_ = measured_power; }
  void set_command_interval(uint32_t command_interval) { this->command_interval_ = command_interval; }
#ifndef CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
  void set_tx_power(esp_power_level_t tx_power) {
    this->tx_power_ = tx_power;
    this->has_tx_power_ = true;
  }
#endif

 protected:
  void on_ble_ready_();
  void send_adv_data_();

  std::vector<ViltroxLight *> lights_;
  size_t next_light_{0};

  uint16_t major_{};
  uint16_t minor_{};
  int8_t measured_power_{};
  uint32_t command_interval_{};
#ifndef CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
  esp_power_level_t tx_power_{};
  bool has_tx_power_{false};
#endif

  esp_ble_adv_params_t adv_params_{};
  uint8_t adv_data_[30]{};
  Frame frame_{};  // on air now
  bool has_frame_{false};
  uint8_t packet_id_{0};
  uint32_t last_send_{0};

  bool ble_ready_{false};    // radio set up since the BLE stack last came up
  bool advertising_{false};  // advertising started (or being started)
};

}  // namespace esphome::viltrox

#endif  // USE_ESP32
