# ZMK Input Processor: Threshold-based Temporary Layer

This is a custom ZMK input processor that extends the standard temporary layer functionality with an **activation threshold** based on accumulated movement distance.

## Features

- **Movement Threshold**: Only activates the layer after the trackball/mouse has moved a minimum distance
- **Auto-deactivation**: Automatically returns to the base layer after a configurable timeout
- **Idle Requirement**: Optional setting to prevent activation immediately after keyboard key presses
- **Excluded Positions**: Specify key positions (like mouse buttons) that won't deactivate the layer

## Installation

Add this module to your `config/west.yml`:

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: your-username
      url-base: https://github.com/your-username
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: zmk-input-processor-threshold-temp-layer
      remote: your-username
      revision: main
  self:
    path: config
```

## Usage

In your board overlay file (e.g., `mona2_r.overlay`):

```dts
&trackball_listener {
    input-processors = <&zip_threshold_temp_layer 1 500 20>;
                     // Parameters: layer, timeout_ms, threshold_pixels
};
```

### Parameters

1. **layer** (required): The layer index to activate (e.g., `1` for layer 1)
2. **timeout** (required): Milliseconds before auto-deactivation (e.g., `500` for 500ms)
3. **activation-threshold** (optional, default: 0): Minimum movement distance in pixels (e.g., `20`)

### Example Configurations

**Activate immediately (like standard temp-layer):**
```dts
input-processors = <&zip_threshold_temp_layer 1 500 0>;
```

**Require 20 pixels of movement:**
```dts
input-processors = <&zip_threshold_temp_layer 1 500 20>;
```

**Require 50 pixels + 200ms keyboard idle:**
```dts
/ {
    custom_temp_layer: input_processor_custom {
        compatible = "zmk,input-processor-threshold-temp-layer";
        #input-processor-cells = <2>;
        layer = <1>;
        timeout = <500>;
        activation-threshold = <50>;
        require-prior-idle-ms = <200>;
    };
};

&trackball_listener {
    input-processors = <&custom_temp_layer>;
};
```

## How It Works

1. **Movement Accumulation**: The processor accumulates the distance traveled by the trackball
2. **Threshold Check**: When accumulated distance reaches the threshold, the layer activates
3. **Layer Active**: Once active, the trackball works normally on that layer
4. **Auto-deactivation**: After the timeout period with no movement, the layer deactivates
5. **Reset**: The accumulated distance resets when:
   - The layer deactivates (timeout)
   - A keyboard key is pressed

## License

MIT License - same as ZMK
