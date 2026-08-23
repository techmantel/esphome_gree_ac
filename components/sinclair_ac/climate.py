from esphome.components import climate, uart
import esphome.codegen as cg
import esphome.config_validation as cv

AUTO_LOAD = []
DEPENDENCIES = ["uart"]

sinclair_ac_ns = cg.esphome_ns.namespace("sinclair_ac")
SinclairAC = sinclair_ac_ns.class_(
    "SinclairAC", cg.Component, uart.UARTDevice, climate.Climate
)
sinclair_ac_cnt_ns = sinclair_ac_ns.namespace("CNT")
SinclairACCNT = sinclair_ac_cnt_ns.class_("SinclairACCNT", SinclairAC)

SCHEMA = climate.climate_schema(climate.Climate).extend(uart.UART_DEVICE_SCHEMA)

CONFIG_SCHEMA = SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(SinclairACCNT),
})


async def to_code(config):
    var = cg.new_Pvariable(config["id"])
    await climate.register_climate(var, config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
