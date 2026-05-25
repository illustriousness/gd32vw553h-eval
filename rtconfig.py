import os

ARCH = 'risc-v'
CPU = 'bumblebee'
CORE = 'risc-v'
BUILD = 'debug'

CROSS_TOOL = os.getenv('RTT_CC', 'gcc')
if CROSS_TOOL != 'gcc':
    print('Please make sure your toolchains is GNU GCC!')
    exit(0)

PLATFORM = 'gcc'
BSP_LIBRARY_TYPE = None
MAP_FILE = './build/rtthread.map'
LINK_FILE = './board/linker_scripts/link.lds'

EXEC_PATH = os.getenv('RTT_EXEC_PATH')
if not EXEC_PATH:
    print('Error: RTT_EXEC_PATH is not set')
    exit(1)

PREFIX = os.getenv('RTT_CC_PREFIX')
if not PREFIX:
    for prefix in ('riscv64-elf-', 'riscv64-unknown-elf-'):
        gcc = os.path.join(EXEC_PATH, prefix + 'gcc')
        if os.name == 'nt':
            gcc += '.exe'
        if os.path.exists(gcc):
            PREFIX = prefix
            break

if not PREFIX:
    print('Error: no supported RISC-V GCC found in RTT_EXEC_PATH: ' + EXEC_PATH)
    print('Tried: riscv64-elf-gcc, riscv64-unknown-elf-gcc')
    exit(1)

CC = PREFIX + 'gcc'
AS = PREFIX + 'gcc'
AR = PREFIX + 'ar'
CXX = PREFIX + 'g++'
LINK = PREFIX + 'gcc'
TARGET_EXT = 'elf'
SIZE = PREFIX + 'size'
OBJDUMP = PREFIX + 'objdump'
OBJCPY = PREFIX + 'objcopy'

# 设备/ABI 选项含义:
# -march=rv32imafc_zicsr_zifencei : 目标 ISA (RV32 + M/A/F/C + CSR/FENCE.I)
# -mcmodel=medany                 : 位置无关取址模型
# -msmall-data-limit=8            : 小数据区阈值(字节)
# -mabi=ilp32f                    : 32 位 ABI + 单精度硬件浮点
# -fmessage-length=0              : 诊断信息不自动折行
# -fsigned-char                   : plain char 作为 signed char
# -ffunction-sections/-fdata-sections : 函数/数据分节，便于链接裁剪
DEVICE = (
    ' -march=rv32imafc_zicsr_zifencei'
    ' -mcmodel=medany'
    ' -msmall-data-limit=8'
    ' -mabi=ilp32f'
    ' -fmessage-length=0'
    ' -fsigned-char'
    ' -ffunction-sections'
    ' -fdata-sections'
    ' --specs=picolibc.specs'
)

# C 选项:
# -std=gnu11                      : GNU C11
# -DUSE_STDPERIPH_DRIVE           : 启用 GD 外设驱动路径
# -fcommon                        : 兼容旧代码的 common 符号行为
# -Wno-error=implicit-function-declaration : 旧代码兼容
CFLAGS = DEVICE + ' -std=gnu11 -DUSE_STDPERIPH_DRIVE -fcommon -Wno-error=implicit-function-declaration'

# 汇编选项:
# -x assembler-with-cpp           : 汇编先过 C 预处理
AFLAGS = DEVICE + ' -c -x assembler-with-cpp'

ble_pkg_path = os.path.join(os.path.dirname(__file__), 'packages', 'gd32vw55x-ble')
ble_sdk_path = os.path.join(ble_pkg_path, 'GD32VW55x_RELEASE_V1.0.3g')
ble_msdk_path = os.path.join(ble_sdk_path, 'MSDK')
if os.path.exists(ble_msdk_path):
    # gd32-riscv-series 在 BSP_USING_WLAN 下会编译 WLAN 异常/系统日志代码，
    # 这些源码包含 MSDK 头文件；由 BSP 提供路径，避免修改外设库软件包脚本。
    for include_path in (
        os.path.join(ble_pkg_path, 'ble', 'adapter'),
        os.path.join(ble_pkg_path, 'wifi', 'adapter'),
        os.path.join(ble_sdk_path, 'config'),
        os.path.join(ble_sdk_path, 'ROM-EXPORT', 'bootloader'),
        os.path.join(ble_sdk_path, 'ROM-EXPORT', 'halcomm'),
        os.path.join(ble_msdk_path, 'app'),
        os.path.join(ble_msdk_path, 'rtos', 'rtos_wrapper'),
        os.path.join(ble_msdk_path, 'plf', 'src'),
        os.path.join(ble_msdk_path, 'plf', 'src', 'raw_flash'),
        os.path.join(ble_msdk_path, 'plf', 'src', 'uart'),
    ):
        CFLAGS += ' -I' + include_path

rom_symbol = os.path.join(ble_pkg_path, 'GD32VW55x_RELEASE_V1.0.3g', 'ROM-EXPORT', 'symbol', 'rom_symbol_m.gcc')
rom_symbol_flag = ''
if os.path.exists(rom_symbol):
    rom_symbol_flag = ' -Wl,--just-symbols=' + rom_symbol

# 链接选项:
# -nostartfiles                   : 不用工具链默认启动文件
# --gc-sections                   : 链接时丢弃未引用段
# --no-warn-mismatch              : 放宽第三方预编译库属性不匹配告警
# --just-symbols                  : 仅导入 WiFi ROM 符号表
LFLAGS = DEVICE + ' -nostartfiles -Xlinker --gc-sections -Xlinker --no-warn-mismatch' + rom_symbol_flag + ' -T ' + LINK_FILE + ' -Wl,-Map=' + MAP_FILE
CPATH = ''
LPATH = ''

if BUILD == 'debug':
    CFLAGS += ' -O0 -g3'
    AFLAGS += ' -g3'
else:
    CFLAGS += ' -O2'

CXXFLAGS = CFLAGS
POST_ACTION = OBJCPY + ' -O binary $TARGET ./build/rtthread.bin\n' + SIZE + ' $TARGET \n'


def dist_handle(BSP_ROOT, dist_dir):
    import sys
    sys.path.append(os.path.join(os.path.dirname(BSP_ROOT), 'tools'))
    from sdk_dist import dist_do_building
    dist_do_building(BSP_ROOT, dist_dir)
