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

// app.js (React bundle)
extern const uint8_t LEDBRICK_JS_COMPRESSED[];
extern const size_t LEDBRICK_JS_SIZE;
extern const char LEDBRICK_JS_TYPE[];

// app.css (React styles)
extern const uint8_t STYLE_CSS_COMPRESSED[];
extern const size_t STYLE_CSS_SIZE;
extern const char STYLE_CSS_TYPE[];

}  // namespace ledbrick_web_server
}  // namespace esphome