#ifdef USE_ESP_IDF

#include "ledbrick_web_server.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "web_content.h"
#include <esp_log.h>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <cctype>

namespace esphome {
namespace ledbrick_web_server {

static const char *const TAG = "ledbrick_web_server";
static const size_t MAX_REQUEST_SIZE = 32768;  // 32KB max request body

LEDBrickWebServer *LEDBrickWebServer::instance_ = nullptr;

void LEDBrickWebServer::setup() {
  ESP_LOGI(TAG, "Setting up LEDBrick Web Server on port %d", this->port_);
  
  // Store instance for static callbacks
  instance_ = this;
  
  // Configure and start the HTTP server
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = this->port_;
  config.ctrl_port = this->port_;
  config.stack_size = 8192;
  config.max_uri_handlers = 26;  // We register 24 handlers
  config.recv_wait_timeout = 10;
  config.send_wait_timeout = 10;
  
  esp_err_t ret = httpd_start(&this->server_, &config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(ret));
    return;
  }
  
  // Register URI handlers
  
  // Static content
  httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = handle_index,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &index_uri);
  
  // Modern React endpoints
  httpd_uri_t app_js_uri = {
    .uri = "/app.js",
    .method = HTTP_GET,
    .handler = handle_js,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &app_js_uri);
  
  httpd_uri_t app_css_uri = {
    .uri = "/app.css",
    .method = HTTP_GET,
    .handler = handle_css,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &app_css_uri);
  
  // API endpoints
  httpd_uri_t api_schedule_get = {
    .uri = "/api/schedule",
    .method = HTTP_GET,
    .handler = handle_api_schedule_get,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &api_schedule_get);
  
  httpd_uri_t api_schedule_post = {
    .uri = "/api/schedule",
    .method = HTTP_POST,
    .handler = handle_api_schedule_post,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &api_schedule_post);
  
  httpd_uri_t api_presets_get = {
    .uri = "/api/presets",
    .method = HTTP_GET,
    .handler = handle_api_presets_get,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &api_presets_get);
  
  // Preset load handler - we'll handle the path parsing in the handler
  httpd_uri_t api_preset_load = {
    .uri = "/api/presets/*",
    .method = HTTP_POST,
    .handler = handle_api_preset_load,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &api_preset_load);
  
  httpd_uri_t api_status = {
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = handle_api_status_get,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &api_status);
  
  httpd_uri_t api_clear = {
    .uri = "/api/schedule/clear",
    .method = HTTP_POST,
    .handler = handle_api_clear,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &api_clear);
  
  httpd_uri_t api_point = {
    .uri = "/api/schedule/point",
    .method = HTTP_POST,
    .handler = handle_api_point_post,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &api_point);
  
  httpd_uri_t api_location = {
    .uri = "/api/location",
    .method = HTTP_POST,
    .handler = handle_api_location_post,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &api_location);
  
  httpd_uri_t api_time_shift = {
    .uri = "/api/time_shift",
    .method = HTTP_POST,
    .handler = handle_api_time_shift_post,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &api_time_shift);
  
  httpd_uri_t api_moon_simulation = {
    .uri = "/api/moon_simulation",
    .method = HTTP_POST,
    .handler = handle_api_moon_simulation_post,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &api_moon_simulation);
  
  // ESPHome-compatible endpoints for scheduler control
  httpd_uri_t scheduler_enable_on = {
    .uri = "/switch/web_scheduler_enable/turn_on",
    .method = HTTP_POST,
    .handler = handle_scheduler_enable,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &scheduler_enable_on);
  
  httpd_uri_t scheduler_enable_off = {
    .uri = "/switch/web_scheduler_enable/turn_off",
    .method = HTTP_POST,
    .handler = handle_scheduler_disable,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &scheduler_enable_off);
  
  httpd_uri_t scheduler_state = {
    .uri = "/switch/web_scheduler_enable",
    .method = HTTP_GET,
    .handler = handle_scheduler_state,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &scheduler_state);
  
  httpd_uri_t pwm_scale_set = {
    .uri = "/number/pwm_scale/set",
    .method = HTTP_POST,
    .handler = handle_pwm_scale_set,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &pwm_scale_set);
  
  httpd_uri_t pwm_scale_get = {
    .uri = "/number/pwm_scale",
    .method = HTTP_GET,
    .handler = handle_pwm_scale_get,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &pwm_scale_get);
  
  httpd_uri_t api_schedule_debug = {
    .uri = "/api/schedule/debug",
    .method = HTTP_GET,
    .handler = handle_api_schedule_debug,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &api_schedule_debug);
  
