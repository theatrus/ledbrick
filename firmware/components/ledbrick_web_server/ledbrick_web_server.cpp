#ifdef USE_ESP_IDF

#include "ledbrick_web_server.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "web_content.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

namespace esphome {
namespace ledbrick_web_server {

static const char *const TAG = "ledbrick_web_server";
static const size_t MAX_REQUEST_SIZE = 32768;  // 32KB max request body
static const uint32_t LOOP_CALL_TIMEOUT_MS = 5000;  // Longest wait for the main loop to run a request

LEDBrickWebServer *LEDBrickWebServer::instance_ = nullptr;

// Shut down receive before close so lwIP cannot process packets on a socket that is
// being torn down (same approach as ESPHome's web_server_idf)
static void safe_close_with_shutdown(httpd_handle_t hd, int sockfd) {
  shutdown(sockfd, SHUT_RD);
  close(sockfd);
}

void LEDBrickWebServer::setup() {
  ESP_LOGI(TAG, "Setting up LEDBrick Web Server on port %d", this->port_);

  // Store instance for static callbacks
  instance_ = this;

  struct Route {
    const char *uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t *req);
  };
  static const Route ROUTES[] = {
    // Static content
    {"/", HTTP_GET, handle_index},
    {"/app.js", HTTP_GET, handle_js},
    {"/app.css", HTTP_GET, handle_css},
    // API endpoints
    {"/api/schedule", HTTP_GET, handle_api_schedule_get},
    {"/api/schedule", HTTP_POST, handle_api_schedule_post},
    {"/api/presets", HTTP_GET, handle_api_presets_get},
    {"/api/presets/*", HTTP_POST, handle_api_preset_load},
    {"/api/status", HTTP_GET, handle_api_status_get},
    {"/api/schedule/clear", HTTP_POST, handle_api_clear},
    {"/api/schedule/point", HTTP_POST, handle_api_point_post},
    {"/api/schedule/debug", HTTP_GET, handle_api_schedule_debug},
    {"/api/location", HTTP_POST, handle_api_location_post},
    {"/api/time_shift", HTTP_POST, handle_api_time_shift_post},
    {"/api/time_projection", HTTP_POST, handle_api_time_shift_post},  // Alias for time_shift
    {"/api/moon_simulation", HTTP_POST, handle_api_moon_simulation_post},
    {"/api/timezone", HTTP_GET, handle_api_timezone_get},
    {"/api/timezone", HTTP_POST, handle_api_timezone_post},
    {"/api/channel/control", HTTP_POST, handle_api_channel_control},
    {"/api/channel/configs", HTTP_POST, handle_api_channel_configs},
    {"/api/channel/dimming", HTTP_POST, handle_api_channel_dimming},
    {"/api/led_models", HTTP_GET, handle_api_led_models_get},
    {"/api/led_models", HTTP_POST, handle_api_led_models_post},
    {"/api/temperature/config", HTTP_GET, handle_api_temperature_config_get},
    {"/api/temperature/config", HTTP_POST, handle_api_temperature_config_post},
    {"/api/temperature/status", HTTP_GET, handle_api_temperature_status_get},
    {"/api/temperature/fan-curve", HTTP_GET, handle_api_fan_curve_get},
    {"/api/temperature/reset-emergency", HTTP_POST, handle_api_temperature_reset_emergency},
    // ESPHome-compatible endpoints for scheduler control
    {"/switch/web_scheduler_enable/turn_on", HTTP_POST, handle_scheduler_enable},
    {"/switch/web_scheduler_enable/turn_off", HTTP_POST, handle_scheduler_disable},
    {"/switch/web_scheduler_enable", HTTP_GET, handle_scheduler_state},
    {"/number/pwm_scale/set", HTTP_POST, handle_pwm_scale_set},
    {"/number/pwm_scale", HTTP_GET, handle_pwm_scale_get},
  };

  // Configure and start the HTTP server
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = this->port_;
  config.ctrl_port = this->port_;
  config.stack_size = 8192;
  config.max_uri_handlers = sizeof(ROUTES) / sizeof(ROUTES[0]) + 4;  // Room to add endpoints
  config.recv_wait_timeout = 10;
  config.send_wait_timeout = 10;
  // Match the socket count reserved at codegen; purge the oldest connection when full
  config.max_open_sockets = this->max_open_sockets_;
  config.lru_purge_enable = true;
  config.close_fn = safe_close_with_shutdown;
  // Without this, "/api/presets/*" only matches that literal path
  config.uri_match_fn = httpd_uri_match_wildcard;

  esp_err_t ret = httpd_start(&this->server_, &config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(ret));
    return;
  }

  for (const auto &route : ROUTES) {
    httpd_uri_t uri = {
      .uri = route.uri,
      .method = route.method,
      .handler = route.handler,
      .user_ctx = this
    };
    esp_err_t err = httpd_register_uri_handler(this->server_, &uri);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to register %s: %s", route.uri, esp_err_to_name(err));
    }
  }

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

bool LEDBrickWebServer::run_in_loop_(std::function<void()> &&work) {
  // Shared with the deferred call, so a request that times out leaves nothing dangling.
  // Work functions must capture by value for the same reason.
  enum : uint8_t { PENDING, RUNNING, ABANDONED };
  struct LoopCall {
    std::function<void()> work;
    SemaphoreHandle_t done{nullptr};
    std::atomic<uint8_t> state{PENDING};
    ~LoopCall() {
      if (this->done != nullptr) {
        vSemaphoreDelete(this->done);
      }
    }
  };
  auto call = std::make_shared<LoopCall>();
  call->done = xSemaphoreCreateBinary();
  if (call->done == nullptr) {
    return false;
  }
  call->work = std::move(work);

  // defer() is safe to call from other tasks
  this->defer([call]() {
    uint8_t expected = PENDING;
    if (!call->state.compare_exchange_strong(expected, RUNNING)) {
      return;  // The request already gave up and told the client so
    }
    call->work();
    xSemaphoreGive(call->done);
  });
  App.wake_loop_threadsafe();

  if (xSemaphoreTake(call->done, pdMS_TO_TICKS(LOOP_CALL_TIMEOUT_MS)) == pdTRUE) {
    return true;
  }
  // Cancel work that has not started, so a 503 never hides a change that happens later
  uint8_t expected = PENDING;
  if (call->state.compare_exchange_strong(expected, ABANDONED)) {
    return false;
  }
  // The work is already running on the main loop; it finishes shortly, so report its result
  xSemaphoreTake(call->done, portMAX_DELAY);
  return true;
}

