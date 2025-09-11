#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "../ledbrick_scheduler/ledbrick_scheduler.h"
#include "esphome/components/sensor/sensor.h"

#ifdef USE_ESP_IDF
#include <esp_http_server.h>
#include "esphome/components/json/json_util.h"
#include <map>
#include <string>

namespace esphome {
namespace ledbrick_web_server {

class LEDBrickWebServer : public Component {
 public:
  LEDBrickWebServer(ledbrick_scheduler::LEDBrickScheduler *scheduler) : scheduler_(scheduler) {}
  
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::WIFI; }
  
  void set_port(uint16_t port) { this->port_ = port; }
  void set_username(const std::string &username) { this->username_ = username; }
  void set_password(const std::string &password) { this->password_ = password; }
  
  // Sensor setters for INA280
  void set_voltage_sensor(sensor::Sensor *sensor) { this->voltage_sensor_ = sensor; }
  void set_current_sensor(sensor::Sensor *sensor) { this->current_sensor_ = sensor; }
  
  // Fan sensor setters
  void set_fan_speed_sensor(sensor::Sensor *sensor) { this->fan_speed_sensor_ = sensor; }
  void set_fan_state_sensor(sensor::Sensor *sensor) { this->fan_state_sensor_ = sensor; }
  
  // Temperature sensor setters (supports multiple sensors)
  void add_temperature_sensor(sensor::Sensor *sensor, const std::string &name = "") {
    this->temperature_sensors_.push_back({sensor, name});
  }
  
 protected:
  ledbrick_scheduler::LEDBrickScheduler *scheduler_;
  httpd_handle_t server_{nullptr};
  uint16_t port_{80};
  std::string username_;
  std::string password_;
  
  // Request handlers
  static esp_err_t handle_index(httpd_req_t *req);
  static esp_err_t handle_js(httpd_req_t *req);
  static esp_err_t handle_css(httpd_req_t *req);
  static esp_err_t handle_api_schedule_get(httpd_req_t *req);
  static esp_err_t handle_api_schedule_post(httpd_req_t *req);
  static esp_err_t handle_api_presets_get(httpd_req_t *req);
  static esp_err_t handle_api_preset_load(httpd_req_t *req);
  static esp_err_t handle_api_status_get(httpd_req_t *req);
  static esp_err_t handle_api_clear(httpd_req_t *req);
  static esp_err_t handle_api_point_post(httpd_req_t *req);
  static esp_err_t handle_api_location_post(httpd_req_t *req);
  static esp_err_t handle_api_time_shift_post(httpd_req_t *req);
  static esp_err_t handle_api_moon_simulation_post(httpd_req_t *req);
  static esp_err_t handle_scheduler_enable(httpd_req_t *req);
  static esp_err_t handle_scheduler_disable(httpd_req_t *req);
  static esp_err_t handle_scheduler_state(httpd_req_t *req);
  static esp_err_t handle_pwm_scale_set(httpd_req_t *req);
  static esp_err_t handle_pwm_scale_get(httpd_req_t *req);
  static esp_err_t handle_api_schedule_debug(httpd_req_t *req);
  static esp_err_t handle_api_timezone_get(httpd_req_t *req);
  static esp_err_t handle_api_timezone_post(httpd_req_t *req);
  static esp_err_t handle_api_channel_control(httpd_req_t *req);
  static esp_err_t handle_api_channel_configs(httpd_req_t *req);
  static esp_err_t handle_api_temperature_config_get(httpd_req_t *req);
  static esp_err_t handle_api_temperature_config_post(httpd_req_t *req);
  static esp_err_t handle_api_temperature_status_get(httpd_req_t *req);
  static esp_err_t handle_api_fan_curve_get(httpd_req_t *req);
  static esp_err_t handle_not_found(httpd_req_t *req);
  
  // Helper methods
  static LEDBrickWebServer *get_instance(httpd_req_t *req);
  static std::unique_ptr<char[]> read_request_body(httpd_req_t *req);
  bool check_auth(httpd_req_t *req);
  void send_json_response(httpd_req_t *req, int status, const JsonDocument &doc);
  void send_error(httpd_req_t *req, int status, const std::string &message);
  void send_compressed_content(httpd_req_t *req, const uint8_t *compressed_data, 
                              size_t compressed_size, const char *content_type);
  
  // Static instance for callbacks
  static LEDBrickWebServer *instance_;
  
  // INA280 sensor references
  sensor::Sensor *voltage_sensor_{nullptr};
  sensor::Sensor *current_sensor_{nullptr};
  
  // Fan sensor references
  sensor::Sensor *fan_speed_sensor_{nullptr};
  sensor::Sensor *fan_state_sensor_{nullptr};
  
  // Temperature sensor references (can have multiple)
  struct TemperatureSensorInfo {
    sensor::Sensor *sensor;
    std::string name;
  };
  std::vector<TemperatureSensorInfo> temperature_sensors_;
};

}  // namespace ledbrick_web_server
}  // namespace esphome

// Include web content after namespace (it has its own namespace declaration)
#include "web_content.h"

#endif  // USE_ESP_IDF