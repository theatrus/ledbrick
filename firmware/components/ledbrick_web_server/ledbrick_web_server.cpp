#ifdef USE_ESP_IDF

#include "ledbrick_web_server.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "web_content.h"
#include <esp_log.h>
#include <cstring>

namespace esphome {
namespace ledbrick_web_server {

static const char *const TAG = "ledbrick_web_server";

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
  config.max_uri_handlers = 16;
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
  
  // React build serves single app.js
  httpd_uri_t app_js_uri = {
    .uri = "/app.js",
    .method = HTTP_GET,
    .handler = handle_js,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &app_js_uri);
  
  // Legacy compatibility endpoints
  httpd_uri_t js_uri = {
    .uri = "/ledbrick_api.js",
    .method = HTTP_GET,
    .handler = handle_js,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &js_uri);
  
  httpd_uri_t ui_js_uri = {
    .uri = "/ledbrick-ui.js",
    .method = HTTP_GET,
    .handler = handle_ui_js,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &ui_js_uri);
  
  // React CSS
  httpd_uri_t app_css_uri = {
    .uri = "/app.css",
    .method = HTTP_GET,
    .handler = handle_css,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &app_css_uri);
  
  // Legacy CSS compatibility
  httpd_uri_t css_uri = {
    .uri = "/style.css",
    .method = HTTP_GET,
    .handler = handle_css,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &css_uri);
  
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

esp_err_t LEDBrickWebServer::handle_ui_js(httpd_req_t *req) {
  auto *self = get_instance(req);
  self->send_compressed_content(req, LEDBRICK_UI_JS_COMPRESSED, LEDBRICK_UI_JS_SIZE, LEDBRICK_UI_JS_TYPE);
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
  
  // Read the request body
  char *buf = (char *)malloc(req->content_len + 1);
  if (!buf) {
    self->send_error(req, 500, "Out of memory");
    return ESP_OK;
  }
  
  int ret = httpd_req_recv(req, buf, req->content_len);
  if (ret <= 0) {
    free(buf);
    self->send_error(req, 400, "Failed to read request body");
    return ESP_OK;
  }
  buf[ret] = '\0';
  
  // Import the schedule
  bool success = self->scheduler_->import_schedule_json(buf);
  free(buf);
  
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
  
  // Add built-in presets
  presets.add("dynamic_sunrise_sunset");
  presets.add("full_spectrum");
  presets.add("simple");
  presets.add("reef_natural");
  presets.add("planted_co2");
  presets.add("moonlight_only");
  
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
  doc["pwm_scale"] = self->scheduler_->get_pwm_scale();
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
  
  // Add current channel values
  JsonArray channels = doc["channels"].to<JsonArray>();
  auto values = self->scheduler_->get_current_values();
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
  
  // Read the request body
  char *buf = (char *)malloc(req->content_len + 1);
  if (!buf) {
    self->send_error(req, 500, "Out of memory");
    return ESP_OK;
  }
  
  int ret = httpd_req_recv(req, buf, req->content_len);
  if (ret <= 0) {
    free(buf);
    self->send_error(req, 400, "Failed to read request body");
    return ESP_OK;
  }
  buf[ret] = '\0';
  
  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, buf);
  free(buf);
  
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
  
  // Read the request body
  char *buf = (char *)malloc(req->content_len + 1);
  if (!buf) {
    self->send_error(req, 500, "Out of memory");
    return ESP_OK;
  }
  
  int ret = httpd_req_recv(req, buf, req->content_len);
  if (ret <= 0) {
    free(buf);
    self->send_error(req, 400, "Failed to read request body");
    return ESP_OK;
  }
  buf[ret] = '\0';
  
  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, buf);
  free(buf);
  
  if (error) {
    self->send_error(req, 400, "Invalid JSON");
    return ESP_OK;
  }
  
  // Extract latitude and longitude
  if (!doc.containsKey("latitude") || !doc.containsKey("longitude")) {
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
  
  // Read the request body
  char *buf = (char *)malloc(req->content_len + 1);
  if (!buf) {
    self->send_error(req, 500, "Out of memory");
    return ESP_OK;
  }
  
  int ret = httpd_req_recv(req, buf, req->content_len);
  if (ret <= 0) {
    free(buf);
    self->send_error(req, 400, "Failed to read request body");
    return ESP_OK;
  }
  buf[ret] = '\0';
  
  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, buf);
  free(buf);
  
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
  
  // Read the request body
  char *buf = (char *)malloc(req->content_len + 1);
  if (!buf) {
    self->send_error(req, 500, "Out of memory");
    return ESP_OK;
  }
  
  int ret = httpd_req_recv(req, buf, req->content_len);
  if (ret <= 0) {
    free(buf);
    self->send_error(req, 400, "Failed to read request body");
    return ESP_OK;
  }
  buf[ret] = '\0';
  
  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, buf);
  free(buf);
  
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

esp_err_t LEDBrickWebServer::handle_not_found(httpd_req_t *req) {
  httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not Found");
  return ESP_OK;
}

}  // namespace ledbrick_web_server
}  // namespace esphome

#endif  // USE_ESP_IDF