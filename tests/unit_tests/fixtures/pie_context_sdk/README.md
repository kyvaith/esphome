# Reviewed ESP-IDF PIE context sources

These Apache-2.0 ESP-IDF source fixtures are used only in isolated test SDK trees.
The two reviewed owner files are byte-identical in the official `v5.5.5` and
`v5.5.6` tags. The `fixed` files come from official commit
[`b25cb2906c7db70a86332dffbe77df64345d8a73`](https://github.com/espressif/esp-idf/commit/b25cb2906c7db70a86332dffbe77df64345d8a73).
Only the PIE macro and miscellaneous-field regions of that commit are backported;
other differences between its branch and the reviewed tags are excluded.

| Fixture | Official source path | SHA-256 |
| --- | --- | --- |
| `reviewed-5.5.6/portasm.S` | `components/freertos/FreeRTOS-Kernel/portable/riscv/portasm.S` | `0bfd43a09e877654b2e6c53de6a7dd97ac98139c603c68b895509b9fd027e479` |
| `reviewed-5.5.6/rvruntime-frames.h` | `components/riscv/include/riscv/rvruntime-frames.h` | `a5a493c78dd574b995fd7442816902176b0a52bec841b057b863a1f0ff0813c0` |
| `fixed/portasm.S` | `components/freertos/FreeRTOS-Kernel/portable/riscv/portasm.S` | `661abedc2e815a1289c7c767a515d714d6dd59090a36852acfcfed196f24427e` |
| `fixed/rvruntime-frames.h` | `components/riscv/include/riscv/rvruntime-frames.h` | `490aa603173d09761a89eca6fb0670211d85480566836850a4069748bc807238` |

Tests verify these hashes, the disabled branch against the complete pristine owner
file, and the enabled regions against the independent official fixed source.
