# ZMK Input Processor: Threshold-based Temporary Layer

This is a custom ZMK input processor that extends the standard temporary layer functionality with an **activation threshold** based on accumulated movement distance.

## Features

- **Movement Threshold**: Only activates the layer after the trackball/mouse has moved a minimum distance
- **Auto-deactivation**: Automatically returns to the base layer after a configurable timeout
- **Idle Requirement**: Optional setting to prevent activation immediately after keyboard key presses
- **Excluded Positions**: Specify key positions (like mouse buttons) that won't deactivate the layer

## Why Use This?

The standard ZMK temporary layer activates immediately on any trackball movement, which can cause accidental layer switches from small, unintentional movements. This processor adds a threshold so the layer only activates after deliberate trackball usage.

**Use Case Example**: Auto-mouse layer that activates only when you intentionally move the trackball, preventing accidental activation from minor movements or vibrations.

## Installation

### Step 1: Update `config/west.yml`

Add this module as a project in your ZMK config repository's `config/west.yml`:

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: gohanda11  # Add this remote
      url-base: https://github.com/gohanda11
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: zmk-input-processor-threshold-temp-layer  # Add this project
      remote: gohanda11
      revision: main
  self:
    path: config
```

**Note**: If you already have other custom modules, just add the `gohanda11` remote and the `zmk-input-processor-threshold-temp-layer` project to your existing `west.yml`.

## Usage

### Step 2: Define the Input Processor Node

In your board overlay file (e.g., `boards/shields/yourboard/yourboard_right.overlay`), add the processor definition at the root level:

```dts
#include <input/processors.dtsi>
#include <zephyr/dt-bindings/input/input-event-codes.h>

/ {
    // Define the threshold temp layer processor
    zip_threshold_temp_layer: input_processor_threshold_temp_layer {
        compatible = "zmk,input-processor-threshold-temp-layer";
        #input-processor-cells = <2>;
        activation-threshold = <100>;  // Pixels to move before layer activates
        require-prior-idle-ms = <0>;   // Optional: ms of keyboard idle required
        excluded-positions = <>;        // Optional: key positions that don't deactivate layer
    };
};
```

### Step 3: Apply to Your Input Listener

Add the processor to your trackball/mouse input listener:

```dts
&trackball_listener {
    status = "okay";
    device = <&trackball_device>;

    // Apply the threshold temp layer processor
    input-processors = <&zip_threshold_temp_layer 1 500>;
                     // Parameters: layer_number, timeout_ms
};
```

### Runtime Parameters

When using the processor in `input-processors`, you specify:

1. **layer** (required): The layer index to activate (e.g., `1` for layer 1)
2. **timeout** (required): Milliseconds before auto-deactivation (e.g., `500` for 500ms)

### Device Properties

Configure these in the device tree node definition:

- **`activation-threshold`** (default: `0`): Minimum movement distance in pixels before layer activates
  - `0` = activate immediately (behaves like standard temp-layer)
  - `100` = require 100 pixels of movement
  - `200` = require 200 pixels of movement (recommended for deliberate activation)

- **`require-prior-idle-ms`** (default: `0`): Milliseconds that must pass after last keystroke before layer can activate
  - `0` = can activate anytime
  - `200` = must wait 200ms after last key press

- **`excluded-positions`** (default: `<>`): Array of key position indices that won't deactivate the layer
  - Useful for mouse buttons on the same layer
  - Example: `<12 13 14>` excludes positions 12, 13, and 14

### Complete Example

Here's a complete example for a split keyboard with trackball on the right side:

```dts
#include "yourboard.dtsi"
#include <input/processors.dtsi>
#include <zephyr/dt-bindings/input/input-event-codes.h>

/ {
    // Define threshold-based temp layer processor
    zip_threshold_temp_layer: input_processor_threshold_temp_layer {
        compatible = "zmk,input-processor-threshold-temp-layer";
        #input-processor-cells = <2>;
        activation-threshold = <200>;   // Require 200px movement
        require-prior-idle-ms = <0>;
        excluded-positions = <>;
    };
};