esp_err_t LEDBrickWebServer::respond_from_loop_(httpd_req_t *req, std::function<int(JsonDocument &)> &&build) {
  struct Reply {
    JsonDocument doc;
    int status{200};
  };
  auto reply = std::make_shared<Reply>();
  auto build_fn = std::make_shared<std::function<int(JsonDocument &)>>(std::move(build));
  if (!this->run_in_loop_([reply, build_fn]() { reply->status = (*build_fn)(reply->doc); })) {
    this->send_error(req, 503, "Device busy, try again");
    return ESP_OK;
  }
  this->send_json_response(req, reply->status, reply->doc);
  return ESP_OK;
}

// Fills an error reply for changes that took effect but could not be saved to flash
static int save_failed(JsonDocument &doc) {
  doc["error"] = "Change applied but the schedule is too large to save; it will be lost on restart";
  doc["code"] = 500;
  return 500;
}

// Helper to read the whole request body with size limits
std::unique_ptr<char[]> LEDBrickWebServer::read_request_body(httpd_req_t *req) {
  auto *self = get_instance(req);

  // Check content length
  if (req->content_len > MAX_REQUEST_SIZE) {
    self->send_error(req, 413, "Request too large");
    return nullptr;
  }

  if (req->content_len == 0) {
    self->send_error(req, 400, "Empty request body");
    return nullptr;
  }

  // Allocate buffer on heap
  std::unique_ptr<char[]> buf(new (std::nothrow) char[req->content_len + 1]);
  if (!buf) {
    self->send_error(req, 500, "Out of memory");
    return nullptr;
  }

  // httpd_req_recv returns what has arrived so far, so keep reading until the body is complete
  size_t received = 0;
  int timeouts = 0;
  while (received < req->content_len) {
    int ret = httpd_req_recv(req, buf.get() + received, req->content_len - received);
    if (ret == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) {
      continue;
    }
    if (ret <= 0) {
      self->send_error(req, 400, "Failed to read request body");
      return nullptr;
    }
    received += ret;
  }
  buf[received] = '\0';

  // A parser builds a node for every value. A small body of empty arrays can hold
  // thousands, enough to run the heap out, so refuse it before anything parses it.
  if (LEDScheduler::json_item_bound(buf.get(), received) > LEDScheduler::MAX_JSON_ITEMS) {
    self->send_error(req, 413, "Too many values in request");
    return nullptr;
  }

  return buf;
}

void LEDBrickWebServer::add_allowed_host(const std::string &host) {
  std::string lower = host;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
  this->allowed_hosts_.push_back(lower);
}

bool LEDBrickWebServer::host_allowed_(httpd_req_t *req) {
  // A DNS rebinding page sends a Host and Origin of its own domain, which pass the Origin
  // check. Answer only to addresses and to names this device actually has.
  char host[128];
  esp_err_t err = httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
  if (err == ESP_ERR_NOT_FOUND) {
    return true;  // Browsers always send Host; other clients may not
  }
  if (err != ESP_OK) {
    return false;
  }

  std::string name(host);
  if (!name.empty() && name[0] == '[') {
    return true;  // IPv6 address
  }
  size_t colon = name.rfind(':');
  if (colon != std::string::npos) {
    name.resize(colon);  // Drop the port
  }
  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });

  // IPv4 address: four dot-separated numbers
  int dots = 0;
  bool numeric = !name.empty();
  for (char c : name) {
    if (c == '.') {
      dots++;
    } else if (!isdigit(static_cast<unsigned char>(c))) {
      numeric = false;
    }
  }
  if (numeric && dots == 3) {
    return true;
  }

  std::string device = App.get_name().str();
  std::transform(device.begin(), device.end(), device.begin(), [](unsigned char c) { return std::tolower(c); });
  if (name == device || name == device + ".local" || name == "localhost") {
    return true;
  }
  return std::find(this->allowed_hosts_.begin(), this->allowed_hosts_.end(), name) != this->allowed_hosts_.end();
}

bool LEDBrickWebServer::origin_allowed_(httpd_req_t *req) {
  // Browsers send Origin with cross-site POSTs. Requests without it (curl, Home
  // Assistant, the dev proxy) are allowed; requests from another site are not.
  char origin[128];
  esp_err_t err = httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin));
  if (err == ESP_ERR_NOT_FOUND) {
    return true;
  }
  if (err != ESP_OK) {
    return false;
  }

  char host[128];
  if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) {
    return false;
  }

  const char *origin_host = origin;
  if (strncasecmp(origin_host, "http://", 7) == 0) {
    origin_host += 7;
  } else if (strncasecmp(origin_host, "https://", 8) == 0) {
    origin_host += 8;
  } else {
    return false;  // "null" and other opaque origins
  }
  return strcasecmp(origin_host, host) == 0;
}

void LEDBrickWebServer::send_unauthorized_(httpd_req_t *req) {
  httpd_resp_set_status(req, status_line_(401));
  httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"LEDBrick\"");
  httpd_resp_send(req, "Authorization required", HTTPD_RESP_USE_STRLEN);
}

bool LEDBrickWebServer::check_auth(httpd_req_t *req) {
  if (!this->host_allowed_(req)) {
    this->send_error(req, 403, "Host name not allowed; add it to allowed_hosts");
    return false;
  }
  if (req->method == HTTP_POST && !this->origin_allowed_(req)) {
    this->send_error(req, 403, "Cross-site request rejected");
    return false;
  }

  if (this->username_.empty() || this->password_.empty()) {
    return true;  // No auth required
  }

  char auth_header[256] = {0};
  if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) != ESP_OK ||
      strncmp(auth_header, "Basic ", 6) != 0) {
    this->send_unauthorized_(req);
    return false;
  }

  const char *encoded = auth_header + 6;
  uint8_t decoded[192];
  size_t decoded_len = base64_decode(reinterpret_cast<const uint8_t *>(encoded), strlen(encoded), decoded,
                                     sizeof(decoded));

  // Compare every byte of the expected value so timing does not reveal a partial match
  std::string expected = this->username_ + ":" + this->password_;
  uint8_t diff = decoded_len == expected.size() ? 0 : 1;
  for (size_t i = 0; i < expected.size(); i++) {
    uint8_t got = i < decoded_len ? decoded[i] : 0;
    diff |= got ^ static_cast<uint8_t>(expected[i]);
  }
  if (diff != 0) {
    this->send_unauthorized_(req);
    return false;
  }
  return true;
}

