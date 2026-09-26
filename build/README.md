# Build scripts

Each file is the entry point for one host. POSIX hosts all end up in
`build-posix.sh`, which runs `make`. Hosts that cannot compile this tree
exit 2 and say why, so a scripted package build fails instead of producing
a binary that does not work.

| Script | Host | Builds |
| --- | --- | --- |
| `glibc-linux.sh` | Debian, Fedora, Ubuntu, Arch, RHEL | yes |
| `musl-linux.sh` | Alpine, Void-musl | yes |
| `nixos.sh` | NixOS, nixpkgs | yes, needs `pkg-config` and `PREFIX=$out` |
| `macos.sh` | macOS | yes, needs xz (`brew install xz`) |
| `ios.sh` | iPhoneOS / simulator | cross, needs `LZMA_PREFIX` |
| `android.sh` | Android NDK | cross, needs `ANDROID_NDK` and `LZMA_PREFIX` |
| `freebsd.sh` | FreeBSD | yes |
| `netbsd.sh` | NetBSD | yes |
| `openbsd.sh` | OpenBSD | yes |
| `dragonfly.sh` | DragonFly BSD | yes |
| `ghostbsd.sh` | GhostBSD | yes, same as FreeBSD |
| `solaris.sh` | Solaris 11 | yes, use `g++` and `gmake` |
| `illumos.sh` | illumos | yes |
| `omnios.sh` | OmniOS | yes, forwards to illumos |
| `haiku.sh` | Haiku | yes |
| `windows.sh` / `windows.ps1` | Windows | yes, needs a Win32 g++ and zlib/liblzma |
| `reactos.sh` / `reactos.ps1` | ReactOS | yes, same Win32 port as Windows |
| `freedos.sh` | FreeDOS | yes, needs DJGPP g++ (`CXX=i586-pc-msdosdjgpp-g++`) |
| `kolibrios.sh` | KolibriOS | source compiles; needs a g++ with a C++17 library (`CXX=kos32-g++`) |
| `redox.sh` | Redox OS | yes, C file API branch, needs the Redox toolchain |
| `serenity.sh` | SerenityOS | yes, C file API branch, needs the Serenity toolchain |

Run from the repository root or from this directory:

```
./build/glibc-linux.sh
PREFIX=/usr ./build/glibc-linux.sh
```

`DO_INSTALL=1` installs into `PREFIX` (default `/usr/local`).
