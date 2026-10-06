"""Small, fixed-storage translation table for embedded UI strings."""

from __future__ import annotations

import json

from esphome import automation, codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = []

CONF_DEFAULT_LOCALE = "default_locale"
CONF_TRANSLATIONS = "translations"
CONF_LOCALE = "locale"
CONF_VALUES = "values"

i18n_ns = cg.esphome_ns.namespace("i18n")
I18nComponent = i18n_ns.class_("I18nComponent", cg.Component)
I18nSetLocaleAction = i18n_ns.class_("I18nSetLocaleAction", automation.Action)

TRANSLATION_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_LOCALE): cv.string_strict,
        cv.Required(CONF_VALUES): cv.Schema({cv.string: cv.string_strict}),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(I18nComponent),
        cv.Optional(CONF_DEFAULT_LOCALE, default="en-US"): cv.string_strict,
        cv.Required(CONF_TRANSLATIONS): cv.All(
            cv.ensure_list(TRANSLATION_SCHEMA), cv.Length(min=1, max=8)
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


def _literal(value: str) -> cg.RawExpression:
    return cg.RawExpression(json.dumps(value, ensure_ascii=False))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_default_locale(_literal(config[CONF_DEFAULT_LOCALE])))
    for pack in config[CONF_TRANSLATIONS]:
        locale = pack[CONF_LOCALE]
        for key, value in pack[CONF_VALUES].items():
            cg.add(var.add_translation(_literal(locale), _literal(key), _literal(value)))


@automation.register_action(
    "i18n.set_locale",
    I18nSetLocaleAction,
    cv.Schema({cv.Required(CONF_ID): cv.use_id(I18nComponent), cv.Required("locale"): cv.templatable(cv.string)}),
    synchronous=True,
)
async def i18n_set_locale_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    templ = await cg.templatable(config["locale"], args, cg.std_string)
    cg.add(var.set_locale(templ))
    return var
