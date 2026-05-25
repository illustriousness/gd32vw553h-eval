# GD32VW553H-EVAL 调试、GD SDK WiFi/BLE 移植问题记录

本文记录本工程从 VS Code/GDB 调试修复，到直接接入 `gd_sdk/MSDK`
WiFi/BLE，并完成编译、下载、WiFi 联网和 BLE 收发验证过程中遇到的主要问题与解决方式。

## 当前结论

当前工程保留 RT-Thread 内核调度，但不再走 RT-Thread 的 WiFi/lwIP 框架：

- WiFi/BLE 使用 `gd_sdk/MSDK` 原生代码。
- RT-Thread 只作为系统调度和基础 BSP 环境。
- 已关闭 `RT_USING_WIFI` 和 `RT_USING_LWIP`，避免两套网络栈冲突。
- 固件可以编译、下载、运行。
- WiFi 已能扫描、连接本机热点、获取 IP，并从电脑 ping 通板子。
- BLE 已能广播、电脑连接 GATT、订阅 notify、写入 RX characteristic，并收到板端 echo notify。

## 1. VS Code / GDB 连接问题

### 现象

VS Code 中 `cortex-debug` 配置启动后 GDB 连不上，或者启动时找不到可用的
RISC-V GDB。

### 原因

原配置里调试路径、ELF 路径和 GDB 工具链假设比较固定，但本机环境中没有单独安装
RISC-V 专用 GDB。系统可用的是 `/usr/bin/gdb-multiarch`，它也可以调试 RISC-V ELF。

### 解决方式

- 在 workspace 调试配置里显式指定：
  - `executable`: `${workspaceFolder}/build/rtthread.elf`
  - `serverpath`: `openocd`
  - `configFiles`: `${workspaceFolder}/openocd_gdlink.cfg`
  - `gdbPath`: `/usr/bin/gdb-multiarch`
- 保留并简化 `scripts/riscv-gdb.sh`：
  - 优先使用系统中的 RISC-V GDB。
  - 如果没有，则 fallback 到 `gdb-multiarch`。
  - 如果两者都没有，再给出安装提示。

这样 VS Code 和命令行可以复用同一套 GDB 选择逻辑，减少环境差异。

## 2. 程序启动后直接崩溃

### 现象

程序刚运行就进入异常，GDB 看到和 libc `rand()` / `random()` 相关的访问异常。

### 原因

当前裸机/RT-Thread 启动环境下，picolibc 的部分 libc 实现会访问 TLS。异常现场中
`tp` 寄存器是无效值，例如 `0xdeadbeef`，导致 libc 的 `rand/random/srand/srandom`
路径访问非法地址。

### 解决方式

新增 `gd_sdk/port/rand_compat.c`，提供本工程自己的轻量实现：

- `rand`
- `srand`
- `random`
- `srandom`

这样避开 picolibc TLS 依赖，程序不再在随机数接口处崩溃。

## 3. 直接接入 GD SDK，而不是适配 RT-Thread WLAN 框架

### 目标

用户要求“直接用 GD SDK 中的代码，不要适配 RTT 的框架”。因此本次没有把 GD SDK
WiFi/BLE 包装成 RT-Thread `rt_wlan` 或 `netdev`，而是直接编译和调用 GD SDK MSDK。

### 处理方式

新增 `gd_sdk/SConscript`，把以下内容接入当前工程：

- MSDK platform/util 代码。
- MSDK RTOS wrapper。
- MSDK WiFi manager。
- MSDK lwIP。
- MSDK BLE app/profile。
- WiFi/BLE 预编译库。
- 本工程 port 文件。

新增 `gd_sdk/port/gd_sdk_direct_demo.c`，直接调用 MSDK API：

- `wifi_init()`
- `wifi_wait_ready()`
- `wifi_management_scan()`
- `wifi_management_connect()` / `wifi_management_connect_with_bssid()`
- `ble_init(true)`
- `ble_wait_ready()`
- `ble_datatrans_srv_init()`
- `ble_adv_create()`
- `ble_adv_start()`

新增 `gd_sdk/port/gd_sdk_direct_irq.c`，手动接入 WiFi/BLE IRQ handler，避免直接使用
SDK 自带 `gd32vw55x_it.c` 时覆盖 RT-Thread BSP 中已有的 UART/系统中断处理。

## 4. SDK 编译接入时的兼容问题

### 4.1 raw flash 链接缺少 list inline 符号

#### 现象

链接时报 `list_pick`、`list_pick_last`、`list_next` 等符号缺失。

#### 原因

BSP/SDK 头文件中这些函数以 inline 形式存在，但当前编译组合下没有生成外部符号；
MSDK 某些对象文件又需要外部符号。

#### 解决方式

新增 `gd_sdk/port/slist_inline_compat.c`，为这些 list helper 提供外部实现，满足链接。

### 4.2 cJSON 缺少标准整数类型

#### 现象

`gd_sdk/MSDK/util/src/cJSON.c` 编译时出现 `uintptr_t` / 标准整数类型相关错误。

#### 原因

当前编译环境下该文件没有显式包含 `<stdint.h>`。

