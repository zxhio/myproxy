# myproxy

[中文](README_cn.md) | English

Lightweight TCP proxy with extremely low resource footprint.

## Features

- **Zero-copy**: Uses `splice()` on Linux for kernel-space data transfer
- **Event-driven**: Built with libev for efficient I/O multiplexing
- **Statically linked**: No external runtime dependencies

## Resource Usage

- **CPU**: Event-driven, O(1) per connection
- **Binary**: ~760KB static binary
- **Data Transfer**: Zero-copy on Linux via `splice()`, traditional read/write on macOS

## Build

```bash
mkdir build && cd build
cmake ..
make
```

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
| -v, --verbose     | Show connection stats          |
| -vv               | Show detailed I/O operations   |
| -h, --help        | Show help                      |

### Examples

```bash
# Single proxy (CLI)
./myproxy -l 0.0.0.0:8080 -b 127.0.0.1:8000

# Multiple proxies (config file)
cat > myproxy.conf << EOF
verbose=1
0.0.0.0:8080,127.0.0.1:8000
0.0.0.0:8081,127.0.0.1:8001
EOF
./myproxy -c myproxy.conf
```

### Config File Format

```bash
# Global options (key=value)
verbose=1               # 0=quiet, 1=info, 2=debug

# Proxy configs (listen,backend)
0.0.0.0:8080,127.0.0.1:8000
0.0.0.0:8081,127.0.0.1:8001
```
