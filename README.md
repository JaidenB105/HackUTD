# ResilientSense

ResilientSense is an ESP-IDF firmware project for reading an NTC temperature sensor through an ESP32-S3 ADC, tracking recent measurements, and reporting health and historical diagnostic findings over the serial console. A separate native C test suite exercises the history and diagnostic logic on a development computer without requiring ESP-IDF hardware APIs.

## What the application does

The firmware repeats this cycle once per second:

1. Acquire a sensor reading (or generate a deterministic simulated fault).
2. Record the measurement, acquisition time, and history-ingestion time in a fixed-size ring buffer.
3. Update the basic health state with the fault detector.
4. Analyze recent history with the diagnostic engine.
5. Log a diagnostic report when a diagnosis changes or recovery is verified.

The fault detector and diagnostic engine have separate responsibilities. `faultDetect.c` classifies individual readings and tracks `HEALTHY`, `SUSPECT`, or `FAILED` states. `diagnostic_engine.c` analyzes sequences and timing in history to identify deeper patterns. A diagnostic report describes observed evidence and possible causes; it does not assert an unverified physical root cause.

### Diagnostic rules

The default configuration in `diagnostic_config_init()` uses:

| Rule | Default condition |
|---|---|
| ADC saturation | At least 3 consecutive successful acquisitions at or below raw ADC 16, or at or above 4079 (12-bit ADC range 0–4095) |
| Repeated invalid data | At least 3 consecutive successful acquisitions with out-of-range ADC data or a non-finite temperature |
| Communication failure | At least 3 consecutive unsuccessful acquisition statuses |
| Stale data | The newest usable acquisition is at least 5 seconds old |
| Abnormal temperature change | Absolute temperature change per elapsed second is at least 100 °C/s |
| Recovery | At least 3 consecutive valid, non-saturated samples with strictly increasing acquisition timestamps |

These are configurable defaults, not claims about the physical sensor's safe operating limits. The diagnostic engine uses monotonic microsecond timestamps, rejects invalid or out-of-order time intervals for rate calculations, and distinguishes acquisition time from ingestion time. Legacy history samples without an explicit acquisition timestamp fall back to their existing timestamp.

### Reports and fault IDs

Diagnostic reports contain a stable fault ID and name, severity, component, evidence flags, available raw ADC values and status, timing/rate measurements, possible causes, a recommended action, and a simulated-data marker. Current IDs include `F201` (ADC saturation), `F202` (repeated invalid data), `F203` (stale data), `F204` (abnormal temperature change), and `F205` (communication failure). Static strings are provided for descriptions and recommended actions so reports can later be adapted to a structured transport such as JSON.

The report's "confirmed" status refers to the observed diagnostic condition. It does not confirm a wiring fault, sensor defect, or physical disconnection. Communication errors are reported as acquisition failures unless independent evidence proves a disconnection.

### Sensor and hardware model

`sensor.c` configures ADC1 channel 3 at 12-bit resolution with 12 dB attenuation, reads the ADC, and estimates NTC temperature using a beta-model conversion (`BETA = 3950`, reference temperature 298.15 K). The conversion is documented in source as an initial approximation for Wokwi. The Wokwi diagram connects the NTC output to ESP32-S3 GPIO 4, with power at 3.3 V and ground at GND.

**ADC endpoint limitation:** the current sensor driver returns `ESP_ERR_INVALID_RESPONSE` for raw values 0 and 4095. Since saturation detection requires a successful acquisition status, physical endpoint readings returned by this driver are classified as invalid data rather than ADC saturation. The deterministic injected saturation scenarios explicitly provide a successful status to exercise the saturation rule. Adjust the driver if physical endpoint saturation must be diagnosed separately from conversion-invalid data.

## Languages and tools

- **C (C11):** firmware modules, diagnostic rules, history, and native tests.
- **CMake:** ESP-IDF project/component configuration and native test configuration.
- **ESP-IDF:** firmware framework and hardware/FreeRTOS APIs. The checked-in `sdkconfig` targets ESP32-S3 and identifies ESP-IDF 6.2.0.
- **Wokwi:** optional ESP32-S3/NTC simulation described by `diagram.json` and `wokwi.toml`.
- **CMake-compatible native compiler:** MSVC on Windows or GCC/Clang on other supported development environments.

## Repository map

