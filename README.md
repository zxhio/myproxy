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
```

| Option            | Description                    |
|-------------------|--------------------------------|
| -l, --listen-addr | Listen address (ip:port)     |
| -b, --backend-addr| Backend address (ip:port)    |
| -v, --verbose     | Show connection stats          |
| -vv               | Show detailed I/O operations   |
| -h, --help        | Show help                      |

### Examples

```bash
# Forward local 8080 to backend 8000
./myproxy -l 0.0.0.0:8080 -b 127.0.0.1:8000

# With verbose logging
./myproxy -l 0.0.0.0:8080 -b backend.local:80 -vv
```