#### 解决方式

在 `cJSON.c` 中补充 `#include <stdint.h>`。

### 4.3 lwIP errno 冲突

#### 现象

MSDK lwIP 与 picolibc 的 `errno` 处理发生冲突，可能导致链接或运行时 TLS 相关问题。

#### 原因

SDK lwIP port 中有自定义全局 `errno`，但当前工具链 libc 已经提供 errno，并可能走 TLS。

#### 解决方式

调整 `gd_sdk/MSDK/lwip/lwip-2.2.0/port/lwipopts.h`，并移除
`gd_sdk/MSDK/lwip/lwip-2.2.0/src/api/err.c` 中 SDK 自定义全局 `errno` 的定义，避免和
libc 冲突。

## 5. BLE 支持被宏静默关闭

### 现象

BLE 初始化流程看似接入了，但服务没有按预期注册，广播/GATT 行为不完整。

### 原因

工程中优先包含了 BSP 的 `platform_def.h`，其中 `CFG_BLE_SUPPORT` 默认被注释。
如果只依赖 SDK 默认配置，BLE 相关代码会被条件编译排除。

### 解决方式

在 `gd_sdk/SConscript` 的 `CPPDEFINES` 中显式定义：

- `CFG_BLE_SUPPORT`
- `CFG_RTOS`
- `PLATFORM_OS_RTTHREAD`
- `GD_SDK_KEEP_RTTHREAD_CONSOLE`

同时避免重复定义 BSP 已经定义的 `CFG_WLAN_SUPPORT` / `CFG_COEX`，减少宏重定义 warning。

## 6. BLE 默认 profile 干扰 Datatrans 验证

### 现象

GATT primary/characteristic discovery 中出现多个默认服务，不利于确认 Datatrans 服务和
handle。

### 原因

MSDK BLE app 默认会带一些 profile，例如 DIS、sample、blue-courier 等。

### 解决方式

在 `gd_sdk/MSDK/ble/app/ble_app_config.h` 中关闭默认 profile 和命令支持，只保留
Datatrans 服务。

最终关键 handle 为：

| 用途 | UUID | Handle |
| --- | --- | --- |
| Datatrans service | `00000101-0000-1000-8000-00805f9b34fb` | `0x0001` - `0x0006` |
| RX characteristic value | `00000102-0000-1000-8000-00805f9b34fb` | `0x0003` |
| TX notify characteristic value | `00000103-0000-1000-8000-00805f9b34fb` | `0x0005` |
| TX CCCD | - | `0x0006` |

## 7. WiFi scan/connect 一直 timeout

### 现象

WiFi 初始化成功，`wifi_wait_ready()` 返回正常，但扫描和连接流程不推进：

- `wifi_management_scan()` 返回 timeout。
- WiFi event 已经 post。
- event loop 没有 dispatch。
- 队列中看不到积压消息。

### 排查方式

在 `wifi_eloop.c` 和 `wifi_management.c` 增加 GDB 可读计数器：

- event post 次数。
- message post 次数。
- wait 成功次数。
- dispatch 次数。
- WiFi management state machine 进入次数。
- scan/connect 命令状态。

观察结果是：event 已经进入队列并被取出，但没有被当成成功消息继续处理。

### 根因

RT-Thread 5 的 `rt_mq_recv()` 语义是：

- 成功时返回收到的数据长度。
- 失败时返回负数错误码。

而 GD SDK 的 `wrapper_rtthread.c` 原逻辑按“返回 0 才成功”处理：

- 收到长度例如 `12` 时，被误判为失败。
- WiFi event loop 取到消息后直接当 timeout 丢弃。
- WiFi state machine 无法推进。

### 解决方式

修改 `gd_sdk/MSDK/rtos/rtos_wrapper/wrapper_rtthread.c`：

- `sys_queue_fetch()` 中，`rt_mq_recv()` 返回 `< 0` 才视为错误。
- `sys_queue_read()` 中，`rt_mq_recv()` 返回 `< 0` 才视为错误。

修复后 WiFi event loop 可以正常 dispatch，scan 能找到目标 SSID，connect 能进入认证、
关联和 DHCP 阶段。

## 8. WiFi DHCP 阶段异常，PC 指向 SRAM 代码

### 现象

WiFi 已经连接到 AP，进入 DHCP 或 lwIP checksum 路径时触发异常：

- `mepc` 落在 `0x200001xx` 附近。
- 对应 `.code_to_sram` 中的 `lwip_standard_chksum`。
- GDB 反查 SRAM 内容发现代码被函数指针覆盖。

被覆盖的数据看起来像 ROM/mbedTLS platform callback 表，例如：

- `gd_hardware_poll`
- `sys_calloc`
- `sys_mfree`
- `snprintf`
- `printf`
- `my_time_get`

### 根因

GD SDK 官方 linker 会预留 SRAM 起始的 `0x200` 字节：

```text
RAM origin = 0x20000000 + 0x200
```

当前工程原 linker 从 `0x20000000` 开始放置 `.code_to_sram`，正好和 ROM/SDK 在低 SRAM
保存 platform callback 的区域冲突。运行到 DHCP/lwIP 时，低地址 SRAM 代码被回调表覆盖，
最终执行损坏指令。

