# nuttx_vendor_espressif
Espressif chip support for NuttX

```sh
~$ mkdir espressif-workspace
~$ cd espressif-workspace/
~/espressif-workspace$ git clone https://github.com/lljwork2021/nuttx_vendor_espressif.git
~/espressif-workspace$ git clone https://github.com/apache/nuttx.git -b releases/13.0 nuttx
~/espressif-workspace$ git clone https://github.com/apache/nuttx-apps -b releases/13.0 apps
~/espressif-workspace$ mkdir -p ~/espressif-workspace/toolchain/riscv-none-elf-gcc
curl -s -L "https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v13.2.0-2/xpack-riscv-none-elf-gcc-13.2.0-2-linux-x64.tar.gz" \
| tar -C ~/espressif-workspace/toolchain/riscv-none-elf-gcc --strip-components 1 -xz
~/espressif-workspace$ echo "export PATH=~/espressif-workspace/toolchain/riscv-none-elf-gcc/bin:$PATH" >> ~/.bashrc
~/espressif-workspace$ sudo usermod -aG dialout "$USER"
```
