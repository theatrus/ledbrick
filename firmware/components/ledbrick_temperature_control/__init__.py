import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, binary_sensor, output, switch, number
from esphome.const import (
    CONF_ID,
    CONF_UPDATE_INTERVAL,
    CONF_ICON,
    CONF_UNIT_OF_MEASUREMENT,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
    UNIT_REVOLUTIONS_PER_MINUTE,
    ICON_THERMOMETER,
    ICON_FAN,
    ICON_GAUGE,
)

DEPENDENCIES = ["ledbrick_scheduler", "sensor", "binary_sensor", "output", "switch", "number"]
AUTO_LOAD = ["binary_sensor", "sensor", "number", "switch"]
CODEOWNERS = ["@ledbrick"]

# Configuration constants
CONF_SCHEDULER_ID = "scheduler_id"
CONF_TARGET_TEMPERATURE = "target_temperature"
CONF_PID_KP = "kp"
CONF_PID_KI = "ki"
CONF_PID_KD = "kd"
CONF_MIN_FAN_PWM = "min_fan_pwm"
CONF_MAX_FAN_PWM = "max_fan_pwm"
CONF_EMERGENCY_TEMPERATURE = "emergency_temperature"
CONF_RECOVERY_TEMPERATURE = "recovery_temperature"
CONF_EMERGENCY_DELAY = "emergency_delay"

# Hardware connections
CONF_FAN_PWM_OUTPUT = "fan_pwm_output"
CONF_FAN_ENABLE_SWITCH = "fan_enable_switch"
CONF_FAN_SPEED_SENSOR = "fan_speed_sensor"
CONF_TEMPERATURE_SENSORS = "temperature_sensors"

# Monitoring sensors
CONF_CURRENT_TEMP_SENSOR = "current_temp_sensor"
CONF_TARGET_TEMP_SENSOR = "target_temp_sensor"
CONF_FAN_PWM_SENSOR = "fan_pwm_sensor"
CONF_PID_ERROR_SENSOR = "pid_error_sensor"
CONF_PID_OUTPUT_SENSOR = "pid_output_sensor"
CONF_THERMAL_EMERGENCY_SENSOR = "thermal_emergency_sensor"
CONF_FAN_ENABLED_SENSOR = "fan_enabled_sensor"

# Control components
CONF_ENABLE_SWITCH = "enable_switch"
CONF_TARGET_TEMP_NUMBER = "target_temp_number"

ledbrick_temperature_control_ns = cg.esphome_ns.namespace("ledbrick_temperature_control")
LEDBrickTemperatureControl = ledbrick_temperature_control_ns.class_(
    "LEDBrickTemperatureControl", cg.Component
)
TemperatureControlEnableSwitch = ledbrick_temperature_control_ns.class_(
    "TemperatureControlEnableSwitch", switch.Switch
)
TargetTemperatureNumber = ledbrick_temperature_control_ns.class_(
    "TargetTemperatureNumber", number.Number
)

