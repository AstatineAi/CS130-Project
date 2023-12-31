# Lecture 0 : environment configurations

我的环境:

```
Linux version 5.15.133.1-microsoft-standard-WSL2

Ubuntu 22.04

GCC 11.2.0
```

## Requirements

https://web.stanford.edu/class/cs140/projects/pintos/pintos_12.html

## 前置安装/下载

首先: `sudo apt-get update` 并且 `sudo apt-get upgrade`

`sudo apt-get -y install build-essential automake git libncurses5-dev texinfo expat libexpat1-dev wget`

获取 pintos: `git clone git://pintos-os.org/pintos-anon pintos`

由于这里是 git protocol, 设置 git 的 `http.proxy` 是一点用没有的, 下载速度慢是正常现象.

## 模拟器

### QEMU

`sudo apt-get -y install qemu-system`

## Bochs 2.6.7

下载 bochs: https://sourceforge.net/projects/bochs/files/bochs/2.6.7/

切换到 `.tar.gz` 文件的目录:

```
tar zxvf bochs-2.6.7.tar.gz

cd bochs-2.6.7

./configure --enable-gdb-stub

make

sudo make install
```

然后回到 pintos 的目录, 运行命令 `bochs --help` 会提示你版本是 `2.6.7` 且是刚刚编译的.

## 配置 Pintos (使用 qemu)

为了方便运行 pintos, 在 `.bashrc` 加上对应位置的 export 命令.

- 切换到 `pintos/src/utils` 目录, 打开 `pintos-gdb`, 修改 `GDBMACROS` 到 `pintos/src/misc/gdb-macros`.
- 打开 `Makefile`, 把变量名 `LOADLIBES` 改为 `LDLIBS`
- 由于高版本 Linux 没有 `stropts.h` 库, 在 `/usr/include/` 下面新建一个空的即可
- 注释掉 `squish-pty.c` 288 - 294 行

此时在 `pintos/src/utils` 下 `make`, 只有 warning.

- 把 `pintos/src/utils/pintos` 260 行的 `kernel.bin` 改成 `kernel.bin` 的完整路径 (在 `src/threads/build` 下).
- 把 `pintos/src/utils/Pintos.pm` 362 行的 `loader.bin` 换成完整路径 (和上面的 `.bin` 在同一个目录)
- `/src/threads/Make.vars` 7 行 `bochs` 改为 `qemu`
- `/utils/pintos` 103 行左右 `bochs` 改为 `qemu`
- `/utils/pintos` 621 行左右: `qemu` 改为 `qemu-system-x86_64`

在 `pintos/src/threads` 下:

```
make

cd build

pintos -- run alarm-multiple
```

能跑就是基本成功.

在 `pintos/src/threads` 下:

```
make check
```

进入漫长的等待时间.

不是 27 fail 27 就是完全成功.

在我用 WSL 命令行和 vim 改了许久之后, 突然想起来其实直接在打开了对应文件夹的 VS Code 就能快速完成这些破事了.
