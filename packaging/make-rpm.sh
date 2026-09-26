#!/bin/sh
# RPM from an already-built binary. rpmbuild is in the Fedora container.
set -eu
stage=$1
version=$2
arch=$3
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
top=$(mktemp -d)
mkdir -p "$top/BUILD" "$top/RPMS" "$top/SOURCES" "$top/SPECS" "$top/SRPMS"
cat > "$top/SPECS/67zip.spec" << EOF
Name: 67zip
Version: ${version}
# rpm rejects a version that is not dotted. Tags are v1.0.0; a bare git
# hash is rewritten by the caller. Hyphens are not legal in Version.

Release: 1
Summary: command-line archiver with a .67z container
License: MIT
BuildArch: ${arch}

%description
67zip uses the same commands as p7zip. Archives end in .67z.

%install
mkdir -p %{buildroot}/usr/bin %{buildroot}/usr/share/man/man1
install -m 755 ${stage}/usr/bin/67zip %{buildroot}/usr/bin/67zip
install -m 644 ${stage}/usr/share/man/man1/67zip.1 %{buildroot}/usr/share/man/man1/67zip.1

%files
/usr/bin/67zip
/usr/share/man/man1/67zip.1
EOF
rpmbuild --define "_topdir $top" -bb "$top/SPECS/67zip.spec"
mkdir -p "$root/dist"
cp "$top"/RPMS/*/*.rpm "$root/dist/"
echo "67zip: packed rpm into $root/dist"