const char *LEDBrickWebServer::status_line_(int status) {
  // httpd_resp_set_status keeps the pointer until the response is sent, so these must be static
  switch (status) {
    case 200: return "200 OK";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 409: return "409 Conflict";
    case 413: return "413 Payload Too Large";
    case 503: return "503 Service Unavailable";
    default: return "500 Internal Server Error";
  }
}

void LEDBrickWebServer::send_json_response(httpd_req_t *req, int status, const JsonDocument &doc) {
  std::string response;
  serializeJson(doc, response);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_status(req, status_line_(status));
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
  // Say why up front, rather than load a page whose API calls all fail
  if (!self->host_allowed_(req)) {
    self->send_error(req, 403, "Host name not allowed; add it to allowed_hosts");
    return ESP_OK;
  }
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

  auto json = std::make_shared<std::string>();
  auto *scheduler = self->scheduler_;
  if (!self->run_in_loop_([scheduler, json]() { scheduler->export_schedule_json(*json); })) {
    self->send_error(req, 503, "Device busy, try again");
    return ESP_OK;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json->c_str(), json->length());
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_api_schedule_post(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;

  // Read request body safely
  auto buf = read_request_body(req);
  if (!buf) return ESP_OK;  // Error already sent

  auto body = std::make_shared<std::string>(buf.get());
  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler, body](JsonDocument &doc) {
    std::string error;
    if (!scheduler->import_schedule_json(*body, &error)) {
      doc["error"] = "Invalid schedule: " + error;
      doc["code"] = 400;
      return 400;
    }
    if (!scheduler->save_schedule_to_flash()) {
      doc["error"] = "Schedule applied but too large to save; it will be lost on restart";
      doc["code"] = 500;
      return 500;
    }
    doc["success"] = true;
    doc["message"] = "Schedule imported successfully";
    doc["points"] = scheduler->get_schedule_size();
    return 200;
  });
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
  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler, preset_name](JsonDocument &doc) {
    // Save here rather than in load_preset, so the schedule is written once and a failed save is reported
    if (!scheduler->load_preset(preset_name, false)) {
      doc["error"] = "Unknown preset";
      doc["code"] = 404;
      return 404;
    }
    if (!scheduler->save_schedule_to_flash()) {
      return save_failed(doc);
    }
    doc["success"] = true;
    doc["preset"] = preset_name;
    doc["message"] = "Preset loaded successfully";
    return 200;
  });
}

esp_err_t LEDBrickWebServer::handle_api_status_get(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;

  return self->respond_from_loop_(req, [self](JsonDocument &doc) {
    auto *scheduler = self->scheduler_;
    // Check if scheduler is initialized
    if (!scheduler) {
      doc["error"] = "Scheduler not initialized";
      doc["code"] = 503;
      return 503;
    }

    doc["enabled"] = scheduler->is_enabled();
    doc["time_minutes"] = scheduler->get_current_time_minutes();
    doc["schedule_points"] = scheduler->get_schedule_size();
    doc["pwm_scale"] = scheduler->get_pwm_scale() * 100.0f;  // Convert to percentage
    doc["latitude"] = scheduler->get_latitude();
    doc["longitude"] = scheduler->get_longitude();
    doc["astronomical_projection"] = scheduler->is_astronomical_projection_enabled();
    doc["time_shift_hours"] = scheduler->get_time_shift_hours();
    doc["time_shift_minutes"] = scheduler->get_time_shift_minutes();

    // Add moon simulation data
    auto moon_config = scheduler->get_moon_simulation();
    JsonObject moon_obj = doc["moon_simulation"].to<JsonObject>();
    moon_obj["enabled"] = moon_config.enabled;
    moon_obj["phase_scaling_pwm"] = moon_config.phase_scaling_pwm;
    moon_obj["phase_scaling_current"] = moon_config.phase_scaling_current;
    moon_obj["min_current_threshold"] = moon_config.min_current_threshold;
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
    auto values = scheduler->get_actual_channel_values();
    for (size_t i = 0; i < values.pwm_values.size() && i < 8; i++) {
      JsonObject channel = channels.add<JsonObject>();
      channel["id"] = i + 1;
      channel["pwm"] = values.pwm_values[i];
      channel["current"] = values.current_values[i];
      if (scheduler->is_curve_channel(i)) {
        channel["mode"] = "curve";
        float level = scheduler->get_channel_level(i);
        if (level >= 0.0f) {
          channel["level"] = level * 100.0f;
        }
      } else {
        channel["mode"] = "manual";
      }
    }

    // Add time formatted (handle invalid time gracefully)
    if (scheduler->has_valid_time()) {
      int time_min = scheduler->get_current_time_minutes();
      char time_str[8];
      snprintf(time_str, sizeof(time_str), "%02d:%02d", time_min / 60, time_min % 60);
      doc["time_formatted"] = time_str;
    } else {
      doc["time_formatted"] = "--:--";
    }

    // Add astronomical times (use projected times if projection is enabled)
    auto astro = scheduler->get_projected_astronomical_times();
    if (astro.rise_valid) {
      char sunrise_str[8];
      snprintf(sunrise_str, sizeof(sunrise_str), "%02d:%02d", astro.rise_minutes / 60, astro.rise_minutes % 60);
      doc["sunrise_time"] = sunrise_str;
    }
    if (astro.set_valid) {
      char sunset_str[8];
      snprintf(sunset_str, sizeof(sunset_str), "%02d:%02d", astro.set_minutes / 60, astro.set_minutes % 60);
      doc["sunset_time"] = sunset_str;
    }

    // Moon: position in the cycle (0 new, 0.5 full) and the share that is lit
    float moon_phase = scheduler->get_moon_phase();
    doc["moon_phase"] = moon_phase;
    doc["moon_illumination"] = AstronomicalCalculator::moon_illumination_from_phase(moon_phase);
    doc["led_temp_c"] = scheduler->get_led_temperature_c();

    // Add moon rise/set times
    auto moon_times = scheduler->get_moon_rise_set_times();
    if (moon_times.rise_valid) {
      char moonrise_str[8];
      snprintf(moonrise_str, sizeof(moonrise_str), "%02d:%02d", moon_times.rise_minutes / 60, moon_times.rise_minutes % 60);
      doc["moonrise_time"] = moonrise_str;
    }
    if (moon_times.set_valid) {
      char moonset_str[8];
      snprintf(moonset_str, sizeof(moonset_str), "%02d:%02d", moon_times.set_minutes / 60, moon_times.set_minutes % 60);
      doc["moonset_time"] = moonset_str;
    }

    // Add INA280 sensor values if available
    if (self->voltage_sensor_ && self->voltage_sensor_->has_state()) {
      doc["voltage"] = self->voltage_sensor_->state;
    }
    if (self->current_sensor_ && self->current_sensor_->has_state()) {
      doc["total_current"] = self->current_sensor_->state;
    }

    // Add fan sensor values if available
    if (self->fan_speed_sensor_ && self->fan_speed_sensor_->has_state()) {
      doc["fan_speed"] = self->fan_speed_sensor_->state;
    }
    if (self->fan_state_sensor_ && self->fan_state_sensor_->has_state()) {
      doc["fan_on"] = self->fan_state_sensor_->state > 0.5f;  // Convert to boolean
    }

    // Add temperature sensors from scheduler (auto-discovered)
    if (scheduler->is_temperature_control_initialized()) {
      const auto &scheduler_temp_sensors = scheduler->get_temperature_sensors();
      if (!scheduler_temp_sensors.empty()) {
        JsonArray temps = doc["temperatures"].to<JsonArray>();
        for (const auto &temp_info : scheduler_temp_sensors) {
          if (temp_info.sensor && temp_info.sensor->has_state()) {
            JsonObject temp = temps.add<JsonObject>();
            temp["value"] = temp_info.sensor->state;
            if (!temp_info.name.empty()) {
              temp["name"] = temp_info.name;
            }
          }
        }
      }
    }

    // Add thermal emergency status
    doc["thermal_emergency"] = scheduler->is_thermal_emergency();

    // Add temperature control status from scheduler
    if (scheduler->is_temperature_control_initialized()) {
      const auto &temp_status = scheduler->get_temperature_status();
      JsonObject temp_control = doc["temperature_control"].to<JsonObject>();
      temp_control["enabled"] = temp_status.enabled;
      temp_control["current_temp"] = temp_status.current_temp_c;
      temp_control["target_temp"] = temp_status.target_temp_c;
      temp_control["fan_pwm"] = temp_status.hardware.fan_pwm_percent;
      temp_control["thermal_emergency"] = temp_status.hardware.thermal_emergency;
    }
    return 200;
  });
}

