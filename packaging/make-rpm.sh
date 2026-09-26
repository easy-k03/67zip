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

# Binary package. rpmbuild otherwise looks for a source tarball and a
# build subdirectory that this spec never creates.
%prep
%build

%install
# The staged tree is passed as the build root. Copying here would look
# for files under a second directory rpmbuild creates on its own.

%files
/usr/bin/67zip
/usr/share/man/man1/67zip.1
EOF
# Fedora's rpmbuild does not substitute %{buildroot} inside %install the
# way a from-source spec expects, and it looks for the installed files
# under a directory it creates itself. Hand it the staged tree as the
# build root so %install is a no-op and %files is checked against files
# that already exist.
rpmbuild --define "_topdir $top" \
    --define "buildroot $stage" \
    --buildroot "$stage" \
    -bb "$top/SPECS/67zip.spec"
mkdir -p "$root/dist"
cp "$top"/RPMS/*/*.rpm "$root/dist/"
echo "67zip: packed rpm into $root/dist"
