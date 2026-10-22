"""Compatibility of optional audio and image owners with the upstream Sendspin roles."""

from collections.abc import Callable
from pathlib import Path

import pytest

from esphome import config_validation as cv
from esphome.components.esp32 import (
    KEY_COMPONENTS,
    KEY_ESP32,
    KEY_PATH,
    KEY_SDKCONFIG_OPTIONS,
    get_managed_component_require_names,
)
from esphome.components.sendspin import (
    CONF_SUPPORTED_CHANNELS,
    _get_data,
    register_artwork_preference,
)
from esphome.components.sendspin.media_source import CONFIG_SCHEMA
from esphome.const import PlatformFramework
from esphome.core import CORE
from tests.component_tests.types import SetCoreConfigCallable


def test_mono_codecs_reach_the_generated_role(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    """Only PCM mono is offered; a mono sink must not be handed stereo by negotiation."""
    main_cpp = generate_main(component_config_path("mono_pcm.yaml"))
    assert main_cpp.count("SendspinCodecFormat::PCM") == 1
    assert ".channels = 1" in main_cpp
    assert ".channels = 2" not in main_cpp
    assert "SendspinCodecFormat::FLAC" not in main_cpp
    assert "SendspinCodecFormat::OPUS" not in main_cpp


def test_default_channels_keep_upstream_preference(
    set_core_config: SetCoreConfigCallable,
) -> None:
    """Configs without the extension keep the official stereo-then-mono order."""
    set_core_config(PlatformFramework.ESP32_IDF)
    CONFIG_SCHEMA({"id": "source", "sendspin_id": "hub"})
    assert _get_data().player_config[CONF_SUPPORTED_CHANNELS] == [2, 1]


@pytest.mark.parametrize("channels", [[], [1, 1], [0], [3]])
def test_invalid_channel_lists_are_rejected(
    set_core_config: SetCoreConfigCallable, channels: list[int]
) -> None:
    set_core_config(PlatformFramework.ESP32_IDF)
    with pytest.raises(cv.Invalid):
        CONFIG_SCHEMA(
            {"id": "source", "sendspin_id": "hub", "supported_channels": channels}
        )


def test_artwork_owners_share_one_slot_allocator(
    set_core_config: SetCoreConfigCallable,
) -> None:
    """A paused image owner must not reuse a slot already claimed by a native image."""
    set_core_config(PlatformFramework.ESP32_IDF)
    assert register_artwork_preference({"source": "ALBUM"}) == 0
    assert (
        register_artwork_preference({"source": "ARTIST", "require_frame_done": False})
        == 1
    )
    assert len(_get_data().artwork_preferences) == 2


def test_mixed_image_owners_keep_ack_policy_and_unique_slots(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    """The native slot keeps ACKs; paused latest-wins artwork cannot wedge that gate."""
    main_cpp = generate_main(component_config_path("image_owners.yaml"))
    assert main_cpp.count(".require_frame_done = true") == 2
    assert main_cpp.count(".require_frame_done = false") == 1
    assert ".max_image_bytes = 262144" in main_cpp
    assert "retained_artwork->set_sendspin_slot(1);" in main_cpp
    assert "deferred_artwork->set_slot(2);" in main_cpp


@pytest.mark.parametrize("toolchain", ["native", "platformio"])
def test_local_artwork_tls_options_reach_both_idf_toolchains(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
    toolchain: str,
) -> None:
    """The explicit local-URL exception is configured for IDF independently of its build tool."""
    generate_main(component_config_path(f"image_owners_local_{toolchain}.yaml"))
    sdkconfig = CORE.data[KEY_ESP32][KEY_SDKCONFIG_OPTIONS]
    assert sdkconfig["CONFIG_ESP_TLS_INSECURE"] is True
    assert sdkconfig["CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY"] is True
    # A copied component being discovered is insufficient: generated src must
    # declare its dependency so both build tools inherit the public headers.
    jpeg = CORE.data[KEY_ESP32][KEY_COMPONENTS]["libjpeg-turbo-esp32"]
    jpeg_path = Path(jpeg[KEY_PATH])
    assert jpeg_path == CORE.relative_build_path(
        "components", "libjpeg-turbo-esp32"
    ).resolve()
    assert (jpeg_path / "src" / "jpeglib.h").is_file()
    assert "libjpeg-turbo-esp32" in get_managed_component_require_names()