esp_err_t LEDBrickWebServer::handle_api_clear(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;

  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler](JsonDocument &doc) {
    scheduler->clear_schedule();
    if (!scheduler->save_schedule_to_flash()) {
      return save_failed(doc);
    }
    doc["success"] = true;
    doc["message"] = "Schedule cleared";
    return 200;
  });
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

  std::vector<float> pwm_values(8, 0.0f);
  std::vector<float> current_values(8, LEDScheduler::MAX_CHANNEL_CURRENT);  // Default to the hardware maximum

  // Parse PWM values, clamped to 0-100%
  size_t i = 0;
  for (JsonVariant v : pwm_array) {
    if (i < 8) pwm_values[i++] = std::max(0.0f, std::min(100.0f, v.as<float>()));
  }

  // Parse current values if provided, clamped to the hardware range
  if (!current_array.isNull()) {
    i = 0;
    for (JsonVariant v : current_array) {
      if (i < 8) current_values[i++] = std::max(0.0f, std::min(LEDScheduler::MAX_CHANNEL_CURRENT, v.as<float>()));
    }
  }

  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler, time_minutes, pwm_values, current_values](JsonDocument &response) {
    // A point above a channel's limit would be rejected, so hold it to the limit
    std::vector<float> limited = current_values;
    for (size_t ch = 0; ch < limited.size(); ch++) {
      limited[ch] = std::min(limited[ch], scheduler->get_channel_max_current(ch));
    }
    scheduler->set_schedule_point(time_minutes, pwm_values, limited);
    if (!scheduler->save_schedule_to_flash()) {
      return save_failed(response);
    }
    response["success"] = true;
    response["time_minutes"] = time_minutes;
    response["message"] = "Schedule point added";
    return 200;
  });
}

esp_err_t LEDBrickWebServer::handle_scheduler_enable(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;

  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler](JsonDocument &doc) {
    scheduler->set_enabled(true);  // Saves when the state changes
    doc["id"] = "web_scheduler_enable";
    doc["state"] = "ON";
    return 200;
  });
}

esp_err_t LEDBrickWebServer::handle_scheduler_disable(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;

  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler](JsonDocument &doc) {
    scheduler->set_enabled(false);  // Saves when the state changes
    doc["id"] = "web_scheduler_enable";
    doc["state"] = "OFF";
    return 200;
  });
}

esp_err_t LEDBrickWebServer::handle_scheduler_state(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;

  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler](JsonDocument &doc) {
    doc["id"] = "web_scheduler_enable";
    doc["state"] = scheduler->is_enabled() ? "ON" : "OFF";
    return 200;
  });
}

