# BerryBrowser CJK for BlackBerry 10

BerryBrowser CJK extends [sw7ft/chromium-for-bb10](https://github.com/sw7ft/chromium-for-bb10) with Chinese text rendering, BB10 system Pinyin and handwriting input, and a 720 × 720 rendering option with viewport compensation on the BlackBerry Passport.

The project ships patches against pinned upstream revisions. The complete Chromium checkout, SDK, and packaged runtime files are build prerequisites. See the [Chinese handoff and test notes](docs/BERRY_BROWSER_CJK.md).

## Download and install

**Requires a rooted BlackBerry 10 phone.** The first release was tested on a rooted BlackBerry Passport SQW100-4 running BB10 10.3.3.3216.

**必须使用已 root 的 BlackBerry 10 手机。** 首个发布版已在上述 Passport 真机上测试。

[Download the tested Build 125 BAR](https://github.com/guyiicn/berrybrower-cjk/releases/download/v3.0.2.125/BerryBrowserV3-3.0.2-build125-baseline.bar) · [Release notes](https://github.com/guyiicn/berrybrower-cjk/releases/tag/v3.0.2.125)

## Build

Install Docker, Git and Chromium's `depot_tools`. Run these commands from the repository root on a Linux host:

```sh
gclient sync --revision src@7435e232ccfb88db1e7b3c5d0576dc9551e3b3be
bash prepare-source.sh
docker build -t berry-browser-builder:phase1 .
bash build-phase1.sh
```

The pinned [dependency revisions](config/revisions.tsv) and checked-in GN flags reproduce the source and build configuration of the closeout workspace. The Dockerfile uses the pinned upstream BB10 toolchain image.

## Package

Supply the five runtime files listed in [the payload manifest](config/runtime-payload.sha256) in `src/deploy/berry-shell-bar/payload/`. Obtain them from your authorized BB10/QNX runtime bundle or upstream build distribution. The hashes identify the files used in the verified build.

```sh
(cd src/deploy/berry-shell-bar/payload && sha256sum -c ../../../../config/runtime-payload.sha256)
bash package-phase1.sh 126
unzip -tq BerryBrowserV3-3.0.2-build126-baseline.bar
```

Build 126 passed cross-compilation and BAR integrity checks. Device testing used earlier builds on a Passport SQW100-4 running BB10 10.3.3.3216. Chinese Pinyin and handwriting were confirmed in the browser. See the handoff for the scope of each test.

## Licenses

Source changes are distributed under the [Chromium BSD-style license](LICENSE). The bundled [Noto Sans SC font](overlay/deploy/berry-shell-bar/fonts/NotoSansSC-Regular.otf) uses the [SIL Open Font License](overlay/deploy/berry-shell-bar/Noto-Sans-CJK-OFL.txt).
