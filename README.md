# TDMA Radio MVP

This folder stores the initial C code skeleton for the STM32F401RE + CC1101 TDMA experiment.

## Roles

- Windows desktop: TDMA controller and operator console.
- STM32 bridge: USB CDC bridge, CC1101 SPI owner, and precise TDMA timing master.
- Raspberry Pi: synchronized gateway with two CC1101 modules.

## File Map

### Project root

- `CMakeLists.txt`: Builds the shared TDMA library and the desktop/Raspberry test executables.
- `README.md`: High-level build, run, RF preset, packet shape, and timing notes.
- `FUNCTIONS.md`: Korean function-by-function explanation for the current MVP code.
- `rssi_log.csv`: Local RSSI logger output sample/data file. Treat as generated experiment data, not source code.

### Shared code

- `common/protocol.c`: Encodes and decodes TDMA payloads, calculates the software CRC16, and wraps TDMA payloads into CC1101 variable-length packets.
- `common/tdma.c`: Maintains the local TDMA clock, beacon synchronization, slot lookup, and guard-time checks.
- `common/tdma_runtime.c`: Platform-independent TDMA runtime state machine for slot start, channel switching, TX/RX actions, settling, guard, and GDO0 completion events.
- `common/cc1101_regs.c`: Stores the shared RF Studio CC1101 register preset and PA table.
- `common/radio_metrics.c`: Converts CC1101 RSSI bytes to dBm and estimates rough distance from RSSI.
- `common/node_table.c`: Tracks TDMA node records used by the MVP node table API.

### Public headers

- `include/config.h`: Project-wide constants such as TDMA timing, node addresses, Raspberry SPI devices, bridge baud rate, and RF parameters.
- `include/protocol.h`: TDMA packet types, packet structures, beacon payload structure, and protocol encode/decode declarations.
- `include/tdma.h`: TDMA clock state and timing/synchronization function declarations.
- `include/tdma_runtime.h`: TDMA runtime state/action declarations used by STM32 timer and GDO0 integration.
- `include/cc1101_regs.h`: CC1101 register addresses plus shared RF preset declarations.
- `include/radio_metrics.h`: RSSI conversion and distance-estimation declarations.
- `include/node_table.h`: Node table data structures and function declarations.
- `include/radio_link.h`: Generic radio-link interface shape for future platform-independent radio handling.
- `include/cc1101_linux.h`: Raspberry/Linux CC1101 driver interface.
- `include/spi_linux.h`: Linux spidev wrapper interface used by the Raspberry CC1101 driver.
- `include/serial_win.h`: Windows serial-port wrapper interface.
- `include/stm32_bridge_link.h`: Desktop-side USB CDC bridge command/event interface.
- `include/cc1101_stm32.h`: STM32 CC1101 driver interface for Cube/HAL integration.
- `include/usb_cdc_bridge.h`: STM32-side USB CDC bridge interface.
- `include/stm32_raw_tx_demo.h`: STM32 raw transmit demo entry point declaration.

### Raspberry code

- `raspberry/main_raspberry.c`: Raspberry packet-test executable. Uses COMM2 to transmit range-test packets and COMM1 to receive them.
- `raspberry/rx_logger.c`: Raspberry RSSI logger executable. Listens on COMM1 and writes decoded packet/RSSI/LQI data to CSV.
- `raspberry/cc1101_linux.c`: Linux CC1101 SPI driver logic: reset, register preset, RX start, TX packet, RX FIFO polling.
- `raspberry/spi_linux.c`: Thin Linux spidev open/transfer/close wrapper.

### Desktop code

- `desktop_win/main_desktop.c`: Windows desktop master executable. Sends TDMA beacons through the STM32 bridge and logs received packets.
- `desktop_win/stm32_bridge_link.c`: Encodes/decodes USB CDC bridge frames between the desktop and STM32.
- `desktop_win/serial_win.c`: Windows COM port open/read/write/close implementation.

### STM32 bridge code

