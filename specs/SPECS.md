# myproxy

High-performance TCP proxy with libev.

## Features

- Bidirectional data forwarding
- Backpressure handling (32KB buffer per direction)
- Graceful TCP half-close
- Event-driven (O(1) per connection)
- Log rotation with configurable size limits

## Architecture

```
client <---> myproxy <---> backend
                |
             [libev]
```

## Event Flow

```
1. Listen on specified address
2. Accept client connection
3. Connect to backend
4. Forward data bidirectionally
5. Close on EOF
```

## libev Integration

- **Location**: `third-party/libev-4.33/`
- **Build**: Static library, compiled with `-w` to suppress warnings
- **Macros**: `EV_STANDALONE` to skip config.h, `EV_USE_EPOLL=1` for Linux

## Connection Close

Supports TCP half-close for graceful shutdown:

```
Direction          Behavior
─────────────────────────────────────────
Client closes      → Forward FIN to backend, continue B→C
Backend closes     → Forward FIN to client, continue C→B
Both closed        → Close connection, report stats
```

## Statistics

On connection close, output:

```
Connection [C]IP:PORT ↔ [B]IP:PORT closed (DURATIONs)
  forward: BYTES (RATE/s), backward: BYTES (RATE/s)
```

| Field    | Description                     |
|----------|---------------------------------|
| DURATION | Connection duration in seconds  |
| BYTES    | Total bytes transferred         |
| RATE     | Bytes per second                |

## Usage

```bash
myproxy -l LISTEN_ADDR -b BACKEND_ADDR [OPTIONS]
myproxy -c CONFIG_FILE
```

| Option            | Description                    |
|-------------------|--------------------------------|
| -c, --config      | Config file with proxy configs |
| -l, --listen-addr | Listen address (host:port)     |
| -b, --backend-addr| Backend address (host:port)    |
| -L, --log-file    | Log file path                  |
| -v, --verbose     | Show connection stats          |
| -vv               | Show detailed I/O operations   |
| -V, --version     | Show version                   |
| -h, --help        | Show help                      |

### Examples

```bash
# Single proxy (CLI)
./myproxy -l 0.0.0.0:8080 -b 127.0.0.1:8000

# With log file
./myproxy -l 0.0.0.0:8080 -b 127.0.0.1:8000 -L /var/log/myproxy.log

# Multiple proxies (config file)
./myproxy -c /etc/myproxy.conf
```

### Config File Format

```
# Global options (key=value)
log-level=info           # error, info (default), debug, trace
log-file=/path/to.log    # Log file path
log-max-size=10          # Max log file size in MB (default: 10)
log-max-files=10         # Number of log files to keep (default: 10)

# Proxy configs (listen,backend)
0.0.0.0:8080,127.0.0.1:8000
0.0.0.0:8081,127.0.0.1:8001
```

## Logging

| Level | Char | Description                    |
|-------|------|--------------------------------|
| ERROR | E    | Errors only                    |
| INFO  | I    | Connection events, statistics  |
| DEBUG | D    | Traffic stats with rates       |
| TRACE | T    | Per-read/write operations      |

### Log Format

```
YYYY/MM/DD HH:MM:SS L message
```

Example:
```
2026/03/02 16:21:36 I [PROXY#3] 0.0.0.0:8080 -> 127.0.0.1:8000
2026/03/02 16:21:37 D [OPEN#4] 192.168.1.100:52341 -> 10.0.0.1:80
2026/03/02 16:21:40 D [CLOSE#4] 192.168.1.100:52341 -> 10.0.0.1:80 (Duration: 3.52s)
2026/03/02 16:21:40 D [STATS#4] FWD: 1.21 KiB (350.43 B/s) | BWD: 2.45 KiB (708.12 B/s)
```

### Log Rotation

When a log file reaches `log-max-size`, it is rotated:
- Current file → `myproxy.log.1`
- `myproxy.log.1` → `myproxy.log.2`
- `myproxy.log.N` → `myproxy.log.N+1`
- Oldest file (`.N` where N = `log-max-files`) is deleted

## Build

```bash
mkdir build && cd build
cmake ..
make
```

## Dependencies

- libev 4.33+ (third-party/)
- C99, CMake 3.10+
