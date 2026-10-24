"""The optional input lifecycle preserves the existing default reservation."""

from collections.abc import Callable
from pathlib import Path

import pytest


@pytest.mark.parametrize(
    ("filename", "value"),
    [("runtime_sources.yaml", "false"), ("runtime_sources_release.yaml", "true")],
)
def test_release_input_buffers_codegen(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
    filename: str,
    value: str,
) -> None:
    main = generate_main(component_config_path(filename))
    assert f"set_release_input_buffers_on_stop({value});" in main