### 解决方式

修改 `board/linker_scripts/link.lds`：

- RAM 起始地址改为 `0x20000000 + 0x200`。
- RAM 长度相应减少 `0x200`。

修改 linker script 后需要强制重新链接，否则 SCons 不一定自动发现 linker script 变化：

```sh
rm -f build/rtthread.elf build/rtthread.bin
scons --cdb -j$(nproc)
```

重新链接后确认 `.code_to_sram` 不再从 `0x20000000` 开始：

```sh
riscv64-unknown-elf-objdump -h build/rtthread.elf | sed -n '1,22p'
```

期望 `.code_to_sram` 的 VMA 为 `0x20000200`。

## 9. WiFi 最终验证

### 编译

```sh
rm -f build/rtthread.elf build/rtthread.bin
scons --cdb -j$(nproc)
```

### 下载

复用已有 OpenOCD GDB server：

```sh
scripts/riscv-gdb.sh -q build/rtthread.elf \
  -ex 'set confirm off' \
  -ex 'target extended-remote localhost:3333' \
  -ex 'monitor halt' \
  -ex 'load' \
  -ex 'compare-sections' \
  -ex 'monitor reset halt' \
  -ex 'monitor resume' \
  -ex detach \
  -ex quit
```

### 运行结果

本机热点环境下，板端状态显示：

- WiFi candidate 找到目标 SSID。
- 频点为 2412 MHz。
- WiFi connect 返回成功。
- DHCP 获取到 IPv4 地址。
- WiFi state machine 进入 connected 状态。

电脑侧 ping 板子 IP 成功，说明固件运行、WiFi RX/TX、lwIP、DHCP 和中断路径都已打通。

出于安全原因，本文档不记录本机热点密码；复现时请在 demo 配置中使用自己的测试 SSID
和密码。

## 10. BLE 最终验证

### 广播和服务发现

电脑可以扫描到板子广播名：

```text
GD32VW553H
```

GATT primary discovery 可以看到 Datatrans service：

```text
attr handle = 0x0001, end grp handle = 0x0006 uuid: 00000101-0000-1000-8000-00805f9b34fb
```

### 收发验证

使用 `gatttool -I` 交互式连接，等待 `Connection successful` 后再执行写操作：

```text
connect
char-write-req 0x0006 0100
char-write-req 0x0003 68656c6c6f0a
```

其中：

- `0x0006` 是 TX notify 的 CCCD，写 `0100` 表示开启 notify。
- `0x0003` 是 RX characteristic value。
- `68656c6c6f0a` 是 `hello\n`。

期望收到板端 echo：

```text
Notification handle = 0x0005 value: 68 65 6c 6c 6f 0a
```

### 注意事项

不要把多条 `gatttool` 命令太快地一次性 pipe 进去。之前出现过连接刚建立就写 CCCD/数据，
导致 BlueZ/gatttool 报 `Invalid file descriptor` 或断开的情况。

可靠流程是：

1. 进入 `gatttool -I`。
2. 执行 `connect`。
3. 等待 `Connection successful`。
4. 写 CCCD 开启 notify。
5. 写 RX characteristic。
6. 观察 TX notify。

这个问题更像是 host 侧 BlueZ/gatttool 的时序问题，不是板端 BLE Datatrans 服务不可用。

## 11. 关键文件清单

| 文件 | 作用 |
| --- | --- |
| `gd_sdk/SConscript` | 接入 GD SDK MSDK 源码、头文件、宏和预编译库 |
| `gd_sdk/port/gd_sdk_direct_demo.c` | 直接调用 MSDK WiFi/BLE API 的 demo |
| `gd_sdk/port/gd_sdk_direct_irq.c` | 手工接入 WiFi/BLE IRQ handler |
| `gd_sdk/port/rand_compat.c` | 避开 libc random TLS 崩溃 |
| `gd_sdk/port/slist_inline_compat.c` | 补齐 raw flash/list 外部符号 |
| `gd_sdk/MSDK/rtos/rtos_wrapper/wrapper_rtthread.c` | 适配 RT-Thread API，重点修复 message queue 返回值语义 |
| `board/linker_scripts/link.lds` | 预留 SRAM 低 0x200 字节，避免 ROM callback 区覆盖代码 |
| `gd_sdk/MSDK/ble/app/ble_app_config.h` | 关闭默认 BLE profile，仅保留 Datatrans 验证路径 |
| `gd_sdk/MSDK/lwip/lwip-2.2.0/port/lwipopts.h` | 调整 lwIP 配置以适配当前 libc/工程 |
| `scripts/riscv-gdb.sh` | GDB 选择和命令行调试入口 |

## 12. 后续建议

- 当前 WiFi/BLE 已验证能跑通，但 `wifi_eloop.c` 和 `wifi_management.c` 中保留了一些
  GDB 可读调试计数器。它们对后续排查有用，如果要整理成干净版本，可以等功能稳定后再删。
