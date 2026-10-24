    // Write back and invalidate necessary data (note that the window content is not continuous in the buffer)
    // Write back in_bg_buffer, in_fg_buffer extended windows (alignment not necessary on C2M direction)
    uint32_t buf_alignment_size = (uint32_t)ppa_client->engine->platform->buf_alignment_size;
    color_space_pixel_format_t in_bg_pixel_format = {
        .color_type_id = config->in_bg.blend_cm,
    };
    uint32_t in_bg_pixel_depth = color_hal_pixel_format_get_bit_depth(in_bg_pixel_format); // bits
    // Usually C2M can let the msync do alignment internally, however, it only do L1-cacheline-size alignment for L1->L2, and then L2-cacheline-size alignment for L2->mem
    // While M2C direction manual alignment is L2-cacheline-size alignment for mem->L2->L1
    // Mismatching writeback and invalidate data size could cause synchronization error if in_bg/fg_buffer and out_buffer are the same one
    uint32_t in_bg_ext_window = (uint32_t)config->in_bg.buffer + (uint32_t)((uint64_t)config->in_bg.block_offset_y * config->in_bg.pic_w * in_bg_pixel_depth / 8);
    uint32_t in_bg_ext_window_aligned = PPA_ALIGN_DOWN(in_bg_ext_window, buf_alignment_size);
    uint32_t in_bg_ext_window_len = (uint32_t)((uint64_t)config->in_bg.pic_w * config->in_bg.block_h * in_bg_pixel_depth / 8);
    esp_cache_msync((void *)in_bg_ext_window_aligned, PPA_ALIGN_UP(in_bg_ext_window_len + (in_bg_ext_window - in_bg_ext_window_aligned), buf_alignment_size), ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    color_space_pixel_format_t in_fg_pixel_format = {
        .color_type_id = config->in_fg.blend_cm,
    };
    uint32_t in_fg_pixel_depth = color_hal_pixel_format_get_bit_depth(in_fg_pixel_format); // bits
    uint32_t in_fg_ext_window = (uint32_t)config->in_fg.buffer + (uint32_t)((uint64_t)config->in_fg.block_offset_y * config->in_fg.pic_w * in_fg_pixel_depth / 8);
    // Same for fg_buffer msync, do manual alignment
    uint32_t in_fg_ext_window_aligned = PPA_ALIGN_DOWN(in_fg_ext_window, buf_alignment_size);
    uint32_t in_fg_ext_window_len = (uint32_t)((uint64_t)config->in_fg.pic_w * config->in_fg.block_h * in_fg_pixel_depth / 8);
    esp_cache_msync((void *)in_fg_ext_window_aligned, PPA_ALIGN_UP(in_fg_ext_window_len + (in_fg_ext_window - in_fg_ext_window_aligned), buf_alignment_size), ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    // Invalidate out_buffer extended window (alignment strict on M2C direction)
    uint32_t out_ext_window = (uint32_t)config->out.buffer + (uint32_t)((uint64_t)config->out.block_offset_y * config->out.pic_w * out_pixel_depth / 8);
    uint32_t out_ext_window_aligned = PPA_ALIGN_DOWN(out_ext_window, buf_alignment_size);
    uint32_t out_ext_window_len = (uint32_t)((uint64_t)config->out.pic_w * config->in_bg.block_h * out_pixel_depth / 8);
    esp_cache_msync((void *)out_ext_window_aligned, PPA_ALIGN_UP(out_ext_window_len + (out_ext_window - out_ext_window_aligned), buf_alignment_size), ESP_CACHE_MSYNC_FLAG_DIR_M2C);
