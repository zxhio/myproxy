# myproxy

中文 | [English](README.md)

轻量级 TCP 代理，资源占用极低。

## 特性

- **零拷贝**: Linux 上使用 `splice()` 实现内核空间数据传输
- **事件驱动**: 基于 libev 的高效 I/O 多路复用
- **日志轮转**: 支持按大小轮转日志文件，可配置保留数量
- **静态链接**: 无运行时外部依赖

## 资源占用

- **CPU**: 事件驱动，每连接 O(1) 复杂度
- **二进制**: ~920KB 静态二进制
- **数据传输**: Linux 使用 `splice()` 零拷贝，macOS 使用传统 read/write

## 构建

```bash
mkdir build && cd build
cmake ..
make
```

## 使用

```bash
./myproxy -l <监听地址> -b <后端地址> [选项]
./myproxy -c <配置文件>
```

| 选项             | 说明                     |
|------------------|--------------------------|
| -c, --config     | 配置文件，支持多个代理   |
| -l, --listen-addr| 监听地址 (ip:port)       |
| -b, --backend-addr| 后端地址 (ip:port)      |
| -L, --log-file   | 日志文件路径             |
| -v, --verbose    | 显示连接统计             |
| -vv              | 显示详细 I/O 操作        |
| -h, --help       | 显示帮助                 |

### 示例

```bash
# 单个代理 (命令行)
./myproxy -l 0.0.0.0:8080 -b 127.0.0.1:8000

# 带日志文件
./myproxy -l 0.0.0.0:8080 -b 127.0.0.1:8000 -L /var/log/myproxy.log

# 多个代理 (配置文件)
cat > myproxy.conf << EOF
log-level=info           # error, info (默认), debug, trace
log-file=myproxy.log     # 日志文件路径
log-max-size=10          # 单个日志文件最大大小 MB (默认: 10)
log-max-files=10         # 保留的日志文件数量 (默认: 10)
0.0.0.0:8080,127.0.0.1:8000
0.0.0.0:8081,127.0.0.1:8001
EOF
./myproxy -c myproxy.conf
```

### 配置文件格式

```bash
# 全局选项 (key=value)
log-level=info           # error, info (默认), debug, trace
log-file=/path/to.log    # 日志文件路径
log-max-size=10          # 单个日志文件最大大小 MB (默认: 10)
log-max-files=10         # 保留的日志文件数量 (默认: 10)

# 代理配置 (监听,后端)
0.0.0.0:8080,127.0.0.1:8000
0.0.0.0:8081,127.0.0.1:8001
```

### 日志格式

```
YYYY/MM/DD HH:MM:SS L 消息
```

| 等级 | 说明   |
|------|--------|
| E    | 错误   |
| I    | 信息   |
| D    | 调试   |
| T    | 追踪   |

示例:
```
2026/03/02 16:21:36 I [PROXY#3] 0.0.0.0:8080 -> 127.0.0.1:8000
2026/03/02 16:21:40 D [CLOSE#4] 192.168.1.100:52341 -> 10.0.0.1:80 (Duration: 3.52s)
```
