# LVGL Chart Widget Implementation for ESPHome

## Overview

This implementation provides full support for the LVGL v9.4 Chart widget (`lv_chart`) in ESPHome. The chart widget displays data visualizations including line charts, bar charts, and scatter plots with support for multiple data series.

## Features

- **Chart Types**: LINE, BAR, SCATTER
- **Update Modes**: SHIFT (sliding window), CIRCULAR (ECG-style)
- **Multiple Axes**: PRIMARY_Y, SECONDARY_Y, PRIMARY_X, SECONDARY_X
- **Multiple Series**: Each with its own color and axis
- **Interactive**: Detect pressed points with `on_value`
- **Cursors**: Visual markers that can be positioned on data points
- **Dynamic Colors**: Change series colors at runtime
- **Faded Area**: Gradient effects under line charts
- **Custom Division Lines**: Configurable grid lines

## Actions Available

| Action | Description |
|--------|-------------|
| `lvgl.chart.set_next_value` | Add a point using SHIFT/CIRCULAR mode |
| `lvgl.chart.set_value_by_id` | Set a specific point by index (Y only) |
| `lvgl.chart.set_value_by_id2` | Set X/Y values for scatter charts |
| `lvgl.chart.set_series_color` | Change series color dynamically |
| `lvgl.chart.set_cursor_point` | Move cursor to a specific point |
| `lvgl.chart.refresh` | Refresh chart display |

## Configuration Schema

### Chart Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `type` | enum | LINE | Chart type: LINE, BAR, SCATTER |
| `point_count` | int | 10 | Number of data points |
| `update_mode` | enum | SHIFT | SHIFT or CIRCULAR |
| `div_line_count_hor` | int | 3 | Horizontal grid lines |
| `div_line_count_ver` | int | 5 | Vertical grid lines |
| `series` | list | - | List of data series |
| `cursors` | list | - | List of cursors |
| `axis_primary_y` | dict | - | Primary Y axis config |
| `axis_secondary_y` | dict | - | Secondary Y axis config |
| `axis_primary_x` | dict | - | Primary X axis config |
| `axis_secondary_x` | dict | - | Secondary X axis config |

### Series Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `id` | id | required | Series identifier |
| `color` | color | red | Series color |
| `axis` | enum | PRIMARY_Y | Which axis to use |
| `points` | list | - | Initial Y values (LINE/BAR) |
| `x_points` | list | - | Initial X values (SCATTER) |
| `y_points` | list | - | Initial Y values (SCATTER) |

### Cursor Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `id` | id | required | Cursor identifier |
| `color` | color | red | Cursor line color |
| `direction` | enum | ALL | HOR, VER, or ALL |

### Axis Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `min_value` | int | Minimum axis value |
| `max_value` | int | Maximum axis value |

### Styling Parts

| Part | Description |
|------|-------------|
| `main` | Chart background, border, grid lines |
| `items` | Data series (lines/bars), faded area |
| `indicator` | Point markers on line/scatter |
| `cursor` | Cursor visualization |
| `scrollbar` | Zoom navigation |

## Usage Examples

### 1. Basic Line Chart with Sensor

```yaml
sensor:
  - platform: homeassistant
    id: temperature
    entity_id: sensor.temperature
    on_value:
      then:
        - lvgl.chart.set_next_value:
            id: temp_chart
            series_id: temp_series
            value: !lambda 'return (int)id(temperature).state;'

lvgl:
  pages:
    - widgets:
        - chart:
            id: temp_chart
            x: 20
            y: 20
            width: 300
            height: 150
            type: LINE
            point_count: 24
            update_mode: SHIFT
            axis_primary_y:
              min_value: 0
              max_value: 40
            series:
              - id: temp_series
                color: 0xFF0000
```

### 2. Dual-Axis Chart (Temperature + Humidity)

```yaml
sensor:
  - platform: homeassistant
    id: weather_temp
    entity_id: weather.home
    attribute: temperature
    on_value:
      then:
        - lvgl.label.update:
            id: temp_label
            text:
              format: "%.1f°C"
              args: [id(weather_temp).state]
        - lvgl.chart.set_next_value:
            id: weather_chart
            series_id: temp_series
            value: !lambda 'return (int)id(weather_temp).state;'

  - platform: homeassistant
    id: weather_humidity
    entity_id: weather.home
    attribute: humidity
    on_value:
      then:
        - lvgl.label.update:
            id: humidity_label
            text:
              format: "%.0f%%"
              args: [id(weather_humidity).state]
        - lvgl.chart.set_next_value:
            id: weather_chart
            series_id: humidity_series
            value: !lambda 'return (int)id(weather_humidity).state;'

lvgl:
  pages:
    - widgets:
        - label:
            id: temp_label
            x: 20
            y: 10
            text: "--.-°C"
            text_color: 0xFF0000
        - label:
            id: humidity_label
            x: 120
            y: 10
            text: "--%"
            text_color: 0x0000FF

        - chart:
            id: weather_chart
            x: 20
            y: 40
            width: 350
            height: 180
            type: LINE
            point_count: 24
            div_line_count_hor: 4
            div_line_count_ver: 6
            items:
              line_width: 2
            indicator:
              radius: 3
            axis_primary_y:
              min_value: -10
              max_value: 40
            axis_secondary_y:
              min_value: 0
              max_value: 100
            series:
              - id: temp_series
                color: 0xFF0000
                axis: PRIMARY_Y
              - id: humidity_series
                color: 0x0000FF
                axis: SECONDARY_Y
```

