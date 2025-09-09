#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace ledbrick_web_server {

// Web content embedded as compressed byte arrays
// Content is gzip compressed for minimal size

// index.html
extern const uint8_t INDEX_HTML_COMPRESSED[];
extern const size_t INDEX_HTML_SIZE;
extern const char INDEX_HTML_TYPE[];

// ledbrick-api.js
extern const uint8_t LEDBRICK_JS_COMPRESSED[];
extern const size_t LEDBRICK_JS_SIZE;
extern const char LEDBRICK_JS_TYPE[];

// ledbrick-ui.js  
extern const uint8_t LEDBRICK_UI_JS_COMPRESSED[];
extern const size_t LEDBRICK_UI_JS_SIZE;
extern const char LEDBRICK_UI_JS_TYPE[];

// style.css
extern const uint8_t STYLE_CSS_COMPRESSED[];
extern const size_t STYLE_CSS_SIZE;
extern const char STYLE_CSS_TYPE[];

}  // namespace ledbrick_web_server
}  // namespace esphome