  httpd_uri_t api_timezone_get = {
    .uri = "/api/timezone",
    .method = HTTP_GET,
    .handler = handle_api_timezone_get,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &api_timezone_get);
  
  httpd_uri_t api_timezone_post = {
    .uri = "/api/timezone",
    .method = HTTP_POST,
    .handler = handle_api_timezone_post,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &api_timezone_post);
  
  // Manual channel control endpoint
  httpd_uri_t api_channel_control = {
    .uri = "/api/channel/control",
    .method = HTTP_POST,
    .handler = handle_api_channel_control,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &api_channel_control);
  
  // Channel configuration endpoint
  httpd_uri_t api_channel_configs = {
    .uri = "/api/channel/configs",
    .method = HTTP_POST,
    .handler = handle_api_channel_configs,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &api_channel_configs);
  
  // Time projection endpoint (alias for time_shift)
  httpd_uri_t api_time_projection = {
    .uri = "/api/time_projection",
    .method = HTTP_POST,
    .handler = handle_api_time_shift_post,  // Reuse the same handler
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &api_time_projection);
  
  ESP_LOGI(TAG, "LEDBrick Web Server started successfully");
}

void LEDBrickWebServer::loop() {
  // Nothing to do in loop for ESP-IDF HTTP server
}

void LEDBrickWebServer::dump_config() {
  ESP_LOGCONFIG(TAG, "LEDBrick Web Server:");
  ESP_LOGCONFIG(TAG, "  Port: %d", this->port_);
  ESP_LOGCONFIG(TAG, "  Authentication: %s", this->username_.empty() ? "Disabled" : "Enabled");
}

LEDBrickWebServer *LEDBrickWebServer::get_instance(httpd_req_t *req) {
  return static_cast<LEDBrickWebServer *>(req->user_ctx);
}

// Helper to safely read request body with size limits
std::unique_ptr<char[]> LEDBrickWebServer::read_request_body(httpd_req_t *req) {
  // Check content length
  if (req->content_len > MAX_REQUEST_SIZE) {
    auto *self = get_instance(req);
    self->send_error(req, 413, "Request too large");
    return nullptr;
  }
  
  if (req->content_len == 0) {
    auto *self = get_instance(req);
    self->send_error(req, 400, "Empty request body");
    return nullptr;
  }
  
  // Allocate buffer on heap
  std::unique_ptr<char[]> buf(new (std::nothrow) char[req->content_len + 1]);
  if (!buf) {
    auto *self = get_instance(req);
    self->send_error(req, 500, "Out of memory");
    return nullptr;
  }
  
  // Read request data
  int ret = httpd_req_recv(req, buf.get(), req->content_len);
  if (ret <= 0) {
    auto *self = get_instance(req);
    self->send_error(req, 400, "Failed to read request body");
    return nullptr;
  }
  buf[ret] = '\0';
  
  return buf;
}

bool LEDBrickWebServer::check_auth(httpd_req_t *req) {
  if (this->username_.empty() || this->password_.empty()) {
    return true;  // No auth required
  }
  
  // Check for Authorization header
  char auth_header[256] = {0};
  if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) != ESP_OK) {
    // No auth header, send 401
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"LEDBrick\"");
    httpd_resp_send(req, "Authorization required", -1);
    return false;
  }
  
  // Basic auth parsing
  if (strncmp(auth_header, "Basic ", 6) != 0) {
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid authentication");
    return false;
  }
  
  // TODO: Implement base64 decode and credential check
  // For now, we'll accept any auth header
  return true;
}

void LEDBrickWebServer::send_json_response(httpd_req_t *req, int status, const JsonDocument &doc) {
  std::string response;
  serializeJson(doc, response);
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_status(req, std::to_string(status).c_str());
  httpd_resp_send(req, response.c_str(), response.length());
}

void LEDBrickWebServer::send_error(httpd_req_t *req, int status, const std::string &message) {
  JsonDocument doc;
  doc["error"] = message;
  doc["code"] = status;
  send_json_response(req, status, doc);
}

// Helper to send compressed content
void LEDBrickWebServer::send_compressed_content(httpd_req_t *req, const uint8_t *compressed_data, 
                                                size_t compressed_size, const char *content_type) {
  // Set headers
  httpd_resp_set_type(req, content_type);
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=3600");
  
  // Send compressed data directly
  httpd_resp_send(req, (const char *)compressed_data, compressed_size);
}