# Temperature sensor mapping schema
TEMPERATURE_SENSOR_SCHEMA = cv.Schema({
    cv.Required(CONF_ID): cv.use_id(sensor.Sensor),
    cv.Optional("name"): cv.string,
})

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(LEDBrickTemperatureControl),
    cv.GenerateID(CONF_SCHEDULER_ID): cv.use_id("LEDBrickScheduler"),
    cv.Optional(CONF_UPDATE_INTERVAL, default="1s"): cv.positive_time_period_milliseconds,
    
    # Temperature control parameters
    cv.Optional(CONF_TARGET_TEMPERATURE, default=45.0): cv.float_range(min=10.0, max=80.0),
    cv.Optional(CONF_PID_KP, default=2.0): cv.positive_float,
    cv.Optional(CONF_PID_KI, default=0.1): cv.positive_float,
    cv.Optional(CONF_PID_KD, default=0.5): cv.positive_float,
    cv.Optional(CONF_MIN_FAN_PWM, default=0.0): cv.percentage,
    cv.Optional(CONF_MAX_FAN_PWM, default=100.0): cv.percentage,
    cv.Optional(CONF_EMERGENCY_TEMPERATURE, default=60.0): cv.float_range(min=30.0, max=100.0),
    cv.Optional(CONF_RECOVERY_TEMPERATURE, default=55.0): cv.float_range(min=30.0, max=100.0),
    cv.Optional(CONF_EMERGENCY_DELAY, default="5s"): cv.positive_time_period_milliseconds,
    
    # Hardware connections
    cv.Optional(CONF_FAN_PWM_OUTPUT): cv.use_id(output.FloatOutput),
    cv.Optional(CONF_FAN_ENABLE_SWITCH): cv.use_id(switch.Switch),
    cv.Optional(CONF_FAN_SPEED_SENSOR): cv.use_id(sensor.Sensor),
    cv.Optional(CONF_TEMPERATURE_SENSORS, default=[]): cv.ensure_list(TEMPERATURE_SENSOR_SCHEMA),
    
    # Monitoring sensors for Home Assistant
    cv.Optional(CONF_CURRENT_TEMP_SENSOR): sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        icon=ICON_THERMOMETER,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    cv.Optional(CONF_TARGET_TEMP_SENSOR): sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        icon=ICON_THERMOMETER,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    cv.Optional(CONF_FAN_PWM_SENSOR): sensor.sensor_schema(
        unit_of_measurement=UNIT_PERCENT,
        icon=ICON_FAN,
        accuracy_decimals=1,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    cv.Optional(CONF_PID_ERROR_SENSOR): sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        icon=ICON_GAUGE,
        accuracy_decimals=2,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    cv.Optional(CONF_PID_OUTPUT_SENSOR): sensor.sensor_schema(
        unit_of_measurement=UNIT_PERCENT,
        icon=ICON_GAUGE,
        accuracy_decimals=1,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    cv.Optional(CONF_THERMAL_EMERGENCY_SENSOR): binary_sensor.binary_sensor_schema(
        icon="mdi:alert-circle",
        device_class="problem",
    ),
    cv.Optional(CONF_FAN_ENABLED_SENSOR): binary_sensor.binary_sensor_schema(
        icon=ICON_FAN,
        device_class="running",
    ),
    
    # Control components
    cv.Optional(CONF_ENABLE_SWITCH): switch.switch_schema(
        TemperatureControlEnableSwitch,
        icon="mdi:thermostat",
    ),
    cv.Optional(CONF_TARGET_TEMP_NUMBER): number.number_schema(
        TargetTemperatureNumber,
        unit_of_measurement=UNIT_CELSIUS,
        icon=ICON_THERMOMETER,
    ),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    # Get scheduler reference
    scheduler = await cg.get_variable(config[CONF_SCHEDULER_ID])
    cg.add(var.set_scheduler(scheduler))
    
    # Configure update interval
    cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL]))
    
    # Configure temperature control parameters
    cg.add(var.set_target_temperature(config[CONF_TARGET_TEMPERATURE]))
    cg.add(var.set_pid_parameters(
        config[CONF_PID_KP],
        config[CONF_PID_KI], 
        config[CONF_PID_KD]
    ))
    cg.add(var.set_fan_pwm_range(config[CONF_MIN_FAN_PWM], config[CONF_MAX_FAN_PWM]))
    cg.add(var.set_emergency_temperatures(
        config[CONF_EMERGENCY_TEMPERATURE],
        config[CONF_RECOVERY_TEMPERATURE]
    ))
    cg.add(var.set_emergency_delay(config[CONF_EMERGENCY_DELAY]))
    
    # Configure hardware connections
    if CONF_FAN_PWM_OUTPUT in config:
        fan_pwm = await cg.get_variable(config[CONF_FAN_PWM_OUTPUT])
        cg.add(var.set_fan_pwm_output(fan_pwm))
    
    if CONF_FAN_ENABLE_SWITCH in config:
        fan_switch = await cg.get_variable(config[CONF_FAN_ENABLE_SWITCH])
        cg.add(var.set_fan_enable_switch(fan_switch))
    
    if CONF_FAN_SPEED_SENSOR in config:
        fan_sensor = await cg.get_variable(config[CONF_FAN_SPEED_SENSOR])
        cg.add(var.set_fan_speed_sensor(fan_sensor))
    
    # Configure temperature sensors
    for temp_sensor_config in config[CONF_TEMPERATURE_SENSORS]:
        temp_sensor = await cg.get_variable(temp_sensor_config[CONF_ID])
        sensor_name = temp_sensor_config.get("name", str(temp_sensor_config[CONF_ID]))
        cg.add(var.add_temperature_sensor(sensor_name, temp_sensor))
    
    # Configure monitoring sensors
    for sensor_key, method_name in [
        (CONF_CURRENT_TEMP_SENSOR, "set_current_temp_sensor"),
        (CONF_TARGET_TEMP_SENSOR, "set_target_temp_sensor"),
        (CONF_FAN_PWM_SENSOR, "set_fan_pwm_sensor"),
        (CONF_PID_ERROR_SENSOR, "set_pid_error_sensor"),
        (CONF_PID_OUTPUT_SENSOR, "set_pid_output_sensor"),
    ]:
        if sensor_key in config:
            sens = await sensor.new_sensor(config[sensor_key])
            cg.add(getattr(var, method_name)(sens))
    
    # Configure binary sensors
    for sensor_key, method_name in [
        (CONF_THERMAL_EMERGENCY_SENSOR, "set_thermal_emergency_sensor"),
        (CONF_FAN_ENABLED_SENSOR, "set_fan_enabled_sensor"),
    ]:
        if sensor_key in config:
            sens = await binary_sensor.new_binary_sensor(config[sensor_key])
            cg.add(getattr(var, method_name)(sens))
    
    # Configure control components
    if CONF_ENABLE_SWITCH in config:
        enable_switch = await switch.new_switch(config[CONF_ENABLE_SWITCH])
        cg.add(enable_switch.set_temperature_control(var))
        cg.add(var.set_enable_switch(enable_switch))
    
    if CONF_TARGET_TEMP_NUMBER in config:
        target_temp_number = await number.new_number(
            config[CONF_TARGET_TEMP_NUMBER], 
            min_value=10.0,
            max_value=80.0,
            step=0.5
        )
        cg.add(target_temp_number.set_temperature_control(var))
        cg.add(var.set_target_temp_number(target_temp_number))