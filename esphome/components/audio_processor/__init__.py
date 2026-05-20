import esphome.codegen as cg
from esphome.components.esp32 import add_idf_component

# Shared audio processor interface header.
# No config schema: this component only provides the C++ interface.
# esp_aec and esp_afe implement it; esp_audio_stack and intercom_api consume it.

CODEOWNERS = ["@n-IA-hane"]

audio_processor_ns = cg.esphome_ns.namespace("audio_processor")
AudioProcessor = audio_processor_ns.class_("AudioProcessor")


async def to_code(config):
    cg.add_define("USE_AUDIO_PROCESSOR")
    add_idf_component(name="espressif/esp-dsp", ref="*")
