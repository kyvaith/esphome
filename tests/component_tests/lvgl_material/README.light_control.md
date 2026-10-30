# Bounded entity-control host tests

These tests use this checkout's production sources, discovered with the same
`Path(__file__).parents[3]` convention as `test_tile_surface.py`. Python imports
are checked against that source tree, so an editable install from another
checkout cannot silently supply the component under test. No SDK, network,
PlatformIO, product configuration, credentials, or device is needed.

Run from the fork root with its normal Python test dependencies installed:

```sh
export ESPHOME_TEST_ARDUINOJSON=/existing/public/ArduinoJson/src
python -m pytest -o addopts= -q tests/component_tests/lvgl_material
```

The compiler discovery and environment names follow TileSurface. A system
`c++`, `g++`, or `clang++` is used by default. For an already installed Zig:

```sh
export ESPHOME_TEST_CXX=/existing/zig
export ESPHOME_TEST_CXX_ARGS='c++'
```

PowerShell uses the same variables through `$env:ESPHOME_TEST_CXX`,
`$env:ESPHOME_TEST_CXX_ARGS`, and `$env:ESPHOME_TEST_ARDUINOJSON`. Compiler and
ArduinoJson headers are preflighted, never downloaded or copied into the repo.
Use the public ArduinoJson **7** `src` directory, containing `ArduinoJson.h` and
`ArduinoJson/version.hpp`; an existing ESP-IDF managed-component cache is one
valid provider. Its location is supplied by the caller. Missing optional host
dependencies skip C++ tests, as in TileSurface; an explicitly invalid dependency
fails. Check the pytest summary rather than treating skips as compiled coverage.

The Python-only selection does not invoke C++:

```sh
python -m pytest -o addopts= -q \
  tests/component_tests/lvgl_material/test_host_light_control.py \
  tests/component_tests/lvgl_material/test_entity_control.py::ComponentSchemaTests \
  tests/component_tests/lvgl_material/test_direct_scalar.py::ScalarSchemaTests \
  tests/component_tests/lvgl_material/test_entity_control_codegen.py
```

Coverage is deliberately split by ownership:

- Component and Scalar tests compile actual production translation units with
  narrow temporary LVGL/API/RTOS facades. They exercise protocol arena bounds,
  input/command gating, stale generations, source retirement, one-canvas alias
  barriers, pause rollback, explicit recovery, and named-memory accounting.
  Fifteen public JSON fixtures are data, not copies of codec implementations.
- NAV tests compile the actual navigation and gesture-router translation units
  with snapshot/input facades, both with and without the admission feature.
- Generation and diagnostic tests extract the exact current guarded worker
  blocks from `lvgl_esphome.cpp`. Wave tests extract the actual two additive
  methods and use the production public header. These narrow harnesses avoid
  maintaining a second implementation; changed source boundaries fail loudly.
  They do not compile the complete ESP32/PPA renderer or simulate DSI timing.
- Timing tests compile the production constexpr helper at multiple tick rates.
- Composed codegen tests use real root schemas, IDs, automation actions and
  `cg.get_variable` waits for Tile → entity and Volume → Scalar references,
  enabled and disabled. Only already-created widget lookup is supplied by the
  fixture. Legacy callbacks/controller order and absent-feature defines are
  checked without retaining a historical production-source copy.

Support headers are test-only dependencies, not hardware implementations. Files
created for compilation and any header redirects live in temporary directories.
No vendor source is bundled. Existing TileSurface tests and fixtures are reused
in place, not duplicated or replaced. No CODEOWNERS change is needed.

Host success is not target ABI, RTOS scheduling, PPA/cache coherency, physical
touch, panel acknowledgement, HA permission/service execution, target memory or
performance evidence. Native configuration/build and hardware gates remain
separate. In particular, queue-with-carry is software acceptance, not scanout ACK.
