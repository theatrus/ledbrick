import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PORT, CONF_USERNAME, CONF_PASSWORD
from esphome.components import sensor
from esphome.core import CORE, coroutine_with_priority
import os
import subprocess

DEPENDENCIES = ["network", "ledbrick_scheduler"]
AUTO_LOAD = ["json"]

ledbrick_web_server_ns = cg.esphome_ns.namespace("ledbrick_web_server")
LEDBrickWebServer = ledbrick_web_server_ns.class_("LEDBrickWebServer", cg.Component)

CONF_SCHEDULER_ID = "scheduler_id"
CONF_VOLTAGE_SENSOR_ID = "voltage_sensor_id"
CONF_CURRENT_SENSOR_ID = "current_sensor_id"

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LEDBrickWebServer),
            cv.GenerateID(CONF_SCHEDULER_ID): cv.use_id("LEDBrickScheduler"),
            cv.Optional(CONF_PORT, default=80): cv.port,
            cv.Optional(CONF_USERNAME): cv.string,
            cv.Optional(CONF_PASSWORD): cv.string,
            cv.Optional(CONF_VOLTAGE_SENSOR_ID): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_CURRENT_SENSOR_ID): cv.use_id(sensor.Sensor),
        }
    ),
    cv.only_with_esp_idf,
)


@coroutine_with_priority(65.0)
async def to_code(config):
    # Get the scheduler component
    scheduler = await cg.get_variable(config[CONF_SCHEDULER_ID])
    
    # Create the web server component
    var = cg.new_Pvariable(config[CONF_ID], scheduler)
    await cg.register_component(var, config)
    
    # Set configuration
    cg.add(var.set_port(config[CONF_PORT]))
    if CONF_USERNAME in config:
        cg.add(var.set_username(config[CONF_USERNAME]))
    if CONF_PASSWORD in config:
        cg.add(var.set_password(config[CONF_PASSWORD]))
    
    # Wire sensors if configured
    if CONF_VOLTAGE_SENSOR_ID in config:
        voltage_sensor = await cg.get_variable(config[CONF_VOLTAGE_SENSOR_ID])
        cg.add(var.set_voltage_sensor(voltage_sensor))
    if CONF_CURRENT_SENSOR_ID in config:
        current_sensor = await cg.get_variable(config[CONF_CURRENT_SENSOR_ID])
        cg.add(var.set_current_sensor(current_sensor))
    
    # Add defines
    cg.add_define("USE_LEDBRICK_WEB_SERVER")
    
    # Check if web_content.cpp exists, if not build React
    component_dir = os.path.dirname(__file__)
    web_content_cpp = os.path.join(component_dir, "web_content.cpp")
    react_dir = os.path.join(component_dir, "web-react")
    
    if not os.path.exists(web_content_cpp) and os.path.exists(react_dir):
        print("Info: Building React web UI...")
        try:
            # Install dependencies if needed
            if not os.path.exists(os.path.join(react_dir, "node_modules")):
                subprocess.run(["npm", "install"], cwd=react_dir, check=True)
            
            # Build React app
            subprocess.run(["npm", "run", "build"], cwd=react_dir, check=True)
            
            # Generate C++ file
            subprocess.run(["npm", "run", "generate-cpp"], cwd=react_dir, check=True)
            
            print("Info: Successfully built React web UI")
        except subprocess.CalledProcessError as e:
            print(f"Warning: Failed to build React web UI: {e}")
        except Exception as e:
            print(f"Warning: Could not build React web UI: {e}")
    
