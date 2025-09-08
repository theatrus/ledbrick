"""
LEDBrick Scheduler Custom Component
A professional multi-channel LED scheduler with dynamic configuration
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import time as time_
from esphome.const import (
    CONF_ID,
    CONF_UPDATE_INTERVAL,
)
from esphome import automation
from esphome.automation import maybe_simple_id

CODEOWNERS = ["@theatrus"]

ledbrick_scheduler_ns = cg.esphome_ns.namespace("ledbrick_scheduler")
LEDBrickScheduler = ledbrick_scheduler_ns.class_("LEDBrickScheduler", cg.Component)

# Actions
SetSchedulePointAction = ledbrick_scheduler_ns.class_("SetSchedulePointAction", automation.Action)
LoadPresetAction = ledbrick_scheduler_ns.class_("LoadPresetAction", automation.Action)
SetEnabledAction = ledbrick_scheduler_ns.class_("SetEnabledAction", automation.Action)

CONF_CHANNELS = "channels"
CONF_TIME_SOURCE = "time_source"
CONF_TIMEZONE = "timezone"
CONF_TIMEPOINT = "timepoint"
CONF_PWM_VALUES = "pwm_values"
CONF_CURRENT_VALUES = "current_values"
CONF_PRESET_NAME = "preset_name"
CONF_ENABLED = "enabled"

DEFAULT_UPDATE_INTERVAL = "30s"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(LEDBrickScheduler),
    cv.Optional(CONF_CHANNELS, default=8): cv.int_range(min=1, max=16),
    cv.Optional(CONF_UPDATE_INTERVAL, default=DEFAULT_UPDATE_INTERVAL): cv.update_interval,
    cv.Optional(CONF_TIME_SOURCE): cv.use_id(time_.RealTimeClock),
    cv.Optional(CONF_TIMEZONE, default="UTC"): cv.string_strict,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID], config[CONF_UPDATE_INTERVAL])
    await cg.register_component(var, config)
    
    cg.add(var.set_num_channels(config[CONF_CHANNELS]))
    cg.add(var.set_timezone(config[CONF_TIMEZONE]))
    
    if CONF_TIME_SOURCE in config:
        time_source = await cg.get_variable(config[CONF_TIME_SOURCE])
        cg.add(var.set_time_source(time_source))


# Action schemas and code generation
SET_SCHEDULE_POINT_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(LEDBrickScheduler),
    cv.Required(CONF_TIMEPOINT): cv.templatable(cv.int_range(min=0, max=1439)),  # 0-1439 minutes
    cv.Required(CONF_PWM_VALUES): cv.templatable(cv.All(cv.ensure_list(cv.percentage), cv.Length(min=1, max=16))),
    cv.Required(CONF_CURRENT_VALUES): cv.templatable(cv.All(cv.ensure_list(cv.float_range(min=0.0, max=5.0)), cv.Length(min=1, max=16))),
})

LOAD_PRESET_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(LEDBrickScheduler),
    cv.Required(CONF_PRESET_NAME): cv.templatable(cv.string),
})

SET_ENABLED_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(LEDBrickScheduler),
    cv.Required(CONF_ENABLED): cv.templatable(cv.boolean),
})


@automation.register_action("ledbrick_scheduler.set_schedule_point", SetSchedulePointAction, SET_SCHEDULE_POINT_SCHEMA)
async def set_schedule_point_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    
    timepoint = await cg.templatable(config[CONF_TIMEPOINT], args, cg.uint16)
    cg.add(var.set_timepoint(timepoint))
    
    pwm_values = await cg.templatable(config[CONF_PWM_VALUES], args, cg.std_vector.template(cg.float_))
    cg.add(var.set_pwm_values(pwm_values))
    
    current_values = await cg.templatable(config[CONF_CURRENT_VALUES], args, cg.std_vector.template(cg.float_))
    cg.add(var.set_current_values(current_values))
    
    return var


@automation.register_action("ledbrick_scheduler.load_preset", LoadPresetAction, LOAD_PRESET_SCHEMA)
async def load_preset_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    
    preset_name = await cg.templatable(config[CONF_PRESET_NAME], args, cg.std_string)
    cg.add(var.set_preset_name(preset_name))
    
    return var


@automation.register_action("ledbrick_scheduler.set_enabled", SetEnabledAction, SET_ENABLED_SCHEMA)
async def set_enabled_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    
    enabled = await cg.templatable(config[CONF_ENABLED], args, cg.bool_)
    cg.add(var.set_enabled(enabled))
    
    return var