esp_err_t LEDBrickWebServer::handle_pwm_scale_set(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;

  // Read form data (value=XX)
  auto buf = read_request_body(req);
  if (!buf) return ESP_OK;  // Error already sent

  // httpd_query_key_value copies into its output, so it needs a separate buffer
  char value_str[16];
  if (httpd_query_key_value(buf.get(), "value", value_str, sizeof(value_str)) != ESP_OK) {
    self->send_error(req, 400, "Invalid data");
    return ESP_OK;
  }

  char *end = nullptr;
  float percent = strtof(value_str, &end);
  if (end == value_str || !std::isfinite(percent) || percent < 0.0f || percent > 100.0f) {
    self->send_error(req, 400, "value must be 0-100");
    return ESP_OK;
  }

  auto *scheduler = self->scheduler_;
  float scale = percent / 100.0f;  // Convert from percentage
  auto saved = std::make_shared<bool>(true);
  if (!self->run_in_loop_([scheduler, scale, saved]() { *saved = scheduler->set_pwm_scale(scale); })) {
    self->send_error(req, 503, "Device busy, try again");
    return ESP_OK;
  }
  if (!*saved) {
    self->send_error(req, 500, "PWM scale applied but the schedule is too large to save; it will be lost on restart");
    return ESP_OK;
  }
  httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_pwm_scale_get(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;

  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler](JsonDocument &doc) {
    doc["id"] = "pwm_scale";
    doc["value"] = scheduler->get_pwm_scale() * 100.0f;  // Convert to percentage
    doc["min"] = 0;
    doc["max"] = 100;
    doc["step"] = 1;
    return 200;
  });
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
  if (!doc["latitude"].is<double>() || !doc["longitude"].is<double>()) {
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

  // Optional timezone offset
  bool has_timezone_offset = false;
  double timezone_offset = 0.0;
  if (!doc["timezone_offset_hours"].isNull()) {
    if (!doc["timezone_offset_hours"].is<double>()) {
      self->send_error(req, 400, "timezone_offset_hours must be a number");
      return ESP_OK;
    }
    timezone_offset = doc["timezone_offset_hours"];
    if (timezone_offset < -14.0 || timezone_offset > 14.0) {
      self->send_error(req, 400, "timezone_offset_hours must be between -14 and 14");
      return ESP_OK;
    }
    has_timezone_offset = true;
  }

  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler, latitude, longitude, has_timezone_offset,
                                        timezone_offset](JsonDocument &response) {
    if (has_timezone_offset) {
      scheduler->set_timezone_offset_hours(timezone_offset);
      ESP_LOGI(TAG, "Updated timezone offset to %.1f hours", timezone_offset);
    }

    // Update the scheduler location (saves when it changes)
    bool saved = scheduler->set_location(latitude, longitude);
    // set_location skips the save when only the offset changed
    if (has_timezone_offset) {
      saved = scheduler->save_schedule_to_flash() && saved;
    }
    if (!saved) {
      return save_failed(response);
    }
    ESP_LOGI(TAG, "Updated location to %.4f, %.4f", latitude, longitude);

    response["success"] = true;
    response["latitude"] = latitude;
    response["longitude"] = longitude;
    response["message"] = "Location updated";
    return 200;
  });
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

  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler, projection_enabled, time_shift_hours,
                                        time_shift_minutes](JsonDocument &response) {
    // Update the scheduler settings; each saves when it changes
    bool projection_saved = scheduler->set_astronomical_projection(projection_enabled);
    bool shift_saved = scheduler->set_time_shift(time_shift_hours, time_shift_minutes);

    ESP_LOGI(TAG, "Updated time shift: projection=%s, shift=%+d:%02d",
             projection_enabled ? "enabled" : "disabled",
             time_shift_hours, abs(time_shift_minutes));
    if (!projection_saved || !shift_saved) {
      return save_failed(response);
    }

    response["success"] = true;
    response["astronomical_projection"] = projection_enabled;
    response["time_shift_hours"] = time_shift_hours;
    response["time_shift_minutes"] = time_shift_minutes;
    response["message"] = "Time shift settings updated";
    return 200;
  });
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
  bool phase_scaling_pwm = doc["phase_scaling_pwm"] | true;
  bool phase_scaling_current = doc["phase_scaling_current"] | true;

  float min_current_threshold = 0.0f;
  if (doc["min_current_threshold"].is<float>()) {
    min_current_threshold = std::max(0.0f, std::min(LEDScheduler::MAX_CHANNEL_CURRENT,
                                                    doc["min_current_threshold"].as<float>()));
  }

  ESP_LOGD(TAG, "Received moon sim: enabled=%d, pwm_scale=%d, curr_scale=%d, min_curr=%.3f",
           enabled, phase_scaling_pwm, phase_scaling_current, min_current_threshold);

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
        if (current > LEDScheduler::MAX_CHANNEL_CURRENT) current = LEDScheduler::MAX_CHANNEL_CURRENT;
        base_current[i++] = current;
      }
    }
  }

  // Create moon simulation configuration
  LEDScheduler::MoonSimulation moon_config;
  moon_config.enabled = enabled;
  moon_config.phase_scaling_pwm = phase_scaling_pwm;
  moon_config.phase_scaling_current = phase_scaling_current;
  moon_config.min_current_threshold = min_current_threshold;
  moon_config.base_intensity = base_intensity;
  moon_config.base_current = base_current;

  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler, moon_config](JsonDocument &response) {
    // Update the scheduler settings (saves when they change)
    bool saved = scheduler->set_moon_simulation(moon_config);

    ESP_LOGI(TAG, "Updated moon simulation: enabled=%s, pwm_scaling=%s, current_scaling=%s, min_current=%.3fA",
             moon_config.enabled ? "true" : "false",
             moon_config.phase_scaling_pwm ? "true" : "false",
             moon_config.phase_scaling_current ? "true" : "false",
             moon_config.min_current_threshold);
    if (!saved) {
      return save_failed(response);
    }

    response["success"] = true;
    response["enabled"] = moon_config.enabled;
    response["phase_scaling_pwm"] = moon_config.phase_scaling_pwm;
    response["phase_scaling_current"] = moon_config.phase_scaling_current;
    response["min_current_threshold"] = moon_config.min_current_threshold;

    // Add base_intensity to response
    JsonArray intensity_array = response["base_intensity"].to<JsonArray>();
    for (float intensity : moon_config.base_intensity) {
      intensity_array.add(intensity);
    }

    // Add base_current to response
    JsonArray current_array = response["base_current"].to<JsonArray>();
    for (float current : moon_config.base_current) {
      current_array.add(current);
    }

    response["message"] = "Moon simulation settings updated";
    return 200;
  });
}

