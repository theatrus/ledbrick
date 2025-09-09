import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PORT, CONF_USERNAME, CONF_PASSWORD
from esphome.core import CORE, coroutine_with_priority
import os

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
    