### 3. ECG-Style Circular Chart

```yaml
globals:
  - id: ecg_counter
    type: int
    initial_value: "0"

interval:
  - interval: 100ms
    then:
      - lambda: 'id(ecg_counter)++;'
      - lvgl.chart.set_next_value:
          id: ecg_chart
          series_id: ecg_series
          value: !lambda |-
            int val = 50;
            int phase = id(ecg_counter) % 25;
            if (phase == 0) val = 90;       // P wave
            else if (phase == 5) val = 30;  // Q wave
            else if (phase == 6) val = 95;  // R wave (peak)
            else if (phase == 7) val = 20;  // S wave
            else if (phase == 12) val = 60; // T wave
            else val = 50 + (rand() % 6) - 3;
            return val;

lvgl:
  pages:
    - widgets:
        - chart:
            id: ecg_chart
            x: 20
            y: 20
            width: 400
            height: 200
            type: LINE
            point_count: 200
            update_mode: CIRCULAR
            bg_color: 0x001100
            items:
              line_color: 0x00FF00
              line_width: 2
            indicator:
              radius: 0
            axis_primary_y:
              min_value: 0
              max_value: 100
            series:
              - id: ecg_series
                color: 0x00FF00
```

### 4. Animated Scatter Chart (Multiple Points)

```yaml
globals:
  - id: angle
    type: float
    initial_value: "0"

interval:
  - interval: 50ms
    then:
      - lambda: |-
          id(angle) += 0.08;
          if (id(angle) > 6.28) id(angle) = 0;
      # Animate multiple points in orbit
      - lvgl.chart.set_value_by_id2:
          id: scatter_chart
          series_id: orbit_series
          point_index: 0
          x_value: !lambda 'return 50 + (int)(35 * cos(id(angle)));'
          y_value: !lambda 'return 50 + (int)(35 * sin(id(angle)));'
      - lvgl.chart.set_value_by_id2:
          id: scatter_chart
          series_id: orbit_series
          point_index: 1
          x_value: !lambda 'return 50 + (int)(35 * cos(id(angle) + 1.57));'
          y_value: !lambda 'return 50 + (int)(35 * sin(id(angle) + 1.57));'
      - lvgl.chart.set_value_by_id2:
          id: scatter_chart
          series_id: orbit_series
          point_index: 2
          x_value: !lambda 'return 50 + (int)(35 * cos(id(angle) + 3.14));'
          y_value: !lambda 'return 50 + (int)(35 * sin(id(angle) + 3.14));'
      - lvgl.chart.set_value_by_id2:
          id: scatter_chart
          series_id: orbit_series
          point_index: 3
          x_value: !lambda 'return 50 + (int)(35 * cos(id(angle) + 4.71));'
          y_value: !lambda 'return 50 + (int)(35 * sin(id(angle) + 4.71));'

lvgl:
  pages:
    - widgets:
        - chart:
            id: scatter_chart
            x: 50
            y: 50
            width: 250
            height: 250
            type: SCATTER
            point_count: 4
            indicator:
              bg_color: 0xFF6600
              radius: 8
            axis_primary_y:
              min_value: 0
              max_value: 100
            axis_primary_x:
              min_value: 0
              max_value: 100
            series:
              - id: orbit_series
                color: 0xFF6600
                x_points: [50, 85, 50, 15]
                y_points: [15, 50, 85, 50]
```

### 5. Interactive Chart with Cursor

```yaml
lvgl:
  pages:
    - widgets:
        - label:
            id: cursor_label
            x: 20
            y: 10
            text: "Click a point..."
            text_color: 0xFFCC00

        - chart:
            id: cursor_chart
            x: 20
            y: 40
            width: 350
            height: 200
            type: LINE
            point_count: 12
            clickable: true
            items:
              line_width: 3
            indicator:
              radius: 6
            axis_primary_y:
              min_value: 0
              max_value: 100
            series:
              - id: cursor_series
                color: 0x2196F3
                points: [15, 35, 28, 55, 42, 70, 58, 82, 68, 90, 75, 95]
            cursors:
              - id: main_cursor
                color: 0xFF0000
                direction: ALL
            on_value:
              then:
                - lvgl.chart.set_cursor_point:
                    id: cursor_chart
                    cursor_id: main_cursor
                    series_id: cursor_series
                    point_index: !lambda 'return point_index;'
                - lvgl.label.update:
                    id: cursor_label
                    text:
                      format: "Point %d - Value: %d"
                      args: ['point_index', 'value']
```

