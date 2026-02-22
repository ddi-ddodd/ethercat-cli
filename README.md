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

You must pass the name of the Ethernet adapter that is physically connected to your EtherCAT network.

### Linux

```bash
ip link show
```

Look for an interface that is `UP` and has a link, e.g. `eth0`, `enp3s0`, `ens33`. Avoid `lo` (loopback) and Wi-Fi adapters (`wlan*`).

```bash
# Example output — pick the wired adapter connected to EtherCAT hardware
2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 ...
```

### Windows

Open **PowerShell** or **Command Prompt**:

```powershell
# Shows all adapters with their index, name, and status
Get-NetAdapter | Select-Object Name, InterfaceDescription, Status, MacAddress
```

Or use the older command:

```cmd
ipconfig /all
```

Look for an adapter marked **Up** whose description matches your network card (e.g. "Intel(R) Ethernet Connection"). Note the **Name** column (e.g. `Ethernet`, `Local Area Connection`) — that is what you pass to the tool.

Npcap uses the adapter's friendly name prefixed with `\Device\NPF_` internally, but `soem-pdo-dump` accepts the plain adapter name as shown by `Get-NetAdapter`.

## Running

### Linux

```bash
sudo ./build/soem-pdo-dump <interface>

# Example
sudo ./build/soem-pdo-dump eth0
```

Or grant the capability to avoid `sudo`:

```bash
sudo setcap cap_net_raw+ep ./build/soem-pdo-dump
./build/soem-pdo-dump eth0
```

### Windows

Open **Command Prompt or PowerShell as Administrator**, then:

```cmd
.\build\Release\soem-pdo-dump.exe <interface>

:: Example
.\build\Release\soem-pdo-dump.exe Ethernet
```

## Troubleshooting

| Symptom | Likely cause |
|---------|-------------|
| `Failed to initialize EtherCAT` | Wrong interface name, or missing root/admin privileges |
| `No EtherCAT slaves found` | Cable unplugged, slave unpowered, or wrong adapter selected |
| Working counter mismatch | Cabling issue or slave not fully operational |
| `cannot open include file: pcap.h` (Windows build) | Npcap runtime not installed, or Conan setup not run |
