# Scroll snapshot ownership

A scroll snapshot is one logical RGB565 content surface owned by the active
scroll session. Its byte budget is `stride * content_height`, checked against
`max_content_bytes` before any allocation. It is released when the session
ends; it is not an application cache or an additional full-screen work buffer.

The renderer first reuses a sufficiently large external loan, or a contiguous
surface when the heap has enough space. Otherwise it partitions the same pixel
content into row-aligned allocations targeting at most 256 KiB each. The rows
per piece are `max(1, floor(256 KiB / stride))`; the number of pieces is
`ceil(content_height / rows_per_piece)`. Thus the existing content byte budget
also bounds metadata and allocation count. There is no fixed two-piece limit.
A single row wider than the target remains indivisible. Hardware presentation
requires a tightly packed RGB565 stride matching the configured viewport.

Capture builds a private partition. An allocation or rendering failure retires
every new owned piece and restores the original root height, scroll position and
visibility. A borrowed external surface is never freed by LVGL. The partition
becomes visible to the worker only after every piece and its geometry are valid,
and its CPU-written pixels have been synchronized for DMA.

The direct compositor obtains an idle display-owned RGB888 target. It maps the
requested viewport to each intersecting segment and issues hardware PPA copies
into that target. Top and bottom overscroll keep the existing black-fill policy.
It presents only after every required span succeeds. Segment boundaries do not
change framebuffer ownership, cache synchronization or presentation generations.

Refresh drains the worker and updates the existing partition in place. If any
piece fails to render, the whole partition is invalidated and released before
another presentation; no mixture of old and new pieces is published. A failed
worker drain retains all source buffers and ownership. Cleanup, handoff, inertia
completion and error recovery defer instead of freeing or overwriting buffers
still used by that worker.

The snapshot API temporarily changes the scroll root height for each segment.
The validated product uses a flat root with absolute child geometry. Generic
flex, percentage-size or rounded-root compositions require visual boundary QA;
a successful allocation or host geometry test does not establish that layout
contract.

`scroll_snapshot_geometry_test.cpp` executes the actual planner, span visitor,
allocation rollback and borrowed-buffer release helpers. It checks all legal
viewport positions in a representative fragmented surface, exact boundary
mapping, overscroll source coverage, invalid ranges and each partial allocation
failure. The Python test compiles and runs it with a host C++20 compiler. Separate
source checks guard FreeRTOS timeout paths, which cannot execute on the host.
Full firmware compilation and hardware scrolling across segment boundaries are
still required, together with throughput, memory and display diagnostics.
