"""A runtime-only camera catalog can acquire HTTPS sources after setup."""

from collections.abc import Callable
from pathlib import Path

from esphome.components.esp32 import _mbedtls_tls_compiled_out
from esphome.components.esp32.const import KEY_CERT_BUNDLE, KEY_ESP32
from esphome.core import CORE


def test_runtime_sources_keep_tls_and_certificate_bundle(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    """No configured URL or another HTTP consumer may be needed to keep TLS."""
    generate_main(component_config_path("runtime_sources.yaml"))
    assert CORE.data[KEY_ESP32][KEY_CERT_BUNDLE] is True
    assert not _mbedtls_tls_compiled_out()
