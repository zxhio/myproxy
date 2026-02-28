# myproxy

中文 | [English](README.md)

轻量级 TCP 代理，资源占用极低。

## 资源占用

- **内存**: 每连接约 64KB
- **CPU**: 事件驱动，每连接 O(1) 复杂度
- **二进制**: 静态链接，无外部依赖

## 构建

```bash
mkdir build && cd build
cmake ..
make
```

## 使用

```bash
./myproxy -l <监听地址> -b <后端地址> [选项]
```

| 选项             | 说明                     |
|------------------|--------------------------|
| -l, --listen-addr| 监听地址 (ip:port)       |
| -b, --backend-addr| 后端地址 (ip:port)      |
| -v, --verbose    | 显示连接统计             |
| -vv              | 显示详细 I/O 操作        |
| -h, --help       | 显示帮助                 |

### 示例

```bash
# 转发本地 8080 到后端 8000
./myproxy -l 0.0.0.0:8080 -b 127.0.0.1:8000

# 带详细日志
./myproxy -l 0.0.0.0:8080 -b backend.local:80 -vv
```
