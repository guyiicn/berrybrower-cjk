#!/usr/bin/env bash
set -euo pipefail

build_id=${1:-112}
[[ "$build_id" =~ ^[0-9]+$ ]]
build_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
docker run --rm --network none \
    --mount "type=bind,src=$build_dir,dst=/work" \
    --env "BERRY_BASELINE_BUILD=$build_id" \
    --entrypoint /bin/bash berry-browser-builder:phase1 -c '
    set -eo pipefail
    source /root/bbndk/gcc9/bb10-gcc9/env.sh

    build=/work/src/out/qnx-arm
    deploy=/work/src/deploy
    stage=$(mktemp -d)
    app="$stage/berry-shell-bar"
    mkdir -p "$app/payload"
    cp "$deploy/build-v3-bar.sh" "$stage/"
    cp "$deploy/berry-shell-bar/"{launcher.c,bar-descriptor-v3.xml,home.html,youtube-search.html,berry_ime_helper.c,berry_ime_protocol.h} "$app/"
    cp -r "$deploy/berry-shell-bar/"{icons,fonts,Noto-Sans-CJK-OFL.txt} "$app/"
    cp "$deploy/berry-shell-bar/payload/"{libstdc++.so.6,libgcc_s.so.1,libm.so.2,libasound.so.2,root_store.certs} "$app/payload/"
    cp "$build/"{content_shell.pak,shell_resources.pak,ui_resources_100_percent.pak,icudtl.dat} "$app/payload/"
    sed -i "s/com\.sw7ft\.BerryShellV3/com.sw7ft.BerryShellV3Baseline/g; s/<name>Berry Browser<\//<name>Berry Browser Baseline<\//" "$app/bar-descriptor-v3.xml"

    sed -i "s|<buildId>112</buildId>|<buildId>$BERRY_BASELINE_BUILD</buildId>|" "$app/bar-descriptor-v3.xml"
    sed -i "s/build 112 (PGO+ThinLTO O2)/baseline build $BERRY_BASELINE_BUILD/; s/berry_ship_log(112)/berry_ship_log($BERRY_BASELINE_BUILD)/" "$app/launcher.c"

    BERRY_CONTENT_SHELL="$build/content_shell" bash "$stage/build-v3-bar.sh"
    cp "$app/BerryBrowserV3-3.0.2-build$BERRY_BASELINE_BUILD.bar" "/work/BerryBrowserV3-3.0.2-build$BERRY_BASELINE_BUILD-baseline.bar"
'