- 当前 demo 中 WiFi 凭据适合本地验证，不建议长期硬编码真实密码。后续可以改成从
  shell、配置区或 flash 参数读取。
- 如果继续扩展网络功能，建议仍然保持“MSDK WiFi/lwIP 一套栈”，不要再打开
  RT-Thread WiFi/lwIP，避免双栈同时初始化。

## 13. 后续更新：从根目录 `gd_sdk/` 迁到 package 方案

前面章节记录的是“根目录 `gd_sdk/` 直接接入 MSDK”的阶段。后续迁移方向已经调整为：

- 根目录 `gd_sdk/` 保留源码，但不再参与 BSP 编译。
- BLE/WiFi 相关 SDK 源码放入 `packages/gd32vw55x-ble/` 的 package 结构中。
- `board/wifi.c` 负责 WiFi package 初始化。
- `board/drv_wlan.c` 负责把 GD SDK WiFi 事件桥接到 RT-Thread WLAN 设备。
- `applications/ble_wifi_provision.c` 负责 BLE 配网 demo，不再从 `gd_sdk/port/` 启动 demo。

### 13.1 根目录 `gd_sdk/SConscript` 仍被编译

#### 现象

切到 package 后，如果根目录 `gd_sdk/SConscript` 仍然添加 MSDK 源码，会和 package 内部同名
源码、预编译库、平台初始化代码重复，出现重复符号、重复初始化或旧代码路径仍然运行。

#### 解决方式

临时把根目录 `gd_sdk/SConscript` 改为空返回，让根目录 `gd_sdk/` 不再参与构建：

```python
from building import *

Return('group')
```

这样可以保留目录用于对照和回滚，但实际构建只走 package 和 board 层代码。

### 13.2 demo 仍在 `gd_sdk/port/`，不符合 package 分层

#### 现象

旧的 `gd_sdk/port/gd_sdk_direct_demo.c` 同时承担 WiFi 初始化、BLE 初始化、默认联网和配网逻辑。
切 package 后它会把应用逻辑、BSP glue 和 SDK 内部调用混在一起。

#### 解决方式

把应用逻辑迁到 `applications/ble_wifi_provision.c`：

- BLE 只使用 `gd32vw55x_ble_*` 和 `gd32vw55x_ble_datatrans_*` package API。
- WiFi 只通过 RT-Thread WLAN API 发起连接。
- 默认启动 BLE advertising，收到 JSON 后解析 `ssid` 和 `pwd/password`。
- 默认测试连接使用 `123`，文档不记录测试热点密码。

### 13.3 WiFi 初始化入口不唯一

#### 现象

同时存在根目录 demo、package 初始化和 board driver 初始化时，WiFi 可能被初始化多次，状态机、
netif、event loop 和中断路径很难判断实际入口。

#### 解决方式

约束入口：

| 层级 | 职责 |
| --- | --- |
| `board/wifi.c` | 调用 `wifi_init()`、`wifi_wait_ready()`，设置 STA/AP mode，并在 ready 后置位 |
| `board/drv_wlan.c` | 注册 `wlan0/wlan1`，实现 scan/join/disconnect，并把 SDK connect/fail/disconnect 事件映射为 RT-Thread WLAN 事件 |
| `applications/ble_wifi_provision.c` | 等待 WLAN ready，然后调用 `rt_wlan_connect_adv()` |

### 13.4 package 目录被 `.gitignore` 忽略

#### 现象

`packages/gd32vw55x-ble/.gitignore` 内容为 `*`，导致 package 内复制和修改的 SDK 文件在当前 BSP
仓库里不会显示为普通 tracked diff。

#### 影响

本地验证可以正常编译，但后续发布到 RT-Thread package 仓库时，不能只看 BSP 仓库 diff；
需要单独整理 package 仓库内容、license、tag 和 package index 元数据。

#### 解决方式

- BSP 仓库只保留 package 使用配置和 board/app glue。
- `packages/gd32vw55x-ble/` 后续应作为独立 package 仓库整理。
- 发布前确认 GD SDK 源码和预编译库的授权边界，不在 README 中暗示 SDK 代码归本工程所有。

## 14. `mem.o` / `_SEGGER_RTT` 占用 `.bss` 和 RTT 控制块位置

### 现象

查看 map/size 时，`kernel/src/mem.o` 附近的 `.bss` 看起来占用较大，同时 RTT 控制块 `_SEGGER_RTT`
落在普通 `.bss` 中，可能影响对 SRAM 使用量和早期日志缓冲区位置的判断。

### 原因

SEGGER RTT 默认控制块和 buffer 是全局静态对象，会进入 `.bss`。如果不单独放段，链接器会把它和
普通 bss 混在一起，既不利于观察，也不利于把 RTT 控制块放到期望的 SRAM 位置。

### 解决方式

通过应用层 Kconfig 注入全局宏：

```text
SEGGER_RTT_SECTION=".bss.segger_rtt"
```

并在 linker script 中把 `.bss.segger_rtt` 放到普通 `.bss` 前面。这样 `_SEGGER_RTT` 会稳定放在
bss 前部，J-Link RTT 扫描更容易找到控制块，也便于从 map 文件中区分 RTT 占用和普通内核内存池占用。

