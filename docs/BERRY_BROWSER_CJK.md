# BerryBrowser CJK 交接文档

BerryBrowser CJK 为 BlackBerry Passport 上的 Berry Browser 增加中文字体、系统拼音和手写输入。验收范围是 BB10 10.3.3.3216 上的 Passport，型号 SQW100-4。本文以已安装的 3.0.2.125 为真机记录。

## 使用与验收

1. 在 BB10 的输入语言设置里启用中文拼音或手写。
2. 打开 Berry Browser，在首页搜索框或网页普通文本框中切换到中文。拼音输入 `women` 后选「我们」，或用系统手写面板写字并选中候选。
3. 选词上屏后连续按两次退格。预期依次是「我们」「我」和空输入框。仍在拼音组字时，退格应当编辑拼音串。
4. 组字中的拼音沿用网页文字颜色并显示细下划线。 Build 125 将默认的亮黄底换成透明样式，便于在深色首页和浅色网页中辨认组字状态。

真机已确认中文显示、手写提交，以及拼音候选上屏。Build 124 的输入法子进程在真机收到「我们」提交后，将两次退格都交回浏览器。Build 125 的应用安装记录、版本和组字样式变更已核对。退格逐字删除和下划线外观仍以浏览器中的实测为准。独立探针检查了系统组字、提交和进程间通信。

验证重点是中文。其他语言、设备型号和所有网页输入组件尚无相同范围的真机验证。密码框和曾标记为密码的输入框不启用此系统输入法路径。

## 清晰度对比

Passport 的屏幕输出为 1440 × 1440。默认预设先绘制 540 × 540，再缩放到屏幕。要对比清晰度，打开 Berry Browser 设置中的 **Display**，选择 **720² sharp**，然后选 **Apply & Restart**。720 档对网页应用 720/540 倍的页面缩放，使常见网页的字号和排版接近默认档。原生窗口尺寸和触摸坐标仍使用实际绘制尺寸。

720 档绘制的像素量是 540 档的约 1.78 倍。可以在同一网页对照文字、滚动流畅度和点按位置。回到 **540² balanced** 后重新启动即可恢复默认渲染尺寸。

## 代码位置

- `ui/ozone/platform/qnx_screen/qnx_system_input.cc` 管理原生输入法会话。浏览器通过本地套接字发送按键及可共享的输入框上下文，接收组字、提交和按键转发。
- `deploy/berry-shell-bar/berry_ime_helper.c` 以独立进程加载 BB10 的 `libinput_client.so.1`。子进程使用系统运行库，浏览器继续使用自己的 QNX 工具链运行库。混用两套运行库在 Passport 上实测会造成加载失败或崩溃。
- `deploy/berry-shell-bar/berry_ime_protocol.h` 定义浏览器与子进程间的固定消息格式。
- `ui/ozone/platform/qnx_screen/qnx_input_method.cc` 把系统事件接到 Chromium 网页输入框。网页组字的透明背景和下划线也在此处设置。
- `content/shell/browser/shell_platform_delegate_qnx.cc` 负责原生地址栏的输入法接入和 720 档网页缩放。
- `deploy/berry-shell-bar/fonts/NotoSansSC-Regular.otf` 随 BAR 安装，为中文提供字形。字体按 [SIL Open Font License 1.1](../overlay/deploy/berry-shell-bar/Noto-Sans-CJK-OFL.txt) 分发。

默认关闭专项输入诊断。排查时可以在 BB10 共享存储的 `misc` 目录创建 `berry-ime.debug` 并重新启动浏览器。筛选 `berry-kbd.log` 中以 `BerryIME:` 开头的行，可查看事件类型、长度和状态。专项日志不记录键值或输入文本。结束后删除标记并重启。

## 编译与打包

使用 Linux、Docker、已同步的 Chromium 依赖和 QNX ARM 工具链。工作区有 `src/` 源码目录，外层放置 `Dockerfile`、`build-phase1.sh`、`package-phase1.sh` 与 `ld-wrapper.sh`。源码基线为 `sw7ft/chromium-for-bb10` 的 `7435e232ccfb88db1e7b3c5d0576dc9551e3b3be`，还需应用项目补丁并准备 `src/out/qnx-arm/args.gn`。若使用补丁仓库，先按其 README 准备工作区和依赖。

在工作区外层运行以下命令。将 `BUILD_ID` 设为目标版本的数字后缀。同一设备上升级时使用高于当前安装版本的值。

```sh
docker build -t berry-browser-builder:phase1 .
bash build-phase1.sh
BUILD_ID=126
bash package-phase1.sh "$BUILD_ID"
unzip -tq "BerryBrowserV3-3.0.2-build${BUILD_ID}-baseline.bar"
unzip -p "BerryBrowserV3-3.0.2-build${BUILD_ID}-baseline.bar" \
  META-INF/MANIFEST.MF | grep -E '^(Package-(Name|Id|Version)|Application-Name):'
```

编译目标是 `content/shell:content_shell`，日志写入外层 `phase1-build.log`。打包会编译 BB10 输入法子进程，并将浏览器可执行文件、同次构建的 V8 快照、资源、字体和运行库一起放入 BAR。使用已授权的 BB10 安装方式升级。安装后核对版本，再验证浏览器实际启动、中文输入和渲染档位。