- `stm32_bridge/STM32_SETUP.md`: CubeIDE setup notes and integration points for the STM32 bridge.
- `stm32_bridge/main_stm32.c`: Placeholder STM32 firmware main loop documenting intended timing ownership.
- `stm32_bridge/cc1101.c`: STM32/HAL-side CC1101 SPI control implementation.
- `stm32_bridge/usb_cdc_bridge.c`: STM32-side USB CDC bridge command parser and event sender.
- `stm32_bridge/stm32_raw_tx_demo.c`: STM32 raw CC1101 transmit demo code.

### Analysis code

- `analysis/calibration_config.json`: Experiment-tunable RSSI, Apollonius, and EKF observation settings. Edit this after collecting real data.
- `analysis/analyze_rssi.py`: Converts RSSI logs into normalized power ratios, drop events, Apollonius candidates, and EKF-ready observations.
- `analysis/sample_log.csv`: Small 3-anchor sample log for checking the analysis pipeline before hardware data is ready.

## Build And Run

### Raspberry packet test

Raspberry sends continuous range-test packets from COMM2 to COMM1 and estimates distance from CC1101 RSSI at the current 433.92 MHz PoC preset.

```bash
cd link16-tdma
cmake -S . -B build
cmake --build build --target tdma_raspberry
./build/tdma_raspberry
```

Run a fixed number of packets:

```bash
./build/tdma_raspberry 100
```

Output includes raw RSSI, converted dBm, LQI, and a rough free-space distance estimate:

```text
seq=1 addr=0x00 rssi_raw=0x.. rssi=-.. dBm lqi=0x.. est_distance=.. m decode=0 payload="range-ping:1"
```

The distance value is an RF estimate, not a measurement. Indoor reflections, antenna direction, body blocking, and breadboard wiring can move the estimate a lot.

If `/dev/spidev0.0` or `/dev/spidev0.1` requires elevated access:

```bash
sudo ./build/tdma_raspberry
```

### Raspberry RSSI logger

Raspberry listens on COMM1 (`/dev/spidev0.0`, CE0) using the shared CC1101 RF preset, including `CHANNR=0x05`.
It writes received packet bytes, RSSI, and LQI to CSV for later TDMA and distance-estimation work.

```bash
cd link16-tdma
cmake -S . -B build
cmake --build build --target tdma_raspberry_rx_logger
./build/tdma_raspberry_rx_logger 2 100
```

Arguments:

```text
tdma_raspberry_rx_logger [node_no] [packet_count]
```

`packet_count=0` or omitted means run continuously. `node_no` selects the receiver profile and CSV path inside the program:

```text
1 = master  0x21 -> rssi_log_master.csv
2 = anchor1 0x22 -> rssi_log_anchor1.csv
3 = anchor2 0x23 -> rssi_log_anchor2.csv
```

The logger does not estimate distance; it stores RSSI/LQI source data first.

Logger CSV columns:

```text
timestamp,receiver_id,receiver_role,addr,tx_node_id,tx_role,seq,frame_no,slot_no,slot_role,channel,rssi_raw,rssi_dbm,lqi,decode,payload_text,payload_hex
```

### RSSI post-processing and calibration options

After collecting RSSI logs, tune analysis settings in:

```text
analysis/calibration_config.json
```

The values in this file are initial analysis parameters, not final measured constants. Update them after baseline RSSI/PDR measurements:

```json
{
  "rssi_noise_floor_dbm": -104.5,
  "rssi_drop_threshold_db": -2.4,
  "rssi_moving_avg_window": 5,
  "ekf_process_noise": 0.08,
  "ekf_measurement_noise": 1.2
}
```

Run the sample analysis:

```bash
python analysis/analyze_rssi.py
```

Run analysis on a measured logger CSV:

```bash
python analysis/analyze_rssi.py --input rssi_log.csv --config analysis/calibration_config.json --out-dir analysis_out
```

Generated files:

```text
analysis_out/normalized_rssi.csv
analysis_out/drop_events.csv
analysis_out/apollonius_candidates.csv
analysis_out/ekf_observations.csv
analysis_out/summary.json
```

Run the EKF trajectory tracking:

```bash
# Build the tracker
cmake --build build --target tdma_ekf_tracker

# Run the tracker (arguments: [input_observations_csv] [output_trajectory_csv])
./build/tdma_ekf_tracker analysis_out/ekf_observations.csv analysis_out/ekf_trajectory.csv
```

Generated trajectory:

```text
analysis_out/ekf_trajectory.csv
```

For 3-anchor normalization, the preferred measured CSV should include `receiver_id`. Existing logs without `receiver_id` are still accepted, but they are treated as if all rows came from `default_receiver_id` in `calibration_config.json`.

### Desktop master

Windows desktop master sends periodic TDMA beacons through the STM32 USB CDC bridge and writes a CSV log.
When STM32 sends RX events, the desktop log includes CC1101 RSSI/LQI and a rough distance estimate.

```powershell
cd link16-tdma
cmake -S . -B build
cmake --build build --target tdma_desktop
.\build\Debug\tdma_desktop.exe COM3 100 desktop_master_log.csv
```

Arguments:

```text
tdma_desktop.exe <COM port> <frame count> <log csv>
```

Example:

```text
tdma_desktop.exe COM3 100 desktop_master_log.csv
```

RX log example:

```text
RX_OK frame=2 addr=0x00 type=4 src=0x10 dst=0xff packet_frame=2 slot=2 payload_len=12 rssi_raw=0x9a rssi=-49.0 dBm lqi=0x80 est_distance=1.42 m
```

Desktop CSV columns:

```text
timestamp,event,frame,receiver_id,receiver_role,tx_node_id,tx_role,slot_no,slot_role,channel,rssi_raw,rssi_dbm,lqi,detail
```

### STM32 bridge

STM32CubeIDE setup and code insertion points are documented in:

```text
stm32_bridge/STM32_SETUP.md
```

## RF Preset

The default CC1101 register table is stored in `common/cc1101_regs.c`.

It is based on the project log:

- `Projects/Link16-TDMA/Logs/2026-05-13 RF Studio 기반 CC1101 세팅 정리.md`

Current preset:

- Carrier frequency: 433.919830 MHz, used as the current 433.92 MHz COTS PoC setting
- Modulation: 2-FSK
- Data rate: 37.9868 kBaud
- Packet mode: variable length
- Address check: enabled
- CRC: enabled
- PA table first entry: `0x85` for 5 dBm

Hardware-aligned TDMA constants:

- Slot period: 10.00 ms
- Payload TX/RX window: 8.00 ms
- PLL/state settling margin: 1.20 ms
- Guard time: 0.80 ms
- STM32 SPI target: 6.5 MHz, SPI mode 0, software CSN
- Logical node set: master anchor `0x21`, anchor 1 `0x22`, anchor 2 `0x23`, aircraft/mobile node `0x31`
- Slot map: slot 0 master beacon, slot 1 aircraft TX, slot 2 anchor 1 report, slot 3 anchor 2 report
- FHSS policy stub: `tdma_channel_for_slot(frame, slot)` maps each frame/slot to a configured CC1101 channel index
- ATPC policy stub: `tdma_tx_power_for_margin(margin_db)` adjusts the default TX power within configured min/max limits
- Runtime state machine: `tdma_runtime_tick()` emits `SET_CHANNEL`, `START_TX`, `START_RX`, `STOP_RADIO`, and `LOG_RX` actions; `tdma_runtime_on_gdo0_edge()` is the GDO0 completion hook for the STM32 EXTI callback

## CC1101 Packet Shape

CC1101 FIFO data uses the RF Studio variable length format:

```text
[length][address][TDMA payload...]
```

The TDMA payload is encoded by `common/protocol.c` and includes:

```text
network_id, type, src, dst, frame_no, slot_no, payload_len, payload, crc16
```

## Timing Model

The OS clocks do not need to match. The STM32 bridge sends beacons on a hardware timer, and the Raspberry Pi synchronizes its local monotonic clock from beacon receive time.

The desktop may change policy and slot maps, but it should not be trusted for sub-frame timing.