esp_err_t LEDBrickWebServer::handle_api_schedule_debug(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;

  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler](JsonDocument &doc) {
    // Get current astronomical times
    auto astro_times = scheduler->get_astronomical_times();
    JsonObject astro = doc["astronomical_times"].to<JsonObject>();
    astro["sunrise_minutes"] = astro_times.rise_minutes;
    astro["sunset_minutes"] = astro_times.set_minutes;

    char sunrise_buf[8];
    char sunset_buf[8];
    snprintf(sunrise_buf, sizeof(sunrise_buf), "%02d:%02d", astro_times.rise_minutes / 60, astro_times.rise_minutes % 60);
    snprintf(sunset_buf, sizeof(sunset_buf), "%02d:%02d", astro_times.set_minutes / 60, astro_times.set_minutes % 60);
    astro["sunrise_formatted"] = sunrise_buf;
    astro["sunset_formatted"] = sunset_buf;

    // Get projected astronomical times if enabled
    if (scheduler->is_astronomical_projection_enabled()) {
      auto projected_times = scheduler->get_projected_astronomical_times();
      JsonObject proj = doc["projected_astronomical_times"].to<JsonObject>();
      proj["sunrise_minutes"] = projected_times.rise_minutes;
      proj["sunset_minutes"] = projected_times.set_minutes;
      proj["time_shift_hours"] = scheduler->get_time_shift_hours();
      proj["time_shift_minutes"] = scheduler->get_time_shift_minutes();
    }

    // Get the full schedule with resolved dynamic points
    std::string schedule_json;
    scheduler->export_schedule_json(schedule_json);

    // Parse the exported JSON and add to our document
    JsonDocument schedule_doc;
    deserializeJson(schedule_doc, schedule_json);
    doc["resolved_schedule"] = schedule_doc;

    // Add debug information for each schedule point
    JsonArray debug_points = doc["debug_points"].to<JsonArray>();
    auto num_points = scheduler->get_schedule_size();

    for (size_t i = 0; i < num_points; i++) {
      auto point_info = scheduler->get_schedule_point_info(i);
      JsonObject debug_point = debug_points.add<JsonObject>();
      debug_point["index"] = i;
      debug_point["time_type"] = point_info.time_type;
      debug_point["offset_minutes"] = point_info.offset_minutes;
      debug_point["calculated_time_minutes"] = point_info.time_minutes;
      debug_point["is_dynamic"] = (point_info.time_type != "fixed");
    }

    // Add current time and status
    doc["current_time_minutes"] = scheduler->get_current_time_minutes();
    doc["scheduler_enabled"] = scheduler->is_enabled();
    doc["astronomical_projection_enabled"] = scheduler->is_astronomical_projection_enabled();
    return 200;
  });
}

esp_err_t LEDBrickWebServer::handle_api_timezone_get(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self || !self->check_auth(req)) {
    return ESP_OK;
  }

  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler](JsonDocument &doc) {
    doc["timezone"] = scheduler->get_timezone();
    doc["timezone_offset_hours"] = scheduler->get_timezone_offset_hours();

    // Get current time info
    auto time_source = scheduler->get_time_source();
    if (time_source) {
      auto time = time_source->now();
      if (time.is_valid()) {
        doc["current_offset_seconds"] = time.timezone_offset();
        doc["is_dst"] = time.is_dst;
      }
    }
    return 200;
  });
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

  auto buf = read_request_body(req);
  if (!buf) return ESP_OK;  // Error already sent

  JsonDocument doc;
  auto error = deserializeJson(doc, buf.get());
  if (error) {
    self->send_error(req, 400, "Invalid JSON");
    return ESP_OK;
  }

  // Accept either timezone name or offset
  bool has_name = false;
  std::string timezone;
  double offset = 0.0;
  if (doc["timezone"].is<const char *>()) {
    timezone = doc["timezone"].as<const char *>();
    // The name is written into the saved JSON as-is, so allow only timezone characters
    bool valid = !timezone.empty() && timezone.size() <= 64;
    for (char c : timezone) {
      valid = valid && (isalnum(static_cast<unsigned char>(c)) || strchr("_/+-:.,", c) != nullptr);
    }
    if (!valid) {
      self->send_error(req, 400, "Invalid timezone name");
      return ESP_OK;
    }
    has_name = true;
  } else if (doc["timezone_offset_hours"].is<double>()) {
    offset = doc["timezone_offset_hours"];
    if (offset < -14.0 || offset > 14.0) {
      self->send_error(req, 400, "timezone_offset_hours must be between -14 and 14");
      return ESP_OK;
    }
  } else {
    self->send_error(req, 400, "Missing timezone or timezone_offset_hours");
    return ESP_OK;
  }

  // ESPHome keeps its own parsed timezone (from YAML or Home Assistant) and ignores
  // setenv("TZ")/tzset(), so this endpoint only updates the scheduler's settings.
  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler, has_name, timezone, offset](JsonDocument &response) {
    if (has_name) {
      scheduler->set_timezone(timezone);
      ESP_LOGI(TAG, "Timezone set to: %s", timezone.c_str());
    } else {
      scheduler->set_timezone_offset_hours(offset);
      ESP_LOGI(TAG, "Timezone offset set to: %.1f hours", offset);
    }

    // Force immediate update
    scheduler->update_timezone_from_time_source();

    // Save to flash
    if (!scheduler->save_schedule_to_flash()) {
      return save_failed(response);
    }

    response["success"] = true;
    response["timezone"] = scheduler->get_timezone();
    response["timezone_offset_hours"] = scheduler->get_timezone_offset_hours();
    return 200;
  });
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

  // A curve-mode channel takes a level (0-100); a manual one takes PWM and current
  const bool by_level = doc["level"].is<float>();
  if (!doc["channel"].is<int>() || (!by_level && (!doc["pwm"].is<float>() || !doc["current"].is<float>()))) {
    self->send_error(req, 400, "Missing channel and level, or channel, pwm and current");
    return ESP_OK;
  }

  int channel = doc["channel"];
  float level = by_level ? doc["level"].as<float>() : 0.0f;
  float pwm = by_level ? 0.0f : doc["pwm"].as<float>();
  float current = by_level ? 0.0f : doc["current"].as<float>();

  if (by_level && !(level >= 0.0f && level <= 100.0f)) {
    self->send_error(req, 400, "level must be between 0 and 100");
    return ESP_OK;
  }
  // Validate PWM (0-100%)
  if (!by_level && !(pwm >= 0.0f && pwm <= 100.0f)) {
    self->send_error(req, 400, "PWM must be between 0 and 100");
    return ESP_OK;
  }

  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler, channel, by_level, level, pwm, current](JsonDocument &response) {
    auto reject = [&response](int status, const std::string &message) {
      response["error"] = message;
      response["code"] = status;
      return status;
    };

    // Manual control only works when the scheduler is off and the LEDs are not in thermal shutdown
    if (scheduler->is_enabled()) {
      return reject(400, "Manual control only available when scheduler is disabled");
    }
    if (scheduler->is_thermal_emergency()) {
      return reject(409, "Thermal emergency active; outputs are held off");
    }
    if (channel < 0 || channel >= scheduler->get_num_channels()) {
      return reject(400, "Invalid channel number");
    }

    if (by_level) {
      if (!scheduler->is_curve_channel(channel)) {
        return reject(400, "level needs a channel in curve mode");
      }
      if (!scheduler->set_channel_manual_level(channel, level / 100.0f)) {
        return reject(409, "Manual control rejected");
      }
      response["success"] = true;
      response["channel"] = channel;
      response["level"] = level;
      response["message"] = "Channel control updated";
      return 200;
    }

    // Validate current (0 to max current for channel)
    float max_current = scheduler->get_channel_max_current(channel);
    if (!(current >= 0.0f && current <= max_current)) {
      return reject(400, "Current must be between 0 and " + std::to_string(max_current));
    }

    // Set the channel values directly
    if (!scheduler->set_channel_manual_control(channel, pwm, current)) {
      return reject(409, "Manual control rejected");
    }

    response["success"] = true;
    response["channel"] = channel;
    response["pwm"] = pwm;
    response["current"] = current;
    response["message"] = "Channel control updated";
    return 200;
  });
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

  // Copy the requested changes out of the document before handing them to the main loop
  struct ConfigUpdate {
    bool has_name{false}, has_color{false}, has_max_current{false};
    std::string name, rgb_hex;
    float max_current{0.0f};
  };
  std::vector<ConfigUpdate> updates;
  for (JsonObject config : doc["configs"].as<JsonArray>()) {
    ConfigUpdate update;
    if (config["name"].is<const char *>()) {
      update.has_name = true;
      update.name = config["name"].as<const char *>();
    }
    if (config["rgb_hex"].is<const char *>()) {
      update.has_color = true;
      update.rgb_hex = config["rgb_hex"].as<const char *>();
    }
    if (config["max_current"].is<float>()) {
      float max_current = config["max_current"];
      if (max_current >= 0.0f && max_current <= 10.0f) {
        update.has_max_current = true;
        update.max_current = max_current;  // Clamped to the hardware limit when applied
      }
    }
    updates.push_back(update);
  }

  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler, updates](JsonDocument &response) {
    uint8_t num_channels = scheduler->get_num_channels();

    // Validate array size
    if (updates.size() != num_channels) {
      response["error"] = "Configs array size must match number of channels";
      response["code"] = 400;
      return 400;
    }

    // Update each channel configuration
    for (uint8_t i = 0; i < num_channels; i++) {
      auto current_config = scheduler->get_channel_config(i);
      if (updates[i].has_name) current_config.name = updates[i].name;
      if (updates[i].has_color) current_config.rgb_hex = updates[i].rgb_hex;
      if (updates[i].has_max_current) current_config.max_current = updates[i].max_current;

      // Clamps max current and keeps the max-current number in step
      scheduler->set_channel_config(i, current_config);
    }

    // Update color sensors to reflect the new colors
    scheduler->update_color_sensors();

    // Save to flash
    if (!scheduler->save_schedule_to_flash()) {
      return save_failed(response);
    }

    ESP_LOGI(TAG, "Updated channel configurations");

    response["success"] = true;
    response["message"] = "Channel configurations updated";
    return 200;
  });
}