&trackball_listener {
    status = "okay";
    device = <&trackball_central>;

    // Rotate trackball 90 degrees, then apply threshold temp layer
    input-processors = <&zip_xy_transform INPUT_TRANSFORM_XY_SWAP>,
                       <&zip_xy_transform INPUT_TRANSFORM_X_INVERT>,
                       <&zip_threshold_temp_layer 1 500>;  // Layer 1, 500ms timeout

    // Scroller configuration for layer 6
    scroller {
        layers = <6>;
        input-processors = <&zip_xy_transform INPUT_TRANSFORM_XY_SWAP>,
                           <&zip_xy_transform INPUT_TRANSFORM_Y_INVERT>;
    };
};
```

## How It Works

1. **Movement Accumulation**: The processor accumulates the distance traveled by the trackball using a fast approximation algorithm
2. **Threshold Check**: When accumulated distance reaches `activation-threshold`, the layer activates
3. **Layer Active**: Once active, the trackball works normally on that layer
4. **Auto-deactivation**: After the timeout period (specified in runtime parameters) with no movement, the layer deactivates
5. **Reset**: The accumulated distance resets when:
   - The layer deactivates (timeout expires)
   - A keyboard key is pressed (unless that position is in `excluded-positions`)

### Distance Calculation

The processor uses an approximation formula for efficiency:
```
distance ≈ max(|dx|, |dy|) + min(|dx|, |dy|) / 2
```

This is faster than true Euclidean distance (`sqrt(dx² + dy²)`) while providing reasonable accuracy for threshold detection.

## Troubleshooting

### Layer Never Activates

1. **Check threshold value**: Try setting `activation-threshold = <0>` to test if the processor works at all
2. **Verify layer configuration**: Ensure the layer number in your keymap matches the runtime parameter
3. **Check build logs**: Look for errors related to `zmk-input-processor-threshold-temp-layer`

### Layer Activates Too Easily

- **Increase threshold**: Try `activation-threshold = <200>` or higher
- **Add idle requirement**: Set `require-prior-idle-ms = <200>` to prevent activation immediately after typing

### Layer Activates Too Slowly

- **Decrease threshold**: Try `activation-threshold = <50>` or lower
- **Check trackball sensitivity**: Ensure your trackball driver is reporting movement correctly

### Build Errors

**Error: `DT_HAS_ZMK_INPUT_PROCESSOR_THRESHOLD_TEMP_LAYER_ENABLED` not defined**
- Make sure you've defined the device tree node in your overlay file
- Verify the `compatible` string is exactly `"zmk,input-processor-threshold-temp-layer"`

**Error: Wrong number of parameters**
- This processor uses **2 parameters** (layer, timeout), not 3
- Device properties like `activation-threshold` go in the node definition, not runtime parameters

## Example Configurations

### Minimal (behaves like standard temp-layer)
```dts
/ {
    zip_threshold_temp_layer: input_processor_threshold_temp_layer {
        compatible = "zmk,input-processor-threshold-temp-layer";
        #input-processor-cells = <2>;
        activation-threshold = <0>;  // No threshold
    };
};

&trackball_listener {
    input-processors = <&zip_threshold_temp_layer 1 500>;
};
```

### Moderate threshold
```dts
/ {
    zip_threshold_temp_layer: input_processor_threshold_temp_layer {
        compatible = "zmk,input-processor-threshold-temp-layer";
        #input-processor-cells = <2>;
        activation-threshold = <100>;  // 100 pixels
    };
};

&trackball_listener {
    input-processors = <&zip_threshold_temp_layer 1 500>;
};
```

### Deliberate activation with idle requirement
```dts
/ {
    zip_threshold_temp_layer: input_processor_threshold_temp_layer {
        compatible = "zmk,input-processor-threshold-temp-layer";
        #input-processor-cells = <2>;
        activation-threshold = <200>;       // 200 pixels
        require-prior-idle-ms = <300>;      // 300ms after last keystroke
        excluded-positions = <40 41 42>;    // Don't deactivate for these keys
    };
};

&trackball_listener {
    input-processors = <&zip_threshold_temp_layer 2 1000>;  // Layer 2, 1 second timeout
};
```

## Real-World Example

This configuration is from the moNa2 keyboard with PAW3222 trackball:

```dts
#include "mona2.dtsi"
#include <input/processors.dtsi>
#include <zephyr/dt-bindings/input/input-event-codes.h>

/ {
    zip_threshold_temp_layer: input_processor_threshold_temp_layer {
        compatible = "zmk,input-processor-threshold-temp-layer";
        #input-processor-cells = <2>;
        activation-threshold = <200>;  // Auto-mouse after 200px movement
        require-prior-idle-ms = <0>;
        excluded-positions = <>;
    };
};

&trackball_central_listener {
    status = "okay";
    device = <&trackball_central>;

    // Rotate 90° clockwise + auto-mouse layer
    input-processors = <&zip_xy_transform INPUT_TRANSFORM_XY_SWAP>,
                       <&zip_xy_transform INPUT_TRANSFORM_X_INVERT>,
                       <&zip_threshold_temp_layer 1 500>;

    scroller {
        layers = <6>;
        input-processors = <&zip_xy_transform INPUT_TRANSFORM_XY_SWAP>,
                           <&zip_xy_transform INPUT_TRANSFORM_Y_INVERT>;
    };
};
```

## Contributing

Issues and pull requests are welcome! Please test thoroughly before submitting.

## License

MIT License - same as ZMK

## Credits

- Based on ZMK's standard `input-processor-temp-layer`
- Developed for the moNa2 keyboard with PAW3222 trackball
