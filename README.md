<div class="title" align=center>
    <img src="./doc/logo.png" width=550>
    <br>
    <p>
        <img src="https://img.shields.io/badge/license-%20%20GNU%20GPLv3%20-orange?style=plastic">
        <img src="https://img.shields.io/badge/C-00599C?logo=c&logoColor=white)">
        <img src="https://img.shields.io/badge/Bash-4EAA25?logo=gnubash&logoColor=fff">
        <img src="https://custom-icon-badges.demolab.com/badge/PowerShell-5391FE?logo=powershell-white&logoColor=fff">
        <img src="https://img.shields.io/github/stars/XJTU-WXY/LazyCommand?style=social">
    </p>

</div>

## 🚩 简介

一个基于 C 语言编写的轻量级跨平台终端命令快捷别名小工具，旨在作为`alias`指令或 shell 函数的简化替代方案，更便于统一管理并提供位置参数模板格式化功能。

## 🔑 使用方法
### 终端初始化

在终端内运行：
```
lc init
```
或 Powershell 内： 
```
.\lc.exe init
```

默认情况下会自动检测当前的终端类型，也可以显式指定：

```
lc init bash
lc init zsh
lc init fish
lc init powershell
lc init cmd        # 仅 Windows
lc init all         # 一次性为当前平台支持的所有终端写入
```
重启终端后生效。
### 编辑配置文件（`lc_config.txt`）

放在与 `lc` / `lc.exe` **同一目录**下，格式为每行一条：

```yaml
别名: 命令模板
```

- 以 `#` 或 `//` 开头的行视为注释；空行会被忽略。
- 模板里可以用 `%{1}`、`%{2}` … 引用位置参数，可以重复引用同一个。
- 模板里可以用 `%{**}` 收集剩余参数。
- `%` 后面如果不是 `{`，会被当成普通字符。
- 可用`\`转义`%`和`\`自身，如：
  ```yaml
  esc: echo 100\%{done} and \\%{1}\\
  ```

  `lc esc arg1` 会等效：`echo 100%{done} and \arg1\`
- 别名不能包含空白字符，且不能是保留字 `init`。

#### 示例

```yaml
aptup: apt update && apt upgrade
ccpy: conda create -n %{1} python=%{2}
mkcd: mkdir %{1} && cd %{1}
gac: git add -A && git commit -m "%{1}"
targz: tar -czvf %{1} %{**}
```

#### 调用效果

| 命令                              | 等效执行                                     |
|-----------------------------------|-----------------------------------------------|
| `lc aptup`               | `apt update && apt upgrade`            |
| `lc ccpy myenv 3.11`               | `conda create -n myenv python=3.11`            |
| `lc mkcd build`                   | `mkdir build && cd build`                      |
| `lc gac "fix bug"`                | `git add -A && git commit -m "fix bug"`        |
| `lc targz archive.tar file1.txt dir1/`                | `tar -czvf archive.tar file1.txt dir1/`        |

## 📦 构建
### Linux / macOS

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Windows

```bat
cmake -B build
cmake --build build --config Release
```

## ⚖ 开源协议
本项目基于 [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.html) 开源。
  
*Open source leads the world to a brighter future!*