esp_err_t LEDBrickWebServer::handle_not_found(httpd_req_t *req) {
  httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not Found");
  return ESP_OK;
}

// Temperature control API handlers
esp_err_t LEDBrickWebServer::handle_api_led_models_get(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;

  // ?custom=true gives just the custom models, as {"led_models": [...]}, ready to post back
  bool custom_only = false;
  char query[48];
  char value[8];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
      httpd_query_key_value(query, "custom", value, sizeof(value)) == ESP_OK) {
    custom_only = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
  }

  // Custom models change at runtime, so read them on the main loop
  auto json = std::make_shared<std::string>();
  auto *scheduler = self->scheduler_;
  if (!self->run_in_loop_([scheduler, json, custom_only]() { *json = scheduler->get_led_models_json(custom_only); })) {
    self->send_error(req, 503, "Device busy, try again");
    return ESP_OK;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json->c_str(), json->length());
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_api_led_models_post(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;

  auto buf = read_request_body(req);
  if (!buf) return ESP_OK;  // Error already sent

  auto body = std::make_shared<std::string>(buf.get());
  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler, body](JsonDocument &doc) {
    std::string error;
    bool save_failed = false;
    if (!scheduler->set_led_models_json(*body, &error, &save_failed)) {
      int code = save_failed ? 500 : 400;
      doc["error"] = error;
      doc["code"] = code;
      return code;
    }
    doc["success"] = true;
    doc["custom_models"] = scheduler->get_custom_led_model_count();
    return 200;
  });
}

