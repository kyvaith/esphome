"""Hardware fade uses the upstream action generator beside dynamic frequency."""

from collections.abc import Callable
from pathlib import Path


def test_fade_and_frequency_actions_coexist(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    """A timed fade still targets the configured channel after an upstream action."""
    main_cpp = generate_main(component_config_path("fade.yaml"))
    assert "backlight->update_frequency(" in main_cpp
    assert "backlight->set_next_fade_duration(" in main_cpp
    assert "SetNextFadeDurationAction" not in main_cpp