// Static content handlers
esp_err_t LEDBrickWebServer::handle_index(httpd_req_t *req) {
  auto *self = get_instance(req);
  self->send_compressed_content(req, INDEX_HTML_COMPRESSED, INDEX_HTML_SIZE, INDEX_HTML_TYPE);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_js(httpd_req_t *req) {
  auto *self = get_instance(req);
  self->send_compressed_content(req, LEDBRICK_JS_COMPRESSED, LEDBRICK_JS_SIZE, LEDBRICK_JS_TYPE);
  return ESP_OK;
}


esp_err_t LEDBrickWebServer::handle_css(httpd_req_t *req) {
  auto *self = get_instance(req);
  self->send_compressed_content(req, STYLE_CSS_COMPRESSED, STYLE_CSS_SIZE, STYLE_CSS_TYPE);
  return ESP_OK;
}

// API handlers
esp_err_t LEDBrickWebServer::handle_api_schedule_get(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  std::string json;
  self->scheduler_->export_schedule_json(json);
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), json.length());
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_api_schedule_post(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  // Read request body safely
  auto buf = read_request_body(req);
  if (!buf) return ESP_OK;  // Error already sent
  
  // Import the schedule
  bool success = self->scheduler_->import_schedule_json(buf.get());
  
  if (success) {
    self->scheduler_->save_schedule_to_flash();
    
    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = "Schedule imported successfully";
    doc["points"] = self->scheduler_->get_schedule_size();
    self->send_json_response(req, 200, doc);
  } else {
    self->send_error(req, 400, "Invalid schedule JSON");
  }
  
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_api_presets_get(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  JsonDocument doc;
  JsonArray presets = doc["presets"].to<JsonArray>();
  
  // No presets - we only have one default that's loaded automatically
  
  self->send_json_response(req, 200, doc);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_api_preset_load(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  // Extract preset name from URL
  std::string url(req->uri);
  std::string prefix = "/api/presets/";
  if (url.find(prefix) != 0) {
    self->send_error(req, 400, "Invalid URL");
    return ESP_OK;
  }
  
  std::string preset_name = url.substr(prefix.length());
  
  ESP_LOGI(TAG, "Loading preset: %s", preset_name.c_str());
  self->scheduler_->load_preset(preset_name);
  self->scheduler_->save_schedule_to_flash();
  
  JsonDocument doc;
  doc["success"] = true;
  doc["preset"] = preset_name;
  doc["message"] = "Preset loaded successfully";
  
  self->send_json_response(req, 200, doc);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_api_status_get(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  JsonDocument doc;
  doc["enabled"] = self->scheduler_->is_enabled();
  doc["time_minutes"] = self->scheduler_->get_current_time_minutes();
  doc["schedule_points"] = self->scheduler_->get_schedule_size();
  doc["pwm_scale"] = self->scheduler_->get_pwm_scale() * 100.0f;  // Convert to percentage
  doc["latitude"] = self->scheduler_->get_latitude();
  doc["longitude"] = self->scheduler_->get_longitude();
  doc["astronomical_projection"] = self->scheduler_->is_astronomical_projection_enabled();
  doc["time_shift_hours"] = self->scheduler_->get_time_shift_hours();
  doc["time_shift_minutes"] = self->scheduler_->get_time_shift_minutes();
  
  // Add moon simulation data
  auto moon_config = self->scheduler_->get_moon_simulation();
  JsonObject moon_obj = doc["moon_simulation"].to<JsonObject>();
  moon_obj["enabled"] = moon_config.enabled;
  moon_obj["phase_scaling"] = moon_config.phase_scaling;
  JsonArray moon_intensity = moon_obj["base_intensity"].to<JsonArray>();
  for (float intensity : moon_config.base_intensity) {
    moon_intensity.add(intensity);
  }
  JsonArray moon_current = moon_obj["base_current"].to<JsonArray>();
  for (float current : moon_config.base_current) {
    moon_current.add(current);
  }
  
  // Add current channel values (actual values from ESPHome components)
  JsonArray channels = doc["channels"].to<JsonArray>();
  auto values = self->scheduler_->get_actual_channel_values();
  for (size_t i = 0; i < values.pwm_values.size() && i < 8; i++) {
    JsonObject channel = channels.add<JsonObject>();
    channel["id"] = i + 1;
    channel["pwm"] = values.pwm_values[i];
    channel["current"] = values.current_values[i];
  }
  
  // Add time formatted
  int time_min = self->scheduler_->get_current_time_minutes();
  char time_str[6];
  snprintf(time_str, sizeof(time_str), "%02d:%02d", time_min / 60, time_min % 60);
  doc["time_formatted"] = time_str;
  
  self->send_json_response(req, 200, doc);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_api_clear(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  self->scheduler_->clear_schedule();
  self->scheduler_->save_schedule_to_flash();
  
  JsonDocument doc;
  doc["success"] = true;
  doc["message"] = "Schedule cleared";
  
  self->send_json_response(req, 200, doc);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_api_point_post(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  // Read request body safely
  auto buf = read_request_body(req);
  if (!buf) return ESP_OK;  // Error already sent
  
  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, buf.get());
  
  if (error) {
    self->send_error(req, 400, "Invalid JSON");
    return ESP_OK;
  }
  
  // Extract schedule point data
  int time_minutes = doc["time_minutes"] | -1;
  JsonArray pwm_array = doc["pwm_values"];
  JsonArray current_array = doc["current_values"];
  
  if (time_minutes < 0 || time_minutes > 1439) {
    self->send_error(req, 400, "Invalid time_minutes (0-1439)");
    return ESP_OK;
  }
  
  std::vector<float> pwm_values(8, 0.0);
  std::vector<float> current_values(8, 2.0);  // Default to 2A
  
  // Parse PWM values
  size_t i = 0;
  for (JsonVariant v : pwm_array) {
    if (i < 8) pwm_values[i++] = v.as<float>();
  }
  
  // Parse current values if provided
  if (!current_array.isNull()) {
    i = 0;
    for (JsonVariant v : current_array) {
      if (i < 8) {
        float current = v.as<float>();
        // Validate current limits (0.5A to 5A)
        if (current < 0.5f) current = 0.5f;
        if (current > 5.0f) current = 5.0f;
        current_values[i++] = current;
      }
    }
  }
  
  // Add the schedule point
  self->scheduler_->set_schedule_point(time_minutes, pwm_values, current_values);
  self->scheduler_->save_schedule_to_flash();
  
  JsonDocument response_doc;
  response_doc["success"] = true;
  response_doc["time_minutes"] = time_minutes;
  response_doc["message"] = "Schedule point added";
  
  self->send_json_response(req, 200, response_doc);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_scheduler_enable(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  self->scheduler_->set_enabled(true);
  self->scheduler_->save_schedule_to_flash();
  
  JsonDocument doc;
  doc["id"] = "web_scheduler_enable";
  doc["state"] = "ON";
  self->send_json_response(req, 200, doc);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_scheduler_disable(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  self->scheduler_->set_enabled(false);
  self->scheduler_->save_schedule_to_flash();
  
  JsonDocument doc;
  doc["id"] = "web_scheduler_enable";
  doc["state"] = "OFF";
  self->send_json_response(req, 200, doc);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_scheduler_state(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  JsonDocument doc;
  doc["id"] = "web_scheduler_enable";
  doc["state"] = self->scheduler_->is_enabled() ? "ON" : "OFF";
  self->send_json_response(req, 200, doc);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_pwm_scale_set(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  // Read form data
  char buf[100];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    self->send_error(req, 400, "No data received");
    return ESP_OK;
  }
  buf[ret] = '\0';
  
  // Parse value from form data (value=XX)
  float value = 0;
  if (httpd_query_key_value(buf, "value", buf, sizeof(buf)) == ESP_OK) {
    value = atof(buf);
    value = value / 100.0f;  // Convert from percentage
    self->scheduler_->set_pwm_scale(value);
    httpd_resp_send(req, "OK", -1);
  } else {
    self->send_error(req, 400, "Invalid data");
  }
  
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_pwm_scale_get(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  JsonDocument doc;
  doc["id"] = "pwm_scale";
  doc["value"] = self->scheduler_->get_pwm_scale() * 100.0f;  // Convert to percentage
  doc["min"] = 0;
  doc["max"] = 100;
  doc["step"] = 1;
  self->send_json_response(req, 200, doc);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_api_location_post(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  // Read request body safely
  auto buf = read_request_body(req);
  if (!buf) return ESP_OK;  // Error already sent
  
  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, buf.get());
  
  if (error) {
    self->send_error(req, 400, "Invalid JSON");
    return ESP_OK;
  }
  
  // Extract latitude and longitude
  if (!doc["latitude"].is<JsonVariant>() || !doc["longitude"].is<JsonVariant>()) {
    self->send_error(req, 400, "Missing latitude or longitude");
    return ESP_OK;
  }
  
  double latitude = doc["latitude"];
  double longitude = doc["longitude"];
  
  // Validate ranges
  if (latitude < -90.0 || latitude > 90.0) {
    self->send_error(req, 400, "Latitude must be between -90 and 90");
    return ESP_OK;
  }
  
  if (longitude < -180.0 || longitude > 180.0) {
    self->send_error(req, 400, "Longitude must be between -180 and 180");
    return ESP_OK;
  }
  
  // Check if timezone_offset_hours is provided
  if (doc["timezone_offset_hours"].is<JsonVariant>()) {
    double timezone_offset = doc["timezone_offset_hours"];
    self->scheduler_->set_timezone_offset_hours(timezone_offset);
    ESP_LOGI(TAG, "Updated timezone offset to %.1f hours", timezone_offset);
  }
  
  // Update the scheduler location (auto-saves)
  self->scheduler_->set_location(latitude, longitude);
  
  ESP_LOGI(TAG, "Updated location to %.4f, %.4f", latitude, longitude);
  
  JsonDocument response_doc;
  response_doc["success"] = true;
  response_doc["latitude"] = latitude;
  response_doc["longitude"] = longitude;
  response_doc["message"] = "Location updated";
  
  self->send_json_response(req, 200, response_doc);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_api_time_shift_post(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  // Read request body safely
  auto buf = read_request_body(req);
  if (!buf) return ESP_OK;  // Error already sent
  
  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, buf.get());
  
  if (error) {
    self->send_error(req, 400, "Invalid JSON");
    return ESP_OK;
  }
  
  // Extract time shift settings
  bool projection_enabled = doc["astronomical_projection"] | false;
  int time_shift_hours = doc["time_shift_hours"] | 0;
  int time_shift_minutes = doc["time_shift_minutes"] | 0;
  
  // Validate ranges
  if (time_shift_hours < -12 || time_shift_hours > 12) {
    self->send_error(req, 400, "Time shift hours must be between -12 and 12");
    return ESP_OK;
  }
  
  if (time_shift_minutes < -59 || time_shift_minutes > 59) {
    self->send_error(req, 400, "Time shift minutes must be between -59 and 59");
    return ESP_OK;
  }
  
  // Update the scheduler settings (auto-saves)
  self->scheduler_->set_astronomical_projection(projection_enabled);
  self->scheduler_->set_time_shift(time_shift_hours, time_shift_minutes);
  
  ESP_LOGI(TAG, "Updated time shift: projection=%s, shift=%+d:%02d", 
           projection_enabled ? "enabled" : "disabled",
           time_shift_hours, abs(time_shift_minutes));
  
  JsonDocument response_doc;
  response_doc["success"] = true;
  response_doc["astronomical_projection"] = projection_enabled;
  response_doc["time_shift_hours"] = time_shift_hours;
  response_doc["time_shift_minutes"] = time_shift_minutes;
  response_doc["message"] = "Time shift settings updated";
  
  self->send_json_response(req, 200, response_doc);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_api_moon_simulation_post(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  // Read request body safely
  auto buf = read_request_body(req);
  if (!buf) return ESP_OK;  // Error already sent
  
  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, buf.get());
  
  if (error) {
    self->send_error(req, 400, "Invalid JSON");
    return ESP_OK;
  }
  
  // Extract moon simulation settings
  bool enabled = doc["enabled"] | false;
  bool phase_scaling = doc["phase_scaling"] | true;
  
  // Extract base_intensity array
  std::vector<float> base_intensity(8, 0.0f);
  if (doc["base_intensity"].is<JsonArray>()) {
    JsonArray intensity_array = doc["base_intensity"];
    size_t i = 0;
    for (JsonVariant v : intensity_array) {
      if (i < 8) {
        float intensity = v.as<float>();
        // Validate intensity range (0-100%)
        if (intensity < 0.0f) intensity = 0.0f;
        if (intensity > 100.0f) intensity = 100.0f;
        base_intensity[i++] = intensity;
      }
    }
  }
  
  // Extract base_current array
  std::vector<float> base_current(8, 0.0f);
  if (doc["base_current"].is<JsonArray>()) {
    JsonArray current_array = doc["base_current"];
    size_t i = 0;
    for (JsonVariant v : current_array) {
      if (i < 8) {
        float current = v.as<float>();
        // Validate current range (0-2.0A)
        if (current < 0.0f) current = 0.0f;
        if (current > 2.0f) current = 2.0f;
        base_current[i++] = current;
      }
    }
  }
  
  // Create moon simulation configuration
  LEDScheduler::MoonSimulation moon_config;
  moon_config.enabled = enabled;
  moon_config.phase_scaling = phase_scaling;
  moon_config.base_intensity = base_intensity;
  moon_config.base_current = base_current;
  
  // Update the scheduler settings (auto-saves)
  self->scheduler_->set_moon_simulation(moon_config);
  
  ESP_LOGI(TAG, "Updated moon simulation: enabled=%s, phase_scaling=%s", 
           enabled ? "true" : "false",
           phase_scaling ? "true" : "false");
  
  JsonDocument response_doc;
  response_doc["success"] = true;
  response_doc["enabled"] = enabled;
  response_doc["phase_scaling"] = phase_scaling;
  
  // Add base_intensity to response
  JsonArray intensity_array = response_doc["base_intensity"].to<JsonArray>();
  for (float intensity : base_intensity) {
    intensity_array.add(intensity);
  }
  
  // Add base_current to response
  JsonArray current_array = response_doc["base_current"].to<JsonArray>();
  for (float current : base_current) {
    current_array.add(current);
  }
  
  response_doc["message"] = "Moon simulation settings updated";
  
  self->send_json_response(req, 200, response_doc);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_api_schedule_debug(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  JsonDocument doc;
  
  // Get current astronomical times
  auto astro_times = self->scheduler_->get_astronomical_times();
  JsonObject astro = doc["astronomical_times"].to<JsonObject>();
  astro["sunrise_minutes"] = astro_times.rise_minutes;
  astro["sunset_minutes"] = astro_times.set_minutes;
  
  char sunrise_buf[6];
  char sunset_buf[6];
  sprintf(sunrise_buf, "%02d:%02d", astro_times.rise_minutes / 60, astro_times.rise_minutes % 60);
  sprintf(sunset_buf, "%02d:%02d", astro_times.set_minutes / 60, astro_times.set_minutes % 60);
  astro["sunrise_formatted"] = sunrise_buf;
  astro["sunset_formatted"] = sunset_buf;
  
  // Get projected astronomical times if enabled
  if (self->scheduler_->is_astronomical_projection_enabled()) {
    auto projected_times = self->scheduler_->get_projected_astronomical_times();
    JsonObject proj = doc["projected_astronomical_times"].to<JsonObject>();
    proj["sunrise_minutes"] = projected_times.rise_minutes;
    proj["sunset_minutes"] = projected_times.set_minutes;
    proj["time_shift_hours"] = self->scheduler_->get_time_shift_hours();
    proj["time_shift_minutes"] = self->scheduler_->get_time_shift_minutes();
  }
  
  // Get the full schedule with resolved dynamic points
  std::string schedule_json;
  self->scheduler_->export_schedule_json(schedule_json);
  
  // Parse the exported JSON and add to our document
  JsonDocument schedule_doc;
  deserializeJson(schedule_doc, schedule_json);
  doc["resolved_schedule"] = schedule_doc;
  
  // Add debug information for each schedule point
  JsonArray debug_points = doc["debug_points"].to<JsonArray>();
  auto num_points = self->scheduler_->get_schedule_size();
  
  for (size_t i = 0; i < num_points; i++) {
    auto point_info = self->scheduler_->get_schedule_point_info(i);
    JsonObject debug_point = debug_points.add<JsonObject>();
    debug_point["index"] = i;
    debug_point["time_type"] = point_info.time_type;
    debug_point["offset_minutes"] = point_info.offset_minutes;
    debug_point["calculated_time_minutes"] = point_info.time_minutes;
    debug_point["is_dynamic"] = (point_info.time_type != "fixed");
  }
  
  // Add current time and status
  doc["current_time_minutes"] = self->scheduler_->get_current_time_minutes();
  doc["scheduler_enabled"] = self->scheduler_->is_enabled();
  doc["astronomical_projection_enabled"] = self->scheduler_->is_astronomical_projection_enabled();
  
  self->send_json_response(req, 200, doc);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_api_timezone_get(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self || !self->check_auth(req)) {
    return ESP_OK;
  }
  
  JsonDocument doc;
  doc["timezone"] = self->scheduler_->get_timezone();
  doc["timezone_offset_hours"] = self->scheduler_->get_timezone_offset_hours();
  
  // Get current time info
  auto time_source = self->scheduler_->get_time_source();
  if (time_source) {
    auto time = time_source->now();
    if (time.is_valid()) {
      doc["current_offset_seconds"] = time.timezone_offset();
      doc["is_dst"] = time.is_dst;
    }
  }
  
  self->send_json_response(req, 200, doc);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_api_timezone_post(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self || !self->check_auth(req)) {
    return ESP_OK;
  }
  
  // Check content length
  if (req->content_len > 256) {
    self->send_error(req, 413, "Request too large");
    return ESP_OK;
  }
  
  char content[256];
  int ret = httpd_req_recv(req, content, std::min<size_t>(req->content_len, sizeof(content) - 1));
  if (ret <= 0) {
    self->send_error(req, 400, "No data received");
    return ESP_OK;
  }
  content[ret] = '\0';
  
  JsonDocument doc;
  auto error = deserializeJson(doc, content);
  if (error) {
    self->send_error(req, 400, "Invalid JSON");
    return ESP_OK;
  }
  
  // Accept either timezone name or offset
  if (doc["timezone"].is<JsonVariant>()) {
    const char* tz = doc["timezone"];
    self->scheduler_->set_timezone(tz);
    ESP_LOGI(TAG, "Timezone set to: %s", tz);
  } else if (doc["timezone_offset_hours"].is<JsonVariant>()) {
    float offset = doc["timezone_offset_hours"];
    self->scheduler_->set_timezone_offset_hours(offset);
    ESP_LOGI(TAG, "Timezone offset set to: %.1f hours", offset);
  } else {
    self->send_error(req, 400, "Missing timezone or timezone_offset_hours");
    return ESP_OK;
  }
  
  // Update SNTP timezone if it's the active time source
  if (doc["timezone"].is<JsonVariant>()) {
    const char* tz_str = doc["timezone"];
    std::string posix_tz;
    
    // Check if it looks like a POSIX TZ string (contains numbers or special chars)
    bool is_posix = false;
    for (const char* p = tz_str; *p; p++) {
      if (isdigit(*p) || *p == ',' || *p == '-' || *p == '+') {
        is_posix = true;
        break;
      }
    }
    
    if (is_posix) {
      // Use the string directly as POSIX TZ
      posix_tz = tz_str;
      ESP_LOGI(TAG, "Using POSIX TZ string directly: %s", tz_str);
    } else {
      // Try to convert IANA timezone name to POSIX TZ string
      // This is a subset of common timezones - for full support, 
      // consider using a complete mapping table or external service
      struct TZMapping {
        const char* iana;
        const char* posix;
      };
      
      static const TZMapping tz_mappings[] = {
        // US & Canada
        {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
        {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
        {"America/Phoenix", "MST7"},  // No DST
        {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
        {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
        {"America/Toronto", "EST5EDT,M3.2.0,M11.1.0"},
        {"America/Vancouver", "PST8PDT,M3.2.0,M11.1.0"},
        
        // Europe
        {"Europe/London", "GMT0BST,M3.5.0,M10.5.0"},
        {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0"},
        {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0"},
        {"Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0"},
        {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0"},
        {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0"},
        {"Europe/Stockholm", "CET-1CEST,M3.5.0,M10.5.0"},
        {"Europe/Moscow", "MSK-3"},  // No DST since 2014
        
        // Asia
        {"Asia/Tokyo", "JST-9"},
        {"Asia/Shanghai", "CST-8"},
        {"Asia/Singapore", "SGT-8"},
        {"Asia/Hong_Kong", "HKT-8"},
        {"Asia/Seoul", "KST-9"},
        {"Asia/Kolkata", "IST-5:30"},
        {"Asia/Dubai", "GST-4"},
        
        // Australia & NZ
        {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0"},
        {"Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0"},
        {"Australia/Brisbane", "AEST-10"},  // No DST
        {"Australia/Perth", "AWST-8"},
        {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0"},
        
        // Others
        {"UTC", "UTC0"},
        {"Etc/UTC", "UTC0"},
        {nullptr, nullptr}
      };
      
      // Search for matching timezone
      bool found = false;
      for (const TZMapping* map = tz_mappings; map->iana != nullptr; map++) {
        if (strcmp(tz_str, map->iana) == 0) {
          posix_tz = map->posix;
          found = true;
          break;
        }
      }
      
      if (!found) {
        // Try to use it anyway, might be a valid timezone we don't know
        posix_tz = tz_str;
        ESP_LOGW(TAG, "Unknown timezone '%s', using as-is", tz_str);
      }
    }
    
    // Update system timezone
    setenv("TZ", posix_tz.c_str(), 1);
    tzset();
    
    // Update the scheduler's timezone string
    self->scheduler_->set_timezone(tz_str);
    
    ESP_LOGI(TAG, "Updated system timezone to: %s (%s)", tz_str, posix_tz.c_str());
  }
  
  // Force immediate update
  self->scheduler_->update_timezone_from_time_source();
  
  // Save to flash
  self->scheduler_->save_schedule_to_flash();
  
  JsonDocument response;
  response["success"] = true;
  response["timezone"] = self->scheduler_->get_timezone();
  response["timezone_offset_hours"] = self->scheduler_->get_timezone_offset_hours();
  
  self->send_json_response(req, 200, response);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_api_channel_control(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  // Read request body safely
  auto buf = read_request_body(req);
  if (!buf) return ESP_OK;  // Error already sent
  
  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, buf.get());
  
  if (error) {
    self->send_error(req, 400, "Invalid JSON");
    return ESP_OK;
  }
  
  // Check if scheduler is disabled (manual control only works when scheduler is off)
  if (self->scheduler_->is_enabled()) {
    self->send_error(req, 400, "Manual control only available when scheduler is disabled");
    return ESP_OK;
  }
  
  // Extract channel and values
  if (!doc["channel"].is<JsonVariant>() || 
      !doc["pwm"].is<JsonVariant>() || 
      !doc["current"].is<JsonVariant>()) {
    self->send_error(req, 400, "Missing channel, pwm, or current");
    return ESP_OK;
  }
  
  uint8_t channel = doc["channel"];
  float pwm = doc["pwm"];
  float current = doc["current"];
  
  // Validate channel
  if (channel >= self->scheduler_->get_num_channels()) {
    self->send_error(req, 400, "Invalid channel number");
    return ESP_OK;
  }
  
  // Validate PWM (0-100%)
  if (pwm < 0.0f || pwm > 100.0f) {
    self->send_error(req, 400, "PWM must be between 0 and 100");
    return ESP_OK;
  }
  
  // Validate current (0 to max current for channel)
  float max_current = self->scheduler_->get_channel_max_current(channel);
  if (current < 0.0f || current > max_current) {
    self->send_error(req, 400, "Current must be between 0 and " + std::to_string(max_current));
    return ESP_OK;
  }
  
  // Set the channel values directly
  self->scheduler_->set_channel_manual_control(channel, pwm, current);
  
  JsonDocument response_doc;
  response_doc["success"] = true;
  response_doc["channel"] = channel;
  response_doc["pwm"] = pwm;
  response_doc["current"] = current;
  response_doc["message"] = "Channel control updated";
  
  self->send_json_response(req, 200, response_doc);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_api_channel_configs(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;
  
  // Read request body safely
  auto buf = read_request_body(req);
  if (!buf) return ESP_OK;  // Error already sent
  
  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, buf.get());
  
  if (error) {
    self->send_error(req, 400, "Invalid JSON");
    return ESP_OK;
  }
  
  // Check if configs array exists
  if (!doc["configs"].is<JsonArray>()) {
    self->send_error(req, 400, "Missing configs array");
    return ESP_OK;
  }
  
  JsonArray configs = doc["configs"];
  uint8_t num_channels = self->scheduler_->get_num_channels();
  
  // Validate array size
  if (configs.size() != num_channels) {
    self->send_error(req, 400, "Configs array size must match number of channels");
    return ESP_OK;
  }
  
  // Update each channel configuration
  for (uint8_t i = 0; i < num_channels; i++) {
    JsonObject config = configs[i];
    
    // Get current config
    auto current_config = self->scheduler_->get_channel_config(i);
    
    // Update channel name
    if (config["name"].is<const char*>()) {
      current_config.name = std::string(config["name"]);
    }
    
    // Update channel color
    if (config["rgb_hex"].is<const char*>()) {
      current_config.rgb_hex = std::string(config["rgb_hex"]);
    }
    
    // Update max current
    if (config["max_current"].is<JsonVariant>()) {
      float max_current = config["max_current"];
      if (max_current >= 0.0f && max_current <= 10.0f) {
        current_config.max_current = max_current;
      }
    }
    
    // Set the updated config
    self->scheduler_->set_channel_config(i, current_config);
  }
  
  // Save to flash
  self->scheduler_->save_schedule_to_flash();
  
  // Update color sensors to reflect the new colors
  self->scheduler_->update_color_sensors();
  
  ESP_LOGI(TAG, "Updated channel configurations");
  
  JsonDocument response_doc;
  response_doc["success"] = true;
  response_doc["message"] = "Channel configurations updated";
  
  self->send_json_response(req, 200, response_doc);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_not_found(httpd_req_t *req) {
  httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not Found");
  return ESP_OK;
}

}  // namespace ledbrick_web_server
}  // namespace esphome

#endif  // USE_ESP_IDF