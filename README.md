# myproxy

[中文](README_cn.md) | English

Lightweight TCP proxy with extremely low resource footprint.

## Features

- **Zero-copy**: Uses `splice()` on Linux for kernel-space data transfer
- **Event-driven**: Built with libev for efficient I/O multiplexing
- **Log rotation**: Size-based log file rotation with configurable retention
- **Statically linked**: No external runtime dependencies

## Resource Usage

- **CPU**: Event-driven, O(1) per connection
- **Binary**: ~920KB static binary
- **Data Transfer**: Zero-copy on Linux via `splice()`, traditional read/write on macOS

## Build

Requires CMake and a C compiler.

```bash
make build                    # current platform
make build-linux-arm64        # cross-compile for Linux ARM64
```

## Pack

```bash
make pack                     # build + package for current platform
make pack-all                 # package all platforms
```

Output tarballs are written to `dist/`.

## Usage

```bash
./myproxy -l <listen_addr> -b <backend_addr> [OPTIONS]
./myproxy -c <config_file>
```

| Option            | Description                    |
|-------------------|--------------------------------|
| -c, --config      | Config file with proxy configs |
| -l, --listen-addr | Listen address (ip:port)     |
| -b, --backend-addr| Backend address (ip:port)    |
| -L, --log-file    | Log file path                  |
| -v, --verbose     | Show connection stats          |
| -vv               | Show detailed I/O operations   |
| -h, --help        | Show help                      |

### Examples

```bash
# Single proxy (CLI)
./myproxy -l 0.0.0.0:8080 -b 127.0.0.1:8000

# With log file
./myproxy -l 0.0.0.0:8080 -b 127.0.0.1:8000 -L /var/log/myproxy.log

# Multiple proxies (config file)
./myproxy -c configs/myproxy.conf.example
```

### Config File Format

See [configs/myproxy.conf.example](configs/myproxy.conf.example) for a full example.

```bash
# Global options (key=value)
log-level=info           # error, info (default), debug, trace
log-file=/path/to.log    # Log file path
log-max-size=10          # Max log file size in MB (default: 10)
log-max-files=10         # Number of log files to keep (default: 10)

# Proxy configs (listen,backend)
0.0.0.0:8080,127.0.0.1:8000
0.0.0.0:8081,127.0.0.1:8001
```

### Log Format

```
YYYY/MM/DD HH:MM:SS L message
```

| Level | Description |
|-------|-------------|
| E     | Error       |
| I     | Info        |
| D     | Debug       |
| T     | Trace       |

Example:
```
2026/03/02 16:21:36 I [PROXY#3] 0.0.0.0:8080 -> 127.0.0.1:8000
2026/03/02 16:21:40 D [CLOSE#4] 192.168.1.100:52341 -> 10.0.0.1:80 (Duration: 3.52s)
```

## systemd Deployment (Linux)

```bash
# Install (binary, config, systemd unit)
sudo ./scripts/install-systemd.sh --force --enable --start

# Uninstall (keep config and logs)
sudo ./scripts/install-systemd.sh uninstall

# Uninstall (remove everything)
sudo ./scripts/install-systemd.sh uninstall --purge

# Dry run
sudo ./scripts/install-systemd.sh --dry-run
```

## launchd Deployment (macOS)

```bash
# Install (binary, config, launchd plist)
sudo ./scripts/install-launchd.sh --force --load --start

# Uninstall (keep config and logs)
sudo ./scripts/install-launchd.sh uninstall

# Uninstall (remove everything)
sudo ./scripts/install-launchd.sh uninstall --purge

# Dry run
sudo ./scripts/install-launchd.sh --dry-run
```