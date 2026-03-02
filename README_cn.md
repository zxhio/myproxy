# myproxy

中文 | [English](README.md)

轻量级 TCP 代理，资源占用极低。

## 特性

- **零拷贝**: Linux 上使用 `splice()` 实现内核空间数据传输
- **事件驱动**: 基于 libev 的高效 I/O 多路复用
- **静态链接**: 无运行时外部依赖

## 资源占用

- **CPU**: 事件驱动，每连接 O(1) 复杂度
- **二进制**: ~760KB 静态二进制
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
| -v, --verbose    | 显示连接统计             |
| -vv              | 显示详细 I/O 操作        |
| -h, --help       | 显示帮助                 |

### 示例

```bash
# 单个代理 (命令行)
./myproxy -l 0.0.0.0:8080 -b 127.0.0.1:8000

# 多个代理 (配置文件)
cat > myproxy.conf << EOF
verbose=1
0.0.0.0:8080,127.0.0.1:8000
0.0.0.0:8081,127.0.0.1:8001
EOF
./myproxy -c myproxy.conf
```

### 配置文件格式

```bash
# 全局选项 (key=value)
verbose=1               # 0=安静, 1=信息, 2=调试

# 代理配置 (监听,后端)
0.0.0.0:8080,127.0.0.1:8000
0.0.0.0:8081,127.0.0.1:8001
```
