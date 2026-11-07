"""Small, fixed-storage translation table for embedded UI strings."""

from __future__ import annotations

import json

from esphome import automation, codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = []
DOMAIN = "i18n"

CONF_DEFAULT_LOCALE = "default_locale"
CONF_TRANSLATIONS = "translations"
CONF_LOCALE = "locale"
CONF_VALUES = "values"
CONF_USE_FLASH_TABLE = "use_flash_table"
MAX_TRANSLATION_ENTRIES = 65535

i18n_ns = cg.esphome_ns.namespace("i18n")
I18nComponent = i18n_ns.class_("I18nComponent", cg.Component)
Translation = i18n_ns.struct("I18nComponent::Translation")
I18nSetLocaleAction = i18n_ns.class_("I18nSetLocaleAction", automation.Action)

TRANSLATION_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_LOCALE): cv.string_strict,
        cv.Required(CONF_VALUES): cv.Schema({cv.string: cv.string_strict}),
    }
)


def translation_count(config):
    return sum(len(pack[CONF_VALUES]) for pack in config[CONF_TRANSLATIONS])


def validate_translation_capacity(config):
    if translation_count(config) > MAX_TRANSLATION_ENTRIES:
        raise cv.Invalid(
            "i18n supports at most 65535 translation entries, including repeated locale packs"
        )
    if config[CONF_USE_FLASH_TABLE]:
        cv.only_on_esp32(config)
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(I18nComponent),
            cv.Optional(CONF_DEFAULT_LOCALE, default="en-US"): cv.string_strict,
            cv.Optional(CONF_USE_FLASH_TABLE, default=False): cv.boolean,
            cv.Required(CONF_TRANSLATIONS): cv.All(
                cv.ensure_list(TRANSLATION_SCHEMA), cv.Length(min=1, max=8)
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    validate_translation_capacity,
)


def _literal(value: str) -> cg.RawExpression:
    return cg.RawExpression(json.dumps(value, ensure_ascii=False))


async def to_code(config):
    # This component is not MULTI_CONF. One exact, fixed table covers all packs,
    # including repeated locales whose existing first-match order is retained.
    cg.add_define("I18N_TRANSLATION_CAPACITY", max(1, translation_count(config)))
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_default_locale(_literal(config[CONF_DEFAULT_LOCALE])))
    if config[CONF_USE_FLASH_TABLE]:
        cg.add_define("USE_I18N_FLASH_TABLE")
        entries = [
            [_literal(pack[CONF_LOCALE]), _literal(key), _literal(value)]
            for pack in config[CONF_TRANSLATIONS]
            for key, value in pack[CONF_VALUES].items()
        ]
        table = (
            cg.shared_progmem_array("i18n_translations", Translation, entries)
            if entries
            else cg.RawExpression("nullptr")
        )
        cg.add(var.set_translation_table(table, len(entries)))
        return
    for pack in config[CONF_TRANSLATIONS]:
        locale = pack[CONF_LOCALE]
        for key, value in pack[CONF_VALUES].items():
            cg.add(
                var.add_translation(_literal(locale), _literal(key), _literal(value))
            )


@automation.register_action(
    "i18n.set_locale",
    I18nSetLocaleAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(I18nComponent),
            cv.Required("locale"): cv.templatable(cv.string),
        }
    ),
    synchronous=True,
)
async def i18n_set_locale_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    templ = await cg.templatable(config["locale"], args, cg.std_string)
    cg.add(var.set_locale(templ))
    return var
