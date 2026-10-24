    // Write back and invalidate necessary data (note that the window content is not continuous in the buffer)
    // Write back and invalidate buffer extended window (alignment not necessary on C2M direction, but alignment strict on M2C direction)
    uint32_t buf_alignment_size = (uint32_t)ppa_client->engine->platform->buf_alignment_size;
    uint32_t out_ext_window = (uint32_t)config->out.buffer + (uint32_t)((uint64_t)config->out.block_offset_y * config->out.pic_w * out_pixel_depth / 8);
    uint32_t out_ext_window_aligned = PPA_ALIGN_DOWN(out_ext_window, buf_alignment_size);
    uint32_t out_ext_window_len = (uint32_t)((uint64_t)config->out.pic_w * config->fill_block_h * out_pixel_depth / 8);
    esp_cache_msync((void *)out_ext_window_aligned, PPA_ALIGN_UP(out_ext_window_len + (out_ext_window - out_ext_window_aligned), buf_alignment_size), ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