## 15. Web Bluetooth 配网页面兼容性问题

### 15.1 Firefox 扫描失败

#### 现象

Firefox 打开 BLE 配网页面时报：

```text
当前浏览器不支持 Web Bluetooth
```

#### 原因

Firefox 桌面版没有完整支持 Web Bluetooth API，页面里的 `navigator.bluetooth` 不存在。

#### 解决方式

换用支持 Web Bluetooth 的 Chrome 或 Edge，并通过安全上下文打开页面。

### 15.2 Edge/Chrome 仍提示不支持

#### 现象

即使换到 Edge，页面仍提示不支持 Web Bluetooth。

#### 原因

Web Bluetooth 不需要单独安装 API，它是浏览器内置能力，但有运行条件：

- 页面必须运行在安全上下文：`https://` 或 `http://localhost`。
- 不能直接依赖普通 `file://` 场景。
- 浏览器、系统蓝牙栈和平台策略都要允许 Web Bluetooth。
- 某些 Linux/Edge/Chromium 组合可能默认禁用或实现不完整。

#### 解决方式

- 优先用 Chrome/Edge 打开 `http://localhost:<port>/...` 或 HTTPS 页面。
- 确认系统蓝牙可用，浏览器有蓝牙权限。
- 如果 `navigator.bluetooth` 仍不存在，说明当前浏览器/平台组合不可用，需换 Chrome 版本、平台或用原生 BLE 工具验证。

### 15.3 手机上复制 HTML 到本机

#### 现象

把 HTML 复制到手机后，是否能直接扫描连接不确定。

#### 原因

手机 Chrome 支持 Web Bluetooth 的前提仍然是安全上下文。直接从文件管理器打开 HTML 往往是
`file://`，不一定满足权限要求；而 `localhost` 在手机上指手机本机，不是电脑。

#### 解决方式

推荐把页面放到一个 HTTPS 地址，或者在手机本机起本地 HTTP 服务后用手机 Chrome 打开
`http://localhost`。如果只是验证 BLE 服务本身，也可以先用 nRF Connect 等手机原生 BLE 工具。

## 16. Linux `bluetoothctl` 和 BLE 默认启动问题

### 现象

需要判断当前固件是否已经默认开启 BLE，以及电脑是否能扫描到板子。

### 解决方式

Linux 侧可以用：

```sh
bluetoothctl
power on
scan on
```

固件侧应在应用启动后看到类似日志：

```text
[ble_prov] BLE advertising as GD32VW553H
```

电脑或手机能扫描到 `GD32VW553H`，说明 BLE stack、advertising 和 Datatrans service 至少已经启动。

## 17. BLE package 化和发布到 RT-Thread package 仓库

### 目标

把 GD SDK 中 BLE 相关内容从 BSP 私有 `gd_sdk/` 抽出，做成可复用 RT-Thread 软件包。

### 目录和职责

推荐 package 只暴露稳定 wrapper API，把 MSDK 内部细节藏在 package 内：

| 路径 | 作用 |
| --- | --- |
| `packages/gd32vw55x-ble/Kconfig` | package 配置项 |
| `packages/gd32vw55x-ble/SConscript` | 源码、头文件、库和宏接入 |
| `packages/gd32vw55x-ble/ble/include/` | 对应用公开的 API |
| `packages/gd32vw55x-ble/ble/adapter/` | RT-Thread 适配、IRQ、平台 glue |
| `packages/gd32vw55x-ble/GD32VW55x_RELEASE_.../` | GD 原始 SDK 文件，保留来源和目录结构 |
| `packages/gd32vw55x-ble/wifi/` | 为 BLE 配网示例复用的必要 WiFi 文件，逻辑上按 BLE package 类似方式整理 |

### 发布步骤

1. 单独创建 package 仓库，整理 `Kconfig`、`SConscript`、README、license 和示例。
2. 打 tag，例如 `v0.1.0`。
3. 在 RT-Thread packages 索引仓库中添加 package 元数据，包括名称、描述、仓库 URL、版本和依赖。
4. 提 PR 到 RT-Thread package 仓库。

### 注意事项

- GD SDK 源码和 `libble_min.a` / `libble_max.a` 等二进制库必须确认再分发许可。
- README 应说明 SDK 来源、支持芯片、已验证 BSP、依赖配置和已知限制。
- 不建议让普通应用直接 include 大量 MSDK 内部头文件；优先使用 package wrapper API。

## 18. package WiFi + RT-Thread WLAN 接入问题

### 18.1 `rt_wlan_connect_adv()` 超时，但 WPA 已握手成功

#### 现象

日志显示 WPA 4-way handshake 和 group key 都完成：

```text
WPA: 4-1 received
WPA: 4-2 send
WPA: 4-3 received
WPA: 4-4 send
WPA: 2-1 received
WPA: 2-2 send
```

但 `rt_wlan_connect_adv()` 仍然超时，没有 DHCP/IP 成功日志。

#### 原因

