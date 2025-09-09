import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PORT, CONF_USERNAME, CONF_PASSWORD
from esphome.core import CORE, coroutine_with_priority
import os
import subprocess

DEPENDENCIES = ["network", "ledbrick_scheduler"]
AUTO_LOAD = ["json"]

ledbrick_web_server_ns = cg.esphome_ns.namespace("ledbrick_web_server")
LEDBrickWebServer = ledbrick_web_server_ns.class_("LEDBrickWebServer", cg.Component)

CONF_SCHEDULER_ID = "scheduler_id"

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LEDBrickWebServer),
            cv.GenerateID(CONF_SCHEDULER_ID): cv.use_id("LEDBrickScheduler"),
            cv.Optional(CONF_PORT, default=80): cv.port,
            cv.Optional(CONF_USERNAME): cv.string,
            cv.Optional(CONF_PASSWORD): cv.string,
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
    
    # Add defines
    cg.add_define("USE_LEDBRICK_WEB_SERVER")
    
    # Run the web content generator script
    component_dir = os.path.dirname(__file__)
    generator_script = os.path.join(component_dir, "generate_web_content.py")
    
    if os.path.exists(generator_script):
        try:
            result = subprocess.run(["python3", generator_script], 
                                  capture_output=True, text=True, cwd=component_dir)
            if result.returncode != 0:
                print(f"Warning: Failed to generate web content: {result.stderr}")
            else:
                print("Info: Successfully regenerated web content")
        except Exception as e:
            print(f"Warning: Could not run web content generator: {e}")
    
