# Lecture 0 : environment configurations

**deprecated**: 仅仅记录开课前在自己电脑上安装遇到的问题和 trouble shooting, 具体配置请参考习题课.

!!! note "我的环境"
    请尽量选择旧版本的 Debian 及其衍生发行版, 如 Ubuntu 18.04 LTS. (参见下方安装时 `stropts.h` 部分)

    ```
    Linux version 5.15.133.1-microsoft-standard-WSL2

    Ubuntu 22.04 (extremely bad choice, not recommanded)

    GCC 11.2.0
    ```

    64 位系统.

WSL 提示无法安装请参考 https://learn.microsoft.com/zh-cn/windows/wsl/troubleshooting#installation-issues.

## 其他可能 (显然) 更优的教程

1. 使用 UC Berkeley 的仓库 : https://github.com/Berkeley-CS162
2. 使用 JHU 的仓库 : https://www.cs.jhu.edu/~huang/cs318/fall17/project/guide.html
3. 使用 PKU 的 docker : https://pkuflyingpig.gitbook.io/pintos/
4. https://github.com/youcunhan/cs130-OperatingSystem-pintos/blob/master/pintos-install.sh

## 环境要求

https://web.stanford.edu/class/cs140/projects/pintos/pintos_12.html

## 前置安装/下载

首先: `sudo apt-get update` 并且 `sudo apt-get upgrade`

`sudo apt-get -y install build-essential automake git libncurses5-dev texinfo expat libexpat1-dev wget`

获取 pintos: `git clone git://pintos-os.org/pintos-anon pintos`

由于这里是 git protocol, 设置 git 的 `http.proxy` 是一点用没有的, 下载速度慢是正常现象.

## 模拟器

### QEMU

`sudo apt-get -y install qemu-system`

### Bochs 2.6.7

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

- `/src/threads/Make.vars` 7 行 `bochs` 改为 `qemu`
- `/utils/pintos` 103 行左右 `bochs` 改为 `qemu`
- `/utils/pintos` 621 行左右: `qemu` 改为 `qemu-system-x86_64`


在 `pintos/src/threads` 下:

```
make

cd build

pintos --qemu -- run alarm-multiple
```

能跑就是基本成功.

在 `pintos/src/threads/build/` 下:

```
make check
```

进入漫长的等待时间.

27 fail 20 就是完全成功.

在我用 WSL 命令行和 vim 改了许久之后, 突然想起来其实直接在打开了对应文件夹的 VS Code 就能快速完成这些破事了.

## 使用 gdb 进行 debug

开启两个终端, 第一个在 `pintos/src/threads/build/` 下 `pintos --qemu --gdb -- run testName`

在 `pintos/src/threads/build/` 下 `pintos-gdb`

尝试运行 `debugpintos` (在 gdb macros 里面声明的, 用处是连接到 qemu, 直接用 `target remote localhost:1234` 也可以)

无报错或者运行后再运行指令 `c` 可以正常继续就是配置正确.

如果出现 `architecture` 相关报错, 进入后运行 `set architecture i386:x86-64`, 然后运行 `debugpintos`

之后可以正常打断点运行.