问题不在 WPA 密码或 4-way handshake，而在 WPA Established 后，SDK/MACIF、DHCP 和 RT-Thread WLAN
CONNECT 事件之间的时序没有打通。

#### 解决方式

在 `wifi_wpa.c` 中，当 EAPOL 首次进入 `EAPOL_STATE_ESTABLISHED` 后：

- 调用 `wpas_set_mac_ctrl_port(vif_idx, NULL, 1)` 打开 MAC control port。
- 投递 `WIFI_MGMT_EVENT_DHCP_START`，让 WiFi management 状态机进入 DHCP。

### 18.2 `DHCP_START` 来得太早

#### 现象

SDK/MACIF 会在 WPA 4-way 后、group key 完成前提前发 `DHCP_START`，导致 DHCP Discover 太早发出，
后面不再稳定推进。

#### 原因

加密连接下，只有 EAPOL 真正到 `EAPOL_STATE_ESTABLISHED` 后才应该开始 DHCP。

#### 解决方式

在 `wifi_management.c` 的 HANDSHAKE 状态里，对非 `CONFIG_WPA_SUPPLICANT` 且加密连接增加判断：

- 如果 `sta->w_eapol.state != EAPOL_STATE_ESTABLISHED`，忽略这次早到的 `DHCP_START`。
- 等 `wifi_wpa.c` 确认 Established 后再重新投递 `WIFI_MGMT_EVENT_DHCP_START`。

### 18.3 DHCP Offer 收到后，Request 卡在 `macif_tx_start()`

#### 现象

DHCP Discover 发出后能收到 Offer；lwIP 进入 `dhcp_handle_offer()` 后同步调用 `dhcp_select()` 发送
DHCP Request，但发送路径卡在 GD SDK 的 `macif_tx_start()`。

#### 原因

Offer 是在 RX 回调路径进入 lwIP 的。`dhcp_handle_offer()` 同步发 Request，相当于在 RX buffer
生命周期内重入 TX，GD MACIF 对这种时序不安全。

#### 解决方式

最终保留的修复放在 package WiFi port 层：

- 在 package 的 `wifi_netif.c` 中识别 DHCP RX 帧，对 DHCP 包使用 `PBUF_RAM` 新建 pbuf，
  `pbuf_take()` 复制 payload 后立即 `free_fn(buf)` 释放 SDK RX buffer，再交给 lwIP。

调试过程中曾尝试改 RT-Thread lwIP `dhcp.c`，把 `dhcp_select()` 延后到 `sys_timeout()` 中发送，
但后来确认不应该修改 `rt-thread/components/net/lwip/` 下的源码；该补丁已移除。重新验证时，仅保留
package 层 DHCP RX copy/free 逻辑，仍可完成 DHCP 并获取 IP。

### 18.4 DHCP Offer 缺少或未解析到 Server Identifier

#### 现象

部分 DHCP Offer 中 lwIP 没有拿到 option 54 Server Identifier，导致 `dhcp_handle_offer()` 认为
Offer 不完整，不进入 Request 阶段。

#### 解决方式

调试过程中曾在 RT-Thread lwIP `dhcp_recv()` 中尝试加入 fallback：如果未得到
`DHCP_OPTION_IDX_SERVER_ID`，就用 Offer 源 IP 作为 server id：

```c
u32_t server_id = lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(addr)));
dhcp_set_option_value(dhcp, DHCP_OPTION_IDX_SERVER_ID, server_id);
dhcp_got_option(dhcp, DHCP_OPTION_IDX_SERVER_ID);
```

但该修改属于 `rt-thread/components/net/lwip/` 源码改动，最终也已移除。当前验证通过的版本不依赖
这个 fallback；如果后续再次遇到特定 DHCP server 的 option 54 兼容问题，应优先在 board/package
层规避，或向上游提交可配置 hook，而不是直接改本工程内的 RT-Thread lwIP 源码。

### 18.5 不修改 RT-Thread lwIP 源码

#### 现象

前一轮能跑通的版本改过以下 RT-Thread lwIP 文件：

- `rt-thread/components/net/lwip/lwip-2.1.2/src/core/ipv4/dhcp.c`
- `rt-thread/components/net/lwip/port/lwipopts.h`
- `rt-thread/components/net/lwip/port/SConscript`
- `rt-thread/components/net/lwip/port/sys_arch.c`
- `rt-thread/components/net/lwip/port/gd32vw55x_lwip_hooks.h`

#### 处理方式

这些改动已从最终方案移除，`rt-thread/components/net/lwip/` 恢复为上游原样。必须保留的配置改为：

- `LWIP_SUPPORT_CUSTOM_PBUF`
- `PBUF_LINK_ENCAPSULATION_HLEN`
- EAPOL 处理放在 package `wifi_netif.c`，不再需要 `LWIP_HOOK_FILENAME`

前两个宏放在 `rtconfig.h` 的 BSP 配置里。早期的
`board/gd32vw55x_lwip_hooks.h` / `packages/gd32vw55x-ble/wifi/adapter/lwip_hooks.c`
只是为了通过 lwIP unknown-ethernet hook 接 EAPOL；现在 EAPOL 已在 package `wifi_netif.c`
进入 lwIP 前直接交给 `wifi_wpa_rx_eapol_event()`，因此该 hook 已移除。