### 6. Faded Area Line Chart with Gradient

```yaml
lvgl:
  pages:
    - widgets:
        - chart:
            id: faded_chart
            x: 20
            y: 20
            width: 400
            height: 220
            type: LINE
            point_count: 30
            update_mode: SHIFT
            bg_color: 0x0a0a14
            # Custom division lines
            div_line_count_hor: 6
            div_line_count_ver: 10
            # Faded area effect with gradient
            items:
              line_color: 0x00D9FF
              line_width: 3
              bg_color: 0x00D9FF      # Fill color
              bg_opa: 40%              # Fill opacity
              bg_grad_color: 0x0a0a14  # Gradient end color
              bg_grad_dir: VER         # Vertical gradient
            indicator:
              bg_color: 0x00D9FF
              radius: 5
              border_width: 2
              border_color: 0xFFFFFF
            axis_primary_y:
              min_value: 0
              max_value: 100
            series:
              - id: faded_series
                color: 0x00D9FF
                points: [50, 55, 60, 58, 65, 70, 68, 75, 72, 78]
```

### 7. Dynamic Color Change Based on Value

```yaml
interval:
  - interval: 2s
    then:
      - if:
          condition:
            lambda: 'return id(temperature).state > 25;'
          then:
            - lvgl.chart.set_series_color:
                id: temp_chart
                series_id: temp_series
                series_color: 0xFF0000  # Red when hot
          else:
            - lvgl.chart.set_series_color:
                id: temp_chart
                series_id: temp_series
                series_color: 0x0000FF  # Blue when cold
```

### 8. Bar Chart with Live Data

```yaml
interval:
  - interval: 2s
    then:
      - lvgl.chart.set_next_value:
          id: bar_chart
          series_id: bar_series
          value: !lambda 'return 100 + (rand() % 400);'

lvgl:
  pages:
    - widgets:
        - chart:
            id: bar_chart
            x: 20
            y: 50
            width: 400
            height: 200
            type: BAR
            point_count: 12
            update_mode: SHIFT
            bg_color: 0x16213e
            div_line_count_hor: 5
            div_line_count_ver: 12
            items:
              bg_opa: 80%
            axis_primary_y:
              min_value: 0
              max_value: 500
            series:
              - id: bar_series
                color: 0x4CAF50
                points: [120, 180, 240, 200, 280, 350, 420, 380, 320, 290, 250, 200]
```

## Axis Ticks and Labels

In LVGL 9.x, chart axis ticks are handled by the **Scale widget**. To add axis ticks to your chart:

1. Place a Scale widget next to your chart
2. Configure the Scale with matching min/max values
3. Use `mode: VERTICAL_LEFT` or `VERTICAL_RIGHT` for Y axis
4. Use `mode: HORIZONTAL_TOP` or `HORIZONTAL_BOTTOM` for X axis

See `scale_example.yaml` for examples.

## Troubleshooting

### Labels show "--" instead of values

Make sure the sensor is receiving data from Home Assistant:
1. Check the entity_id is correct
2. Home Assistant API is connected
3. The attribute exists (for weather entities)

```yaml
sensor:
  - platform: homeassistant
    id: my_sensor
    entity_id: sensor.my_entity
    on_value:
      then:
        - logger.log:
            format: "Received value: %.2f"
            args: [id(my_sensor).state]
```

### Chart not updating

1. Ensure `series_id` matches the `id` defined in `series:`
2. Call `lvgl.chart.refresh` if modifying data directly
3. Check that axis range includes your data values

### Points not visible

1. Set `indicator: radius: 4` to show points
2. Ensure `axis_primary_y` range includes your values
3. Check `point_count` is sufficient

### Scatter points not animating

1. Use `lvgl.chart.set_value_by_id2` for each point you want to animate
2. Make sure `point_index` is within range (0 to point_count-1)
3. Call the action for each point in your interval

### Faded area not showing

1. Set `items.bg_opa` to a value > 0 (e.g., `40%`)
2. Set `items.bg_color` for the fill color
3. Set `items.bg_grad_color` and `bg_grad_dir` for gradient effect

### Cursor not showing

1. Make sure `clickable: true` is set on the chart
2. Define `cursors` section with at least one cursor
3. Use `lvgl.chart.set_cursor_point` action to position cursor