| Path | Purpose |
|---|---|
| `main/main.c` | Firmware entry point, task loop, UART logging, and deterministic fault injection |
| `main/sensor.c`, `main/sensor.h` | ADC setup, sensor acquisition, and temperature conversion |
| `main/faultDetect.c`, `main/faultDetect.h` | Basic per-reading health and fault-state tracking |
| `main/sensor_history.c`, `main/sensor_history.h` | Fixed-size ring buffer of readings and timestamps |
| `main/diagnostic_engine.c`, `main/diagnostic_engine.h` | Configurable historical analysis and diagnostic reports |
| `main/CMakeLists.txt` | ESP-IDF component source registration |
| `tests/native/` | Native CMake project and unit tests |
| `tests/native/mocks/esp_err.h` | Local ESP error definitions used by native tests |
| `diagram.json`, `wokwi.toml` | Wokwi board, sensor wiring, and firmware artifact paths |
| `sdkconfig` | Generated ESP-IDF project configuration |

## Build the firmware

Install ESP-IDF and its supported toolchain for the project target (ESP32-S3), then build from the repository root.

On Windows, open an ESP-IDF PowerShell or Command Prompt with the required environment activated, or use the ESP-IDF export script followed by the build command. For example, from PowerShell:

```powershell
& $env:ComSpec /c 'call "C:\Users\<you>\esp\esp-idf\export.bat" && idf.py build'
```

In an ESP-IDF shell where the environment is already activated:

```console
idf.py build
```

On a POSIX shell, activate the installed ESP-IDF environment first (for example, `source "$IDF_PATH/export.sh"`), then run `idf.py build`.

The application ELF and binary are generated in `build/`; Wokwi is configured to use `build/ResilientSense.elf`. If the firmware build fails because the target or toolchain differs from the checked-in configuration, review the ESP-IDF installation and project `sdkconfig` before changing source files.

## Run on hardware or in Wokwi

### ESP32-S3 hardware

Connect a compatible NTC voltage-divider output to the configured ADC input (ADC1 channel 3 / GPIO 4 for the Wokwi board mapping), then flash and open the serial monitor:

```console
idf.py -p COM<n> flash monitor
```

Replace `COM<n>` with the board's serial port. The configured console baud rate is 115200. Use `Ctrl+]` to exit the ESP-IDF monitor. On POSIX systems, replace the port with the applicable device path, such as `/dev/ttyUSB0`.

### Wokwi

Open the project with the Wokwi extension or simulator. The board and NTC wiring are in `diagram.json`; `wokwi.toml` points to the built ELF and flash arguments. Build the firmware first so those artifacts are present, then start the Wokwi simulation and inspect its serial monitor.

### Deterministic fault injection

Fault injection is currently enabled in `main/main.c` with `ENABLE_FAULT_INJECTION` set to `1`. Scenarios are tied to the one-based loop cycle:

| Cycles | Injected scenario |
|---|---|
| 8–10 | Acquisition timeout |
| 18–20 | Successful acquisition with a non-finite temperature |
| 28–30 | Successful reading with an out-of-range temperature |
| 38–40 | High ADC saturation |
| 48–50 | Low ADC saturation |
| 58–63 | Missing history updates |
| 68–70 | Rapid temperature change |

Injected measurements/reports are marked simulated where applicable. These scenarios are test behavior, not evidence that the physical sensor or wiring has failed. Set `ENABLE_FAULT_INJECTION` to `0` and rebuild to disable them for normal operation.

## Native unit tests

The native suite tests the diagnostic engine, sensor-history ring buffer, and basic fault detector. It uses deterministic inputs and simulated timestamps and does not need a connected board or ESP-IDF.

From the repository root, the requested commands are:

```console
cmake -S tests/native -B tests/native/build
cmake --build tests/native/build
ctest --test-dir tests/native/build --output-on-failure
```

On Windows with the Visual Studio multi-configuration generator, specify the configuration when running CTest:

```powershell
ctest --test-dir tests/native/build -C Debug --output-on-failure
```

The test targets are `fault_detector_tests`, `sensor_history_tests`, and `diagnostic_engine_tests`. The diagnostic suite covers empty and normal histories, high/low saturation thresholds, invalid and communication-failure sequences, stale timeouts and legacy timestamps, identical fresh samples, rate thresholds and timestamp ordering, ring-buffer wraparound, recovery, recommendations, and invalid arguments.

## Verification record

The firmware was built successfully for ESP32-S3 with the installed ESP-IDF 6.2 environment. The native test targets built successfully with the Windows Visual Studio C compiler, and all three CTest suites passed. On that multi-configuration Windows build, CTest was run with `-C Debug`. These checks verify compilation and the listed native behaviors; they do not replace testing on a physical sensor or a live Wokwi session.
