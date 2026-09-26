# 67zip

命令列壓縮程式，用法對齊 p7zip / 7-Zip 的 `7z`。壓縮檔副檔名是 `.67z`。

```
67zip <command> [<switches>...] <archive_name> [<file_names>...] [@listfile]
```

## 編譯

```
make
sudo make install    # 安裝到 /usr/local/bin/67zip
./build/glibc-linux.sh
```

各平台的進入點在 `build/`。看 `build/README.md`。

推上 `v*` tag 會觸發 `.github/workflows/release.yml`，產出 deb、rpm、apk、Arch `pkg.tar.zst`、FreeBSD pkg，以及其他系統的 tar.gz，並掛到 GitHub Release。

需要 C++17 編譯器、`zlib` 與 `liblzma`。

```
make            # glibc / musl Linux、BSD、macOS、Solaris、Haiku、Android NDK
make test       # 可移植層的單元測試
```

系統呼叫只放在 `src/port.hpp`。壓縮檔路徑一律用 `/`，權限位元組是 Unix mode，不用各平台的 `S_I*`。不要在 `main.cpp` 新增系統呼叫。

可以沿用現有程式的系統（有 C++17 與 POSIX libc 即可）：

| 系統 | 說明 |
| --- | --- |
| glibc / musl Linux、NixOS、Android | NixOS 沒有 `/usr`，用 `pkg-config` 找 zlib/liblzma，安裝時要給 `PREFIX=$out` |
| macOS、iOS | POSIX 分支 |
| FreeBSD、NetBSD、OpenBSD、DragonFly、GhostBSD | GhostBSD 與 FreeBSD 同一套 userland |
| Solaris、illumos、OmniOS | `__illumos__` 與 `__sun` 分開辨識 |
| Haiku | POSIX 分支 |

沒有完整 POSIX 的系統走 `src/port.cpp` 裡的專用分支，建置腳本在 `build/`：

| 系統 | 怎麼建 |
| --- | --- |
| Windows、ReactOS | 同一個 Win32 分支。`./build/windows.sh`，ReactOS 用 `./build/reactos.sh` |
| FreeDOS | DJGPP。`CXX=i586-pc-msdosdjgpp-g++ ./build/freedos.sh`。沒有執行緒、沒有符號連結 |
| KolibriOS | 原始碼以 `-DZIP67_OS_KOLIBRI` 編譯。SDK 若沒有 C++17 標準庫就無法連結 |
| Redox OS | 不使用 relibc 沒有的 POSIX 呼叫。`./build/redox.sh` |
| SerenityOS | 不使用 symlink / termios。`./build/serenity.sh` |

## 命令

| 命令 | 作用 |
| --- | --- |
| `a` | 加入檔案 |
| `d` | 從壓縮檔刪除 |
| `e` | 解壓（不要目錄） |
| `l` | 列出內容 |
| `rn` | 重新命名壓縮檔內的項目 |
| `t` | 測試完整性 |
| `u` | 更新較新的檔案 |
| `x` | 解壓（保留路徑） |
| `b` | 效能測試 |
| `h` | 計算雜湊 |
| `i` | 顯示支援的格式 |

## 例子

```
67zip a archive.67z dir/                 # 壓縮，自動補 .67z
67zip a -mx9 -m0=lzma2 archive files     # 最高壓縮，LZMA2
67zip a -psecret archive.67z files       # 設密碼
67zip l archive.67z
67zip l -slt archive.67z                 # 技術資訊
67zip t archive.67z
67zip x -y -oout archive.67z             # 解壓到 out/，全部覆寫
67zip e archive.67z                      # 解壓到目前目錄，不要路徑
67zip x -so archive.67z                  # 寫到 stdout
67zip u archive.67z changed.txt
67zip d archive.67z '*.log'
67zip rn archive.67z old.txt new.txt
67zip h -scrcSHA256 file
```

常用開關與 `7z` 相同：`-mx0`…`-mx9`、`-m0=lzma2|deflate|copy`、`-mmtN`、`-o{dir}`、`-p{password}`、`-r`、`-y`、`-ao{a|s|t|u}`、`-x`、`-i`、`-sdel`、`-si`、`-so`、`-slt`、`-bd`、`-bt`、`@listfile`。

## 格式

`.67z` 是 67zip 自己的容器，不是 7z。方法為 LZMA2（預設）、Deflate 或 Copy。密碼以標頭檢查碼保護開啟，不是加密檔案內容。