同时取消独立 `PKG_USING_GD32VW55X_WIFI`，避免 RT-Thread lwIP port 的 `SConscript` 因该宏排除
原生 `sys_arch.c`。当前 WiFi 代码由 `packages/gd32vw55x-ble` 内置的 WiFi 目录提供。

### 18.6 临时 DHCP/EAPOL 日志影响时序

#### 现象

调试过程中大量 `printf` 能帮助确认状态，但也会拉长 WiFi RX/TX 回调路径，甚至让 RTT 输出截断，
影响判断 DHCP ACK 后的真实行为。

#### 解决方式

问题定位后删除临时日志，只保留必要状态输出，并用以下关键字检查残留：

```sh
grep -R "EAPOL: rx state\|WIFI_MGMT:\|DHCP TX\|DHCP RX\|LWIP DHCP" ...
```

## 19. lwIP `tcpip` 线程栈溢出

### 现象

WPA 完成后进入 DHCP，RTT 输出：

```text
[2547] E/kernel.sched: thread:tcpip stack overflow
```

### 原因

当前 WiFi + lwIP + DHCP 路径的调用深度和局部变量占用超过了默认
`RT_LWIP_TCPTHREAD_STACKSIZE=1024`。

### 解决方式

把 `.config` 和 `rtconfig.h` 中的：

```text
RT_LWIP_TCPTHREAD_STACKSIZE
```

从 `1024` 提高到 `4096`。重新编译、烧录后，DHCP 流程不再触发 `tcpip` 栈溢出。

## 20. WiFi 已连接但应用打印 IP 为 `0.0.0.0`

### 现象

板端日志显示：

```text
WiFi connected successfully
[WLAN.mgnt] wifi connect success ssid:123
[ble_prov] WiFi connected: SSID=123 IP=0.0.0.0
```

### 原因

RT-Thread WLAN 的 CONNECT 事件比 DHCP 地址写入完成更早。应用只等待
`rt_wlan_is_connected()`，还没有等待 lwIP `netif` 真正拿到非零 IPv4 地址。

### 解决方式

在 `applications/ble_wifi_provision.c` 增加等待 IP 的逻辑：

- 优先从 `netdev` 读取 IPv4。
- 如果未启用 `RT_USING_NETDEV`，直接检查 lwIP `netif_default` 的 `netif_ip4_addr()`。
- 只有 IP 非 `0.0.0.0` 后才打印连接成功和通过 BLE notify 返回成功。

最终验证日志：

```text
WiFi connected successfully
[4015] I/WLAN.mgnt: wifi connect success ssid:123
[ble_prov] WiFi connected: SSID=123 IP=10.83.96.159
```

## 21. J-Link / RTT 验证过程中的注意事项

### 21.1 J-Link GDB Server 会 halt 目标

#### 现象

启动 `JLinkGDBServer` 后，RTT 没有继续输出。

#### 原因

GDB server 连接目标时会 halt core，必须有 GDB 客户端连上后执行 reset/go 或 continue。

#### 解决方式

启动 server：

```sh
/usr/local/bin/JLinkGDBServer \
  -device GD32VW553HMQ7 -if JTAG -speed 8000 -JTAGConf -1,-1 \
  -port 19020 -swoport 19022 -rtttelnetport 19021
```

再用 GDB 恢复运行：

```sh
scripts/riscv-gdb.sh build/rtthread.elf -batch \
  -ex "target remote :19020" \
  -ex "monitor reset" \
  -ex "monitor go" \
  -ex "detach" \
  -ex "quit"
```

### 21.2 stale J-Link 进程会阻塞烧录

#### 现象

烧录时端口被占用或 J-Link 连接异常。

#### 解决方式

先查看进程：

```sh
pgrep -af 'JLinkGDBServer|JLinkExe'
```

如需结束，只按具体 PID 执行 `kill <PID>`，不要用模糊的 `killall/pkill`。

### 21.3 `socat` timeout 返回 124 不等于固件失败

#### 现象

用 `timeout 120s socat ...` 抓 RTT，命令退出码是 `124`。

#### 原因

`124` 是 Linux `timeout` 到时终止 `socat`，只表示抓日志时间到了。是否成功要看 RTT 内容。

#### 解决方式

用日志中的关键行为判断：

- BLE advertising 是否启动。
- WPA handshake 是否完成。
- 是否打印 `WiFi connected successfully`。
- 是否打印非零 IPv4 地址。

## 22. 当前 package 方案的验证结论

当前 package 化路径已经验证到：

- 根目录 `gd_sdk/` 不参与编译。
- BLE advertising 名称为 `GD32VW553H`。
- 默认应用能发起 WiFi 连接。
- WPA handshake 完成。
- DHCP 能拿到 IPv4。
- `applications/ble_wifi_provision.c` 等到非零 IP 后再上报成功。

已验证 RTT 关键日志：

