#!/bin/sh
# RPM from an already-built binary.
# rpmbuild on Fedora creates its own build root and then checks %files
# against that empty directory, ignoring a --buildroot that already has
# the files. Build the cpio payload directly instead.
set -eu
stage=$1
version=$2
arch=$3
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

# rpm rejects a version that is not dotted. The caller already rewrote a
# bare git hash to 0.0.0.
name="67zip-${version}-1.${arch}.rpm"
payload=$(mktemp -d)
mkdir -p "$payload/usr/bin" "$payload/usr/share/man/man1"
cp "$stage/usr/bin/67zip" "$payload/usr/bin/67zip"
cp "$stage/usr/share/man/man1/67zip.1" "$payload/usr/share/man/man1/67zip.1"
chmod 755 "$payload/usr/bin/67zip"

# rpmbuild is still the tool that knows the lead, signature and header
# layout. Give it a spec whose %install copies from a path that exists
# inside the container, and do not override buildroot.
top=$(mktemp -d)
mkdir -p "$top/BUILD" "$top/RPMS" "$top/SOURCES" "$top/SPECS" "$top/SRPMS"
cat > "$top/SPECS/67zip.spec" << EOF
Name: 67zip
Version: ${version}
Release: 1
Summary: command-line archiver with a 67z container
License: MIT
BuildArch: ${arch}

%description
67zip uses the same commands as p7zip.

# brp-compress renames the man page to 67zip.1.gz, then %files cannot
# find the name this spec lists. Leave the page uncompressed.
%define __brp_compress %{nil}

%prep
%build

%install
mkdir -p %{buildroot}/usr/bin %{buildroot}/usr/share/man/man1
cp ${payload}/usr/bin/67zip %{buildroot}/usr/bin/67zip
cp ${payload}/usr/share/man/man1/67zip.1 %{buildroot}/usr/share/man/man1/67zip.1
chmod 755 %{buildroot}/usr/bin/67zip

%files
/usr/bin/67zip
/usr/share/man/man1/67zip.1

%changelog
* Sat Sep 26 2026 67zip <67zip@localhost> - ${version}-1
- Package the 67zip binary.
EOF
rpmbuild --define "_topdir $top" -bb "$top/SPECS/67zip.spec"
mkdir -p "$root/dist"
cp "$top"/RPMS/*/*.rpm "$root/dist/"
rm -rf "$top" "$payload"
echo "67zip: packed rpm into $root/dist"
