These exact cache-operation regions come from Espressif ESP-IDF 5.5.5 and 5.5.6,
`components/esp_driver_ppa/src/{ppa_srm,ppa_blend,ppa_fill}.c`.
The source is copyright Espressif Systems (Shanghai) CO LTD and licensed under
Apache-2.0. The `.inc` files preserve the original text so their SHA-256 values
can reject unexpected SDK changes before any source file is written.

Only the cache-operation region is pinned. Independent driver diagnostics or
upstream changes elsewhere in the source file remain untouched. A new SDK
version or changes inside this region require review and new fixture hashes.
