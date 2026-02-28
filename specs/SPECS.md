# myproxy

High-performance TCP proxy with libev.

## Features

- Bidirectional data forwarding
- Backpressure handling (32KB buffer per direction)
- Graceful TCP half-close
- Event-driven (O(1) per connection)

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
```

| Option            | Description                    |
|-------------------|--------------------------------|
| -l, --listen-addr | Listen address (host:port)     |
| -b, --backend-addr| Backend address (host:port)    |
| -v, --verbose     | Show connection stats          |
| -vv               | Show detailed I/O operations   |
| -V, --version     | Show version                   |
| -h, --help        | Show help                      |

### Examples

```bash
# Basic proxy
./myproxy -l 0.0.0.0:8080 -b 127.0.0.1:8000

# With verbose logging
./myproxy -l 0.0.0.0:8080 -b backend.local:80 -vv
```

## Logging

| Flag  | Level   | Output                        |
|-------|---------|-------------------------------|
| (none)| INFO    | Connection events, statistics |
| -v    | DEBUG   | Traffic stats with rates      |
| -vv   | TRACE   | Per-read/write operations     |

## Build

```bash
mkdir build && cd build
cmake ..
make
```

## Dependencies

- libev 4.33+ (third-party/)
- C99, CMake 3.10+
