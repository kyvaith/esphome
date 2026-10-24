    // Write back and invalidate necessary data (note that the window content is not continuous in the buffer)
    // Write back in_buffer extended window (alignment not necessary on C2M direction)
    uint32_t buf_alignment_size = (uint32_t)ppa_client->engine->platform->buf_alignment_size;
    color_space_pixel_format_t in_pixel_format = {
        .color_type_id = config->in.srm_cm,
    };
    uint32_t in_pixel_depth = color_hal_pixel_format_get_bit_depth(in_pixel_format); // bits
    uint32_t in_ext_window = (uint32_t)config->in.buffer + (uint32_t)((uint64_t)config->in.block_offset_y * config->in.pic_w * in_pixel_depth / 8);
    uint32_t in_ext_window_len = (uint32_t)((uint64_t)config->in.pic_w * config->in.block_h * in_pixel_depth / 8);
    esp_cache_msync((void *)in_ext_window, in_ext_window_len, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    // Invalidate out_buffer extended window (alignment strict on M2C direction)
    uint32_t out_ext_window = (uint32_t)config->out.buffer + (uint32_t)((uint64_t)config->out.block_offset_y * config->out.pic_w * out_pixel_depth / 8);
    uint32_t out_ext_window_aligned = PPA_ALIGN_DOWN(out_ext_window, buf_alignment_size);
    uint32_t out_ext_window_len = (uint32_t)((uint64_t)config->out.pic_w * new_block_h * out_pixel_depth / 8); // actual ext_window_len must be less than or equal to this, since actual block_h <= new_block_h (may round down)
    assert(out_ext_window + out_ext_window_len <= (uint32_t)config->out.buffer + config->out.buffer_size);
    esp_cache_msync((void *)out_ext_window_aligned, PPA_ALIGN_UP(out_ext_window_len + (out_ext_window - out_ext_window_aligned), buf_alignment_size), ESP_CACHE_MSYNC_FLAG_DIR_M2C);
