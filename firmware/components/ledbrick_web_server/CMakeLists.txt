idf_component_register(
    SRCS "ledbrick_web_server.cpp" "web_content.cpp"
    INCLUDE_DIRS "."
    REQUIRES "esphome" "json" "esp_http_server" "ledbrick_scheduler"
)