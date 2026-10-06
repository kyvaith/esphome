"""Pipecat realtime voice bridge used by the product voice package."""

from esphome import automation
import esphome.codegen as cg
from esphome.components import esp32, microphone, speaker
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TRIGGER_ID

CODEOWNERS = ["@kyvaith"]
DEPENDENCIES = ["api", "microphone", "speaker"]

CONF_MICROPHONE = "microphone"
CONF_SPEAKER = "speaker"
CONF_BARGE_IN = "barge_in"
CONF_AUTO_PROVISION = "auto_provision"
CONF_ON_PHASE = "on_phase"
CONF_ON_TRANSCRIPT = "on_transcript"
CONF_ON_AUDIO_LEVEL = "on_audio_level"
CONF_ON_REPEATED_FAILURE = "on_repeated_failure"
CONF_ON_FOLLOWUP_OPENED = "on_followup_opened"
CONF_ON_ERROR = "on_error"

va_pipecat_ns = cg.esphome_ns.namespace("va_pipecat")
VaPipecat = va_pipecat_ns.class_("VaPipecat", cg.Component)
OnPhaseTrigger = va_pipecat_ns.class_("OnPhaseTrigger", automation.Trigger.template(cg.std_string))
OnTranscriptTrigger = va_pipecat_ns.class_(
    "OnTranscriptTrigger", automation.Trigger.template(cg.std_string, cg.std_string)
)
OnAudioLevelTrigger = va_pipecat_ns.class_(
    "OnAudioLevelTrigger", automation.Trigger.template(float, float)
)
OnRepeatedFailureTrigger = va_pipecat_ns.class_("OnRepeatedFailureTrigger", automation.Trigger.template())
OnFollowupOpenedTrigger = va_pipecat_ns.class_("OnFollowupOpenedTrigger", automation.Trigger.template())
OnErrorTrigger = va_pipecat_ns.class_(
    "OnErrorTrigger", automation.Trigger.template(cg.std_string, cg.std_string)
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(VaPipecat),
        cv.Required(CONF_MICROPHONE): microphone.microphone_source_schema(
            min_bits_per_sample=16, max_bits_per_sample=16, min_channels=1, max_channels=1
        ),
        cv.Required(CONF_SPEAKER): cv.use_id(speaker.Speaker),
        cv.Optional(CONF_AUTO_PROVISION, default=True): cv.boolean,
        cv.Optional(CONF_BARGE_IN, default=True): cv.boolean,
        cv.Optional(CONF_ON_PHASE): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(OnPhaseTrigger)}
        ),
        cv.Optional(CONF_ON_TRANSCRIPT): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(OnTranscriptTrigger)}
        ),
        cv.Optional(CONF_ON_AUDIO_LEVEL): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(OnAudioLevelTrigger)}
        ),
        cv.Optional(CONF_ON_REPEATED_FAILURE): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(OnRepeatedFailureTrigger)}
        ),
        cv.Optional(CONF_ON_FOLLOWUP_OPENED): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(OnFollowupOpenedTrigger)}
        ),
        cv.Optional(CONF_ON_ERROR): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(OnErrorTrigger)}
        ),
    }
).extend(cv.COMPONENT_SCHEMA)

FINAL_VALIDATE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_MICROPHONE): microphone.final_validate_microphone_source_schema(
            "va_pipecat", sample_rate=16000
        )
    },
    extra=cv.ALLOW_EXTRA,
)


async def _build_trigger(config, key, trigger_type, var, args):
    for item in config.get(key, []):
        trigger = cg.new_Pvariable(item[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, args, item)


async def to_code(config):
    esp32.add_idf_component(name="espressif/esp_websocket_client", ref="1.7.0")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_auto_provision(config[CONF_AUTO_PROVISION]))
    cg.add(var.set_barge_in(config[CONF_BARGE_IN]))
    mic_source = await microphone.microphone_source_to_code(config[CONF_MICROPHONE], passive=False)
    cg.add(var.set_microphone_source(mic_source))
    spk = await cg.get_variable(config[CONF_SPEAKER])
    cg.add(var.set_speaker(spk))
    await _build_trigger(config, CONF_ON_PHASE, OnPhaseTrigger, var, [(cg.std_string, "phase")])
    await _build_trigger(
        config, CONF_ON_TRANSCRIPT, OnTranscriptTrigger, var,
        [(cg.std_string, "role"), (cg.std_string, "text")]
    )
    await _build_trigger(
        config, CONF_ON_AUDIO_LEVEL, OnAudioLevelTrigger, var,
        [(cg.float_, "input_level"), (cg.float_, "output_level")]
    )
    await _build_trigger(config, CONF_ON_REPEATED_FAILURE, OnRepeatedFailureTrigger, var, [])
    await _build_trigger(config, CONF_ON_FOLLOWUP_OPENED, OnFollowupOpenedTrigger, var, [])
    await _build_trigger(
        config, CONF_ON_ERROR, OnErrorTrigger, var,
        [(cg.std_string, "code"), (cg.std_string, "message")]
    )
