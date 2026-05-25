 最稳的做法不是让别人照当前 README.md:41 走，因为它现在还是 Windows/
  GD32EmbeddedBuilder 路线；你这边真正的 Arch 兼容是在 rtconfig.py:18 和
  rtconfig.py:35，而且 SConstruct:18 会在缺少 packages/gd32-riscv-series-latest
  时直接要求 pkgs --update。所以第一步是先把你当前“能编过”的整棵树做成一个
  branch/tag，最好把 packages/ 也一起带上。

  别人拿你的分支后，在 Arch 上先装基础包。base-devel 是 AUR/makepkg 的基础，git
  、python、scons 是构建必需，openocd 和 picocom 分别够用来调试/下载和串口。(wi
  ki.archlinux.org (https://wiki.archlinux.org/title/Kernel/Arch_Build_System))

  sudo pacman -S --needed base-devel git python scons openocd picocom

  工具链这块，你现在这套 BSP 只要求 /usr/bin/riscv-none-elf-* 能找到，并且 nano
  .specs 可用；最快复现是装 riscv-none-elf-gcc-bin，如果想更贴近源码构建环境，
  就装 riscv-none-elf-gcc。AUR 里的 riscv-none-elf-gcc 包说明写了源码构建顺序是
  riscv-none-elf-binutils -> riscv-none-elf-gcc-stage1 -> riscv-none-elf-newlib
  -> riscv-none-elf-gcc，并且包含你这里需要的 nano libs。(aur.archlinux.org
  (https://aur.archlinux.org/packages/riscv-none-elf-gcc-bin))

  # 快速复现
  yay -S riscv-none-elf-gcc-bin

  # 或者源码构建
  yay -S riscv-none-elf-gcc

  如果对方走源码 AUR，提前告诉他两个坑：makepkg 的签名校验用的是“用户自己的 GPG
  keyring”，不是 pacman-key；并行编译靠 makepkg.conf 里的 MAKEFLAGS，例如设成 -
  j$(nproc)。(wiki.archlinux.org (https://wiki.archlinux.org/title/Makepkg))

  # 例如
  echo 'MAKEFLAGS="-j$(nproc)"' >> ~/.makepkg.conf

  然后就是拿你的仓库直接编。当前 rtconfig.py:20 在 Linux 下默认 EXEC_PATH=/usr/
  bin，在 rtconfig.py:65 里也已经固定用了 --specs=nano.specs
  --specs=nosys.specs，所以只要 toolchain 在 PATH 里，一般不需要再设
  RTT_EXEC_PATH。

  cd rt-thread/bsp/gd32/risc-v/gd32vw553h-eval

  which riscv-none-elf-gcc
  riscv-none-elf-gcc --print-file-name=nano.specs

  # 如果你没有把 packages/ 一起给别人，再补这一句
  pkgs --upgrade && pkgs --update

  scons --cdb -j$(nproc)

  如果 riscv-none-elf-gcc 不在 /usr/bin，再手动补：

  export RTT_EXEC_PATH=/你的/riscv-none-elf/bin

  想把这件事做成真正可复现的“开发环境”，我建议你对外只发两样东西：

  1. 你的 branch/tag。
  2. 一份简短的 ARCH_SETUP.md，把上面这些命令写进去。

  如果你要，我可以直接帮你把这份说明写成仓库里的 ARCH_SETUP.md。