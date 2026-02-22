# ethercat-cli

Command-line utility that enumerates EtherCAT slaves, reads their PDO mappings via CoE SDO, and dumps live PDO data. Built with [SOEM v2.0.0](https://github.com/OpenEtherCATsociety/SOEM).

## Requirements

### All platforms

| Tool | Minimum version | Notes |
|------|----------------|-------|
| CMake | 3.20 | |
| Conan | 2.x | `pip install conan` |
| C compiler | C11 support | GCC, Clang, or MSVC |

### Linux

- **Root or `CAP_NET_RAW`** — SOEM opens a raw `AF_PACKET` socket directly; no extra libraries needed.

### Windows

- **Visual Studio 2022** (v145 toolset) — the Conan profile targets MSVC.
- **Npcap** (runtime driver) — install from [npcap.com](https://npcap.com). The Npcap SDK headers and import libraries are fetched automatically by Conan (`npcap/1.70`); only the runtime driver needs to be installed manually.
- **Run as Administrator** — Npcap requires elevated privileges to open a raw adapter handle.

## Building

### First-time setup (any platform)

```bash
# 1. Detect and save your compiler profile (only needed once)
make profile

# 2. Build the SOEM library, install it into the Conan cache,
#    generate the consumer toolchain, then compile the app
make rebuild
```

### Incremental build

After the first setup, recompile the app only (no Conan steps):

```bash
make build
```

To force a full clean and rebuild from scratch:

```bash
make rebuild
```

### Build targets

| Target | Description |
|--------|-------------|
| `make` / `make build` | Configure (idempotent) and compile |
| `make setup` | Build/cache SOEM and generate Conan toolchain |
| `make clean` | Remove the `build/` directory |
| `make rebuild` | `clean` + `setup` + `build` |
| `make profile` | Detect and write a default Conan profile |

## Finding the right network interface

The tool includes a built-in `--list` command that prints all usable interfaces in the exact format required for your platform.

### Linux

```bash
sudo ./build/soem-pdo-dump --list
```

Example output:

```
Available network interfaces (pass the interface name to this tool):

  Interface : eth0   [UP]
  Interface : enp3s0 [UP]
  Interface : wlan0  [UP]
```

Pick the wired adapter connected to your EtherCAT hardware (avoid Wi-Fi and loopback). You can also use `ip link show` for more detail.

### Windows

> **Important:** On Windows, SOEM uses Npcap to open raw Ethernet frames. Npcap does **not** accept friendly adapter names like `Ethernet 10` — it requires the internal `\Device\NPF_{GUID}` device path.

Open **Command Prompt or PowerShell as Administrator**, then run:

```cmd
.\build\Release\soem-pdo-dump.exe --list
```

Example output:

```
Available network interfaces (pass the device name to this tool):

  Device : \Device\NPF_{E905FC24-284B-4BCE-9E1C-DF682422A1F2}
  Desc   : ASIX AX88179A USB 3.2 Gen1 to Gigabit Ethernet Adapter #3

  Device : \Device\NPF_{5219458A-688A-47D7-8421-407DA17F8D16}
  Desc   : Realtek USB GbE Family Controller #2
```

Copy the `Device` string for the adapter physically connected to your EtherCAT network and pass it as the interface argument.

## Running

### Linux

```bash
# List interfaces
sudo ./build/soem-pdo-dump --list

# Run
sudo ./build/soem-pdo-dump <interface>

# Example
sudo ./build/soem-pdo-dump eth0
```

Or grant the capability to avoid `sudo`:

```bash
sudo setcap cap_net_raw+ep ./build/soem-pdo-dump
./build/soem-pdo-dump --list
./build/soem-pdo-dump eth0
```

### Windows

Open **Command Prompt or PowerShell as Administrator**, then:

```cmd
:: List interfaces to find your device path
.\build\Release\soem-pdo-dump.exe --list

:: Run with the NPF device path shown by --list
.\build\Release\soem-pdo-dump.exe \Device\NPF_{E905FC24-284B-4BCE-9E1C-DF682422A1F2}
```

## Troubleshooting

| Symptom | Likely cause |
|---------|-------------|
| `interface X could not open with pcap` (Windows) | Using a friendly name instead of `\Device\NPF_{GUID}` — run `--list` |
| `Failed to initialize EtherCAT` | Wrong interface, or not running as Administrator (Windows) / root (Linux) |
| `No EtherCAT slaves found` | Cable unplugged, slave unpowered, or wrong adapter selected |
| Working counter mismatch | Cabling issue or slave not fully operational |
| `cannot open include file: pcap.h` (Windows build) | Npcap runtime not installed, or `make setup` not run |
