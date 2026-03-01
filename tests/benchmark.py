#!/usr/bin/env python3
"""TCP proxy load testing tool."""

import argparse
import socket
import time
import threading
import sys


def print_usage():
    """Print usage information."""
    print("""Usage: benchmark.py [OPTIONS]

Options:
  -H, --host ADDR          Server host (default: 127.0.0.1)
  -p, --port PORT          Server port (default: 8080)
  -c, --connections NUM    Number of concurrent connections (default: 10)
  -d, --duration SECONDS   Test duration in seconds (default: 10)
  -s, --size BYTES         Send buffer size in bytes (default: 4096)
  -v, --verbose            Show per-connection statistics
  -h, --help               Show this help message

Examples:
  # Basic test with 10 connections for 10 seconds
  ./benchmark.py -H 127.0.0.1 -p 8080

  # Load test with 100 connections, 30s duration, 64KB buffer
  ./benchmark.py -H 127.0.0.1 -p 8080 -c 100 -d 30 -s 65536

  # Test with verbose output
  ./benchmark.py -H 127.0.0.1 -p 8080 -c 50 -d 20 -v
""")


def send_data(host, port, size, duration, client_id):
    """Send data to the server and measure throughput."""
    start_time = time.time()
    total_sent = 0
    total_recv = 0

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((host, port))

        # Prepare data buffer
        data = b'x' * size

        while (time.time() - start_time) < duration:
            # Send data
            sent = sock.send(data)
            total_sent += sent

            # Try to receive response
            try:
                sock.settimeout(0.1)
                while True:
                    recv_data = sock.recv(size)
                    if not recv_data:
                        break
                    total_recv += len(recv_data)
            except socket.timeout:
                pass

        sock.close()

    except Exception as e:
        print(f"Client {client_id}: Error - {e}", file=sys.stderr)
        return 0, 0

    elapsed = time.time() - start_time
    sent_rate = total_sent / elapsed if elapsed > 0 else 0
    recv_rate = total_recv / elapsed if elapsed > 0 else 0

    return sent_rate, recv_rate


def format_size(bytes_per_sec):
    """Format bytes per second to human readable."""
    if bytes_per_sec < 1024:
        return f"{bytes_per_sec:.2f} B/s"
    elif bytes_per_sec < 1024 * 1024:
        return f"{bytes_per_sec / 1024:.2f} KB/s"
    elif bytes_per_sec < 1024 * 1024 * 1024:
        return f"{bytes_per_sec / (1024 * 1024):.2f} MB/s"
    else:
        return f"{bytes_per_sec / (1024 * 1024 * 1024):.2f} GB/s"


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("-H", "--host", default="127.0.0.1", help="Server host")
    parser.add_argument("-p", "--port", type=int, default=8080, help="Server port")
    parser.add_argument("-c", "--connections", type=int, default=10, help="Number of concurrent connections")
    parser.add_argument("-d", "--duration", type=int, default=10, help="Test duration in seconds")
    parser.add_argument("-s", "--size", type=int, default=4096, help="Send buffer size in bytes")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    parser.add_argument("-h", "--help", action="store_true", help="Show help message")

    args = parser.parse_args()

    if args.help:
        print_usage()
        return 0

    print(f"Starting benchmark: {args.connections} connections, {args.duration}s duration")
    print(f"Target: {args.host}:{args.port}, Buffer size: {args.size} bytes")
    print("-" * 60)

    threads = []
    results = []

    for i in range(args.connections):
        t = threading.Thread(target=lambda cid=i: results.append(send_data(args.host, args.port, args.size, args.duration, cid)))
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    if not results:
        print("No successful connections!")
        return 1

    total_sent_rate = sum(r[0] for r in results)
    total_recv_rate = sum(r[1] for r in results)

    print(f"\nResults:")
    print(f"  Total send rate:     {format_size(total_sent_rate)}")
    print(f"  Total recv rate:     {format_size(total_recv_rate)}")
    print(f"  Avg send per conn:   {format_size(total_sent_rate / args.connections)}")
    print(f"  Avg recv per conn:   {format_size(total_recv_rate / args.connections)}")

    if args.verbose:
        print(f"\nPer-connection stats:")
        for i, (sent, recv) in enumerate(results):
            print(f"  Client {i}: {format_size(sent)} sent, {format_size(recv)} recv")

    return 0


if __name__ == "__main__":
    sys.exit(main())