esp_err_t LEDBrickWebServer::handle_api_channel_dimming(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;

  auto buf = read_request_body(req);
  if (!buf) return ESP_OK;  // Error already sent

  JsonDocument doc;
  if (deserializeJson(doc, buf.get())) {
    self->send_error(req, 400, "Invalid JSON");
    return ESP_OK;
  }

  // Fields left out keep the channel's current settings
  struct Request {
    int channel{-1};
    bool has_mode{false};
    ledbrick::DimMode mode{ledbrick::DimMode::MANUAL};
    bool has_priority{false};
    ledbrick::DimPriority priority{ledbrick::DimPriority::CURRENT_FIRST};
    bool has_floor{false};
    float floor_current{0.0f};
    bool has_leds{false};
    std::vector<ledbrick::LedGroup> leds;
  } request;

  if (!doc["channel"].is<int>()) {
    self->send_error(req, 400, "Missing channel");
    return ESP_OK;
  }
  request.channel = doc["channel"];
  if (!doc["mode"].isNull()) {
    std::string mode = doc["mode"].is<const char *>() ? doc["mode"].as<const char *>() : "";
    if (mode != "curve" && mode != "manual") {
      self->send_error(req, 400, "mode must be curve or manual");
      return ESP_OK;
    }
    request.has_mode = true;
    request.mode = mode == "curve" ? ledbrick::DimMode::CURVE : ledbrick::DimMode::MANUAL;
  }
  if (!doc["priority"].isNull()) {
    std::string priority = doc["priority"].is<const char *>() ? doc["priority"].as<const char *>() : "";
    if (priority != "current" && priority != "pwm") {
      self->send_error(req, 400, "priority must be current or pwm");
      return ESP_OK;
    }
    request.has_priority = true;
    request.priority = priority == "pwm" ? ledbrick::DimPriority::PWM_FIRST : ledbrick::DimPriority::CURRENT_FIRST;
  }
  if (!doc["floor_current"].isNull()) {
    if (!doc["floor_current"].is<float>()) {
      self->send_error(req, 400, "floor_current must be a number");
      return ESP_OK;
    }
    request.has_floor = true;
    request.floor_current = doc["floor_current"];
  }
  if (doc["leds_default"].is<bool>() && doc["leds_default"].as<bool>()) {
    request.has_leds = true;  // empty: the emitter's LEDs
  } else if (!doc["leds"].isNull()) {
    if (!doc["leds"].is<JsonArrayConst>()) {
      self->send_error(req, 400, "leds must be an array");
      return ESP_OK;
    }
    for (JsonObjectConst entry : doc["leds"].as<JsonArrayConst>()) {
      if (!entry["model"].is<const char *>() || !entry["count"].is<int>()) {
        self->send_error(req, 400, "Each LED needs a model and a whole count");
        return ESP_OK;
      }
      int count = entry["count"];
      if (count < 1 || count > 100) {
        self->send_error(req, 400, "LED count must be 1-100");
        return ESP_OK;
      }
      request.leds.push_back({entry["model"].as<const char *>(), static_cast<uint16_t>(count)});
    }
    request.has_leds = true;
  }

  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler, request](JsonDocument &response) {
    if (request.channel < 0 || request.channel >= scheduler->get_num_channels()) {
      response["error"] = "Invalid channel number";
      response["code"] = 400;
      return 400;
    }
    uint8_t channel = static_cast<uint8_t>(request.channel);
    auto config = scheduler->get_channel_config(channel);
    std::string error;
    if (!scheduler->set_channel_dimming(channel, request.has_mode ? request.mode : config.dim_mode,
                                        request.has_priority ? request.priority : config.dim_priority,
                                        request.has_floor ? request.floor_current : config.floor_current,
                                        request.has_leds ? request.leds : config.leds, &error)) {
      response["error"] = error;
      response["code"] = 400;
      return 400;
    }
    if (!scheduler->save_schedule_to_flash()) {
      return save_failed(response);
    }
    response["success"] = true;
    response["channel"] = request.channel;
    response["mode"] = scheduler->is_curve_channel(channel) ? "curve" : "manual";
    return 200;
  });
}

esp_err_t LEDBrickWebServer::handle_api_temperature_config_get(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;

  auto config_json = std::make_shared<std::string>();
  auto *scheduler = self->scheduler_;
  if (!self->run_in_loop_([scheduler, config_json]() {
        if (scheduler && scheduler->is_temperature_control_initialized()) {
          *config_json = scheduler->get_temperature_config_json();
        }
      })) {
    self->send_error(req, 503, "Device busy, try again");
    return ESP_OK;
  }

  // Check if scheduler and temperature control are initialized
  if (config_json->empty()) {
    self->send_error(req, 503, "Temperature control not initialized");
    return ESP_OK;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, config_json->c_str(), config_json->length());
  return ESP_OK;
}

esp_err_t LEDBrickWebServer::handle_api_temperature_config_post(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;

  // Read request body
  auto buf = read_request_body(req);
  if (!buf) return ESP_OK;

  auto body = std::make_shared<std::string>(buf.get());
  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler, body](JsonDocument &doc) {
    if (scheduler->is_thermal_emergency()) {
      doc["error"] = "Thermal emergency active; temperature settings cannot change until it clears";
      doc["code"] = 409;
      return 409;
    }

    // Update configuration through scheduler
    std::string error;
    if (!scheduler->set_temperature_config_json(*body, &error)) {
      doc["error"] = "Invalid configuration: " + error;
      doc["code"] = 400;
      return 400;
    }
    doc["success"] = true;
    doc["message"] = "Temperature control configuration updated";
    return 200;
  });
}

esp_err_t LEDBrickWebServer::handle_api_temperature_status_get(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;

  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler](JsonDocument &doc) {
    // Get status from scheduler
    const auto &status = scheduler->get_temperature_status();

    doc["enabled"] = status.enabled;
    doc["thermal_emergency"] = status.hardware.thermal_emergency;
    doc["fan_enabled"] = status.hardware.fan_enabled;
    doc["current_temp_c"] = status.current_temp_c;
    doc["max_temp_c"] = status.max_temp_c;
    doc["target_temp_c"] = status.target_temp_c;
    doc["fan_pwm_percent"] = status.hardware.fan_pwm_percent;
    doc["fan_rpm"] = status.hardware.fan_rpm;
    doc["pid_error"] = status.pid_error;
    doc["pid_output"] = status.pid_output;
    doc["sensors_valid_count"] = status.sensors_valid_count;
    doc["sensors_total_count"] = status.sensors_total_count;
    return 200;
  });
}

esp_err_t LEDBrickWebServer::handle_api_temperature_reset_emergency(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;

  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler](JsonDocument &doc) {
    if (!scheduler->is_thermal_emergency()) {
      doc["success"] = true;
      doc["message"] = "No thermal emergency active";
      return 200;
    }
    if (!scheduler->reset_thermal_emergency()) {
      doc["error"] = "A sensor still reads above the recovery temperature";
      doc["code"] = 409;
      return 409;
    }
    doc["success"] = true;
    doc["message"] = "Thermal emergency reset";
    return 200;
  });
}

esp_err_t LEDBrickWebServer::handle_api_fan_curve_get(httpd_req_t *req) {
  auto *self = get_instance(req);
  if (!self->check_auth(req)) return ESP_OK;

  auto *scheduler = self->scheduler_;
  return self->respond_from_loop_(req, [scheduler](JsonDocument &doc) {
    // Get fan curve from scheduler
    auto curve = scheduler->get_fan_curve();

    JsonArray points = doc["points"].to<JsonArray>();
    for (const auto& point : curve) {
      JsonObject p = points.add<JsonObject>();
      p["temperature"] = point.temperature;
      p["fan_pwm"] = point.fan_pwm;
    }
    return 200;
  });
}

}  // namespace ledbrick_web_server
}  // namespace esphome

#endif  // USE_ESP_IDF
