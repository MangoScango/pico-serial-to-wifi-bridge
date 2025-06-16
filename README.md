# Pico Serial-to-WiFi Bridge

A Raspberry Pi Pico W project that creates a bidirectional bridge between UART serial communication and TCP over WiFi. This allows you to remotely access and control serial devices through a network connection.

## Features

- **Bidirectional Communication**: Data flows seamlessly between UART and TCP connections
- **WiFi Connectivity**: Connect to your local WiFi network for remote access
- **TCP Server**: Listens on port 8080 for incoming connections
- **Buffered Data Handling**: Efficient circular buffer for UART data with overflow protection
- **Connection Management**: Automatic timeout and health monitoring
- **Single Client Support**: Maintains one active TCP connection at a time

## Hardware Requirements

- Raspberry Pi Pico W
- Serial device to bridge (connected to UART1)

## Pin Configuration

- **UART1 TX**: GPIO 4
- **UART1 RX**: GPIO 5
- **Baud Rate**: 115200
- **Data Format**: 8N1 (8 data bits, no parity, 1 stop bit)

## Software Requirements

- Raspberry Pi Pico SDK
- CMake
- ARM GCC toolchain
- OpenOCD (for flashing)
- Picotool (optional, for USB loading)

### Nix Users

This project includes a `flake.nix` for easy development environment setup:

```bash
nix develop
```

## Build Instructions

1. **Set up environment variables** (create a `.env` file):
   ```bash
   WIFI_SSID="YourWiFiNetwork"
   WIFI_PASSWORD="YourWiFiPassword"
   ```

2. **Configure the project**:
   ```bash
   cmake -S . -B build
   ```

3. **Build the project**:
   ```bash
   cmake --build build
   ```

4. **Flash to Pico W**:
   - **Method 1 - OpenOCD** (requires debug probe):
     ```bash
     openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" -c "program build/pico-serial-to-wifi-bridge.elf verify reset exit"
     ```
   
   - **Method 2 - USB Boot** (hold BOOTSEL while connecting):
     ```bash
     picotool load build/pico-serial-to-wifi-bridge.uf2 -fx
     ```

## VS Code Tasks

If using VS Code, the following tasks are available:

- **Configure**: Set up CMake build directory
- **Build**: Compile the project
- **Flash**: Program the device via OpenOCD
- **Clean**: Remove build directory
- **Reset**: Reset the Pico W

## Usage

1. **Power on the Pico W** - It will automatically connect to your configured WiFi network
2. **Monitor the serial output** (UART0 at 115200 baud) to see the assigned IP address
3. **Connect to the TCP server** on port 8080:
   ```bash
   socat -d -d PTY,link=/tmp/picolink,raw TCP:{ip}:8080
   ```
4. **Send/receive data** - Any data written to /tmp/picolink will be forwarded to UART1, and vice versa

## Monitoring and Debugging

The device provides detailed logging via UART0 including:
- WiFi connection status and IP address
- Client connection/disconnection events
- Data transfer statistics
- Buffer overflow warnings
