#include "viltrox.h"

#ifdef USE_ESP32

#include <cstring>

#include "esphome/components/esp32_ble/ble.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "viltrox_light.h"

namespace esphome::viltrox {

static const char *const TAG = "viltrox";

// Packet IDs roll over 0-222, like the WeeyliteII app.
static constexpr uint8_t PACKET_ID_COUNT = 223;

void ViltroxComponent::setup() {
  this->adv_params_ = {
      .adv_int_min = 0x00A0,  // 100 ms, the app's advertising interval
      .adv_int_max = 0x00A0,
      .adv_type = ADV_TYPE_NONCONN_IND,
      .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
      .peer_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
      .channel_map = ADV_CHNL_ALL,
      .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
  };
  // Start somewhere random so a panel that saw our last ID before a reboot
  // doesn't mistake the first new command for a repeat.
  this->packet_id_ = random_uint32() % PACKET_ID_COUNT;
}

float ViltroxComponent::get_setup_priority() const { return setup_priority::AFTER_BLUETOOTH; }

void ViltroxComponent::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Viltrox:\n"
                "  Major: %u, Minor: %u, Measured Power: %d\n"
                "  Command Interval: %" PRIu32 "ms",
                this->major_, this->minor_, this->measured_power_, this->command_interval_);
#ifndef CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
  if (this->has_tx_power_) {
    ESP_LOGCONFIG(TAG, "  TX Power: %ddBm", (this->tx_power_ * 3) - 12);
  }
#endif
  for (auto *light : this->lights_) {
    ESP_LOGCONFIG(TAG, "  Light: channel %u, group %c", light->get_channel(), 'A' + light->get_group() - 1);
  }
}

void ViltroxComponent::loop() {
  if (!esp32_ble::global_ble->is_active()) {
    // Advertising dies with the stack; set everything up again once it's back.
    this->ble_ready_ = false;
    this->advertising_ = false;
    return;
  }
  if (!this->ble_ready_) {
    this->on_ble_ready_();
  }

  const uint32_t now = App.get_loop_component_start_time();
  if (this->has_frame_ && now - this->last_send_ < this->command_interval_) {
    return;
  }

  const size_t count = this->lights_.size();
  for (size_t i = 0; i < count; i++) {
    const size_t index = (this->next_light_ + i) % count;
    Frame frame;
    if (!this->lights_[index]->next_frame(frame)) {
      continue;
    }
    this->next_light_ = (index + 1) % count;

    frame[FRAME_ID_INDEX] = this->packet_id_;
    this->packet_id_ = (this->packet_id_ + 1) % PACKET_ID_COUNT;
    this->frame_ = frame;
    this->has_frame_ = true;
    this->last_send_ = now;

    char hex[format_hex_size(sizeof(Frame))];
    ESP_LOGD(TAG, "Sending %s", format_hex_to(hex, frame.data(), frame.size()));
    this->send_adv_data_();
    return;
  }
}

void ViltroxComponent::on_ble_ready_() {
  this->ble_ready_ = true;
#ifndef CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
  if (this->has_tx_power_) {
    esp_err_t err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, this->tx_power_);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "esp_ble_tx_power_set failed: %s", esp_err_to_name(err));
    }
  }
#endif
  if (this->has_frame_) {
    this->send_adv_data_();  // resume broadcasting after the stack restarts
  }
}

void ViltroxComponent::send_adv_data_() {
  uint8_t *p = this->adv_data_;
  *p++ = 0x02;  // flags: LE general discoverable, no BR/EDR
  *p++ = 0x01;
  *p++ = 0x06;
  *p++ = 0x1A;  // manufacturer data, 26 bytes
  *p++ = 0xFF;
  *p++ = 0x4C;  // Apple
  *p++ = 0x00;
  *p++ = 0x02;  // iBeacon type + length
  *p++ = 0x15;
  memcpy(p, this->frame_.data(), this->frame_.size());
  p += this->frame_.size();
  *p++ = this->major_ >> 8;
  *p++ = this->major_ & 0xFF;
  *p++ = this->minor_ >> 8;
  *p++ = this->minor_ & 0xFF;
  *p++ = static_cast<uint8_t>(this->measured_power_);

  // Legacy advertising data may be replaced while advertising is running, so
  // after the first command this just swaps the payload in place.
  esp_err_t err = esp_ble_gap_config_adv_data_raw(this->adv_data_, sizeof(this->adv_data_));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ble_gap_config_adv_data_raw failed: %s", esp_err_to_name(err));
  }
}

void ViltroxComponent::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT: {
      if (param->adv_data_raw_cmpl.status != ESP_BT_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Setting advertising data failed: %d", param->adv_data_raw_cmpl.status);
        break;
      }
      if (this->advertising_ || !this->has_frame_) {
        break;
      }
      this->advertising_ = true;
      esp_err_t err = esp_ble_gap_start_advertising(&this->adv_params_);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_gap_start_advertising failed: %s", esp_err_to_name(err));
        this->advertising_ = false;
      }
      break;
    }
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT: {
      if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Starting advertising failed: %d", param->adv_start_cmpl.status);
        this->advertising_ = false;
      }
      break;
    }
    default:
      break;
  }
}

}  // namespace esphome::viltrox

#endif  // USE_ESP32
