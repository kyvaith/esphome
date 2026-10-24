"""Validate the opt-in Lottie worker priority and its actual generated call."""

from types import SimpleNamespace
from unittest.mock import AsyncMock

import pytest

from esphome.components.lvgl.widgets import lottie
from esphome.config_validation import Invalid


@pytest.mark.parametrize("priority", [None, 1, 2, 24])
def test_lottie_priority_schema(priority: int | None) -> None:
    config = {"src": "/sd/scene.json", "width": 384, "height": 384}
    if priority is not None:
        config["task_priority"] = priority
    validated = lottie.LOTTIE_SCHEMA(config)
    assert validated["task_priority"] == (2 if priority is None else priority)


@pytest.mark.parametrize("priority", [0, -1, 25])
def test_lottie_priority_rejects_outside_freertos_range(priority: int) -> None:
    with pytest.raises(Invalid):
        lottie.LOTTIE_SCHEMA(
            {
                "src": "/sd/scene.json",
                "width": 384,
                "height": 384,
                "task_priority": priority,
            }
        )


@pytest.mark.parametrize("priority", [1, 2, 24])
@pytest.mark.asyncio
async def test_lottie_priority_reaches_generated_initializer(
    monkeypatch: pytest.MonkeyPatch, priority: int
) -> None:
    emitted = []
    monkeypatch.setattr(lottie, "_lottie_include_added", True)
    monkeypatch.setattr(lottie, "add_lv_use", lambda _: None)
    monkeypatch.setattr(lottie, "lv_obj", SimpleNamespace(set_size=lambda *_: None))
    monkeypatch.setattr(
        lottie, "lv_color", SimpleNamespace(process=AsyncMock(return_value="color"))
    )
    monkeypatch.setattr(
        lottie, "lv_add", lambda statement: emitted.append(str(statement))
    )
    validated = lottie.LOTTIE_SCHEMA(
        {
            "src": "/sd/scene.json",
            "width": 384,
            "height": 384,
            "task_priority": priority,
        }
    )
    await lottie.lottie_spec.to_code(SimpleNamespace(obj="test_scene"), validated)
    assert len(emitted) == 1
    assert f"nullptr, {priority});" in emitted[0]
