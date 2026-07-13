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

需要 CMake 和 C 编译器。

```bash
make build                    # 当前平台
make build-linux-arm64        # 交叉编译 Linux ARM64
```

## 打包

```bash
make pack                     # 构建 + 打包当前平台
make pack-all                 # 打包所有平台
```

打包输出到 `dist/` 目录。

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
./myproxy -c configs/myproxy.conf.example
```

### 配置文件格式

完整示例见 [configs/myproxy.conf.example](configs/myproxy.conf.example)。

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

## systemd 部署（Linux）

```bash
# 安装（二进制、配置、systemd unit）
sudo ./scripts/install-systemd.sh --force --enable --start

# 卸载（保留配置和日志）
sudo ./scripts/install-systemd.sh uninstall

# 卸载（删除所有文件）
sudo ./scripts/install-systemd.sh uninstall --purge

# 预览操作
sudo ./scripts/install-systemd.sh --dry-run
```

## launchd 部署（macOS）

```bash
# 安装（二进制、配置、launchd plist）
sudo ./scripts/install-launchd.sh --force --load --start

# 卸载（保留配置和日志）
sudo ./scripts/install-launchd.sh uninstall

# 卸载（删除所有文件）
sudo ./scripts/install-launchd.sh uninstall --purge

# 预览操作
sudo ./scripts/install-launchd.sh --dry-run
```
