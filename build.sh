#!/bin/bash
set -e
rm -rf dist && mkdir dist
dpkg-buildpackage -us -uc
mv ../hurd-socket-stat_* dist/ 2>/dev/null || true
echo "Build artifacts in dist/"
