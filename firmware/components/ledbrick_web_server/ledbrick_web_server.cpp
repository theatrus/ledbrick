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
  
  httpd_uri_t js_uri = {
    .uri = "/ledbrick-api.js",
    .method = HTTP_GET,
    .handler = handle_js,
    .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &js_uri);
  
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

// Static content handlers
esp_err_t LEDBrickWebServer::handle_index(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, INDEX_HTML, -1);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_js(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/javascript");
  httpd_resp_send(req, LEDBRICK_JS, -1);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_css(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/css");
  httpd_resp_send(req, STYLE_CSS, -1);
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
  doc["moon_simulation"] = self->scheduler_->is_moon_simulation_enabled();
  
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
  std::vector<float> current_values(8, 2.0);
  
  // Parse PWM values
  size_t i = 0;
  for (JsonVariant v : pwm_array) {
    if (i < 8) pwm_values[i++] = v.as<float>();
  }
  
  // Parse current values
  i = 0;
  for (JsonVariant v : current_array) {
    if (i < 8) current_values[i++] = v.as<float>();
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

esp_err_t LEDBrickWebServer::handle_not_found(httpd_req_t *req) {
  httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not Found");
  return ESP_OK;
}

}  // namespace ledbrick_web_server
}  // namespace esphome

#endif  // USE_ESP_IDF