```text
[ble_prov] BLE advertising as GD32VW553H
[ble_prov] connect WiFi SSID=123 pwd_len=14
WiFi join start
WPA: 4-1 received
WPA: 4-2 send
WPA: 4-3 received
WPA: 4-4 send
WPA: 2-1 received
WPA: 2-2 send
WiFi connected successfully
[4015] I/WLAN.mgnt: wifi connect success ssid:123
[ble_prov] WiFi connected: SSID=123 IP=10.83.96.159
```

## 23. 不同 AP 的 WPA/DHCP 与 RT-Thread WLAN 状态差异

### 23.1 AP 不发送独立 group-key 2-way 时 DHCP 不启动

#### 现象

BLE 配网连接 SSID `123` 时，日志停在：

```text
WPA: 4-1 received
WPA: 4-2 send
WPA: 4-3 received
WPA: 4-4 send
[ble_prov] WiFi connect timeout
```

`123` 会继续出现：

```text
WPA: 2-1 received
WPA: 2-2 send
```

而 `123` 不一定发送单独的 group-key 2-way。

#### 原因

原先只在 SDK EAPOL 状态进入 `EAPOL_STATE_ESTABLISHED` 后才打开 controlled port
并投递 `WIFI_MGMT_EVENT_DHCP_START`。部分 WPA2/RSN AP 会在 4-way 第 3 帧里携带
GTK，STA 发出 4/4 后 PTK/GTK 已安装，SDK 状态停在 `EAPOL_STATE_GROUP`，不会再等到
`ESTABLISHED`。

#### 解决方式

在 `wifi_wpa.c` 增加 `wifi_wpa_sta_eapol_data_port_ready()`：

- `EAPOL_STATE_ESTABLISHED` 仍然视为 ready。
- WPA2/RSN 下，如果状态为 `EAPOL_STATE_GROUP`，并且 PTK 已设置、需要的 GTK 已拿到，
  也视为 data port ready。
- `wifi_management.c` 的 DHCP_START guard 复用同一个判断，避免 MACIF 早到的
  DHCP_START 在 key 未就绪时放行。

验证结果：

```text
[ble_prov] connect WiFi SSID=123 pwd_len=8
WPA: 4-1 received
WPA: 4-2 send
WPA: 4-3 received
WPA: 4-4 send
WiFi connected successfully
[210090] I/WLAN.mgnt: wifi connect success ssid:123
[ble_prov] WiFi connected: SSID=123 IP=192.168.43.11
```

### 23.2 SDK 中间失败事件不能直接映射为 RT-Thread 最终失败

#### 现象

`123` 有时会先打印一次 SDK 连接失败，但随后 WPA handshake 和 DHCP 实际成功：

```text
WiFi connection failed: reason=2 (no ap)
WPA: 4-1 received
WPA: 4-2 send
WPA: 4-3 received
WPA: 4-4 send
WPA: 2-1 received
WPA: 2-2 send
[ble_prov] WiFi connect timeout
```

#### 原因

`board/drv_wlan.c` 之前注册了 SDK `WIFI_MGMT_EVENT_CONNECT_FAIL` 回调，并把它立即上报为
RT-Thread `RT_WLAN_DEV_EVT_CONNECT_FAIL`。但 GD SDK 内部连接状态机有 retry 流程，中间
失败事件不代表最终失败；RT-Thread WLAN 状态因此提前退出，应用继续等待时就超时。

#### 解决方式

`board/drv_wlan.c` 改为调用 SDK 阻塞连接接口：

- `wifi_management_connect_with_bssid(..., 1)`
- 或 fallback `wifi_management_connect(..., 1)`

只在阻塞接口返回最终结果后，再向 RT-Thread 上报 `RT_WLAN_DEV_EVT_CONNECT` 或
`RT_WLAN_DEV_EVT_CONNECT_FAIL`。

验证结果：

```text
[ble_prov] connect WiFi SSID=123 pwd_len=14
[251566] I/WLAN.mgnt: disconnect success!
WiFi join start
WiFi join BSSID c4:16:c8:0b:23:e0 RSSI=-57 akm=0x00000008 pairwise=0x00000004 group=0x00000004
WPA: 4-1 received
WPA: 4-2 send
WPA: 4-3 received
WPA: 4-4 send
WPA: 2-1 received
WPA: 2-2 send
WiFi connected successfully
[261014] I/WLAN.mgnt: wifi connect success ssid:123
[ble_prov] WiFi connected: SSID=123 IP=10.83.96.159
```

### 23.3 `ping` 命令触发 tshell 栈余量告警

#### 现象

联网后在 MSH 中执行 `ping baidu.com`，能收到 ICMP 回包，但出现：

```text
[216090] W/kernel.sched: warning: tshell stack is close to end of stack address.
```

#### 原因

FinSH/MSH 默认 `FINSH_THREAD_STACK_SIZE=2048`，在启用 lwIP、DNS 和 ping 命令后栈余量偏紧。

#### 解决方式

将 `.config` 和 `rtconfig.h` 中的 `FINSH_THREAD_STACK_SIZE` 调整为 `4096`。最终固件重新
构建、烧录后，BLE 配网和 MSH `ping` 路径仍可运行。
