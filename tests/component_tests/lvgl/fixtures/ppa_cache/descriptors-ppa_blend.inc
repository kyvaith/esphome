    esp_cache_msync((void *)blend_engine->dma_tx_bg_desc, platform->dma_desc_mem_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    esp_cache_msync((void *)blend_engine->dma_tx_fg_desc, platform->dma_desc_mem_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    esp_cache_msync((void *)blend_engine->dma_rx_desc, platform->dma_desc_mem_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
