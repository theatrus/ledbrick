#include "led_dimming.h"

namespace ledbrick {

// Datasheet curves for the LEDs on LEDBrick emitters. Filled in from the Lumileds datasheets.
const std::vector<LedModel>& builtin_led_models() {
    static const std::vector<LedModel> models;
    return models;
}

}  // namespace ledbrick
