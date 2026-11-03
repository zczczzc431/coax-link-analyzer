# 构建与离线验证

## 软件基线

正式应用取自 699 r2 的冻结源码输入包，原包 SHA-256：

```text
17ca6558202d610c76d300293075bc1e5ad5c0715d53266656b66d82081768e2
```

导出前核验原包全部 214 个文件。本仓库只纳入 39 个正式 C 翻译单元、头文件/include 依赖闭包及 12 项精选测试。文件名中的编号用于对应开发记录，未将旧工作树当作新版底座。部分原始注释仍写 candidate；这描述的是开发时的隔离流程，不代表发布工具会自动部署它们。

源码来源与哈希见 `SOURCE_MANIFEST.json`。导入字节与 Git 的 LF 规范化分别记录校验值。

## 依赖

建议 Ubuntu 22.04 / GCC 11、Python 3.8+：

```bash
sudo apt-get install build-essential python3
# 仅当需要 ARM 编译或数字 RTL 回归
sudo apt-get install gcc-arm-linux-gnueabihf libc6-dev-armhf-cross iverilog
```

无额外 Python 包依赖。Linux framebuffer、input、mmap 等接口来自系统头文件。`libm` 与 pthread 由系统工具链提供。

## 命令

```bash
python3 tools/audit_release.py
python3 tools/build.py test
python3 tools/build.py app
python3 tools/build.py app --cc arm-linux-gnueabihf-gcc
python3 tools/build.py rtl-test
```

构建结果写入 `build/`，不写源工程、SD 或板卡。ARM 与 native 的 app 命令会写同名输出，后运行的覆盖前一个构建结果；这是可丢弃的编译输出。应用默认构建数据目录为 `/tmp/coax-link-analyzer`，与原板正式安装路径不同，因此不承诺生成二进制与历史发布二进制字节相同。

`test --sanitize` 可选地启用 ASan/UBSan；是否适用应结合当前编译器和平台。CI 默认执行普通回归。测试覆盖 FFT 数学、低频统计、示波器精度辅助函数、测量质量、频谱 MASK、公式波形包、向量计算、运行期事务、异步存储、证据流水线和 HOLD UI/配置。

测试程序不运行应用主循环，不操作 `/dev/mem`、LCD 或发生器。HOLD 回归复用实际应用辅助函数，并使用伪发生器回调和临时文件目录。

频谱 MASK 核心的旧版单元测试单独关闭后续资格扩展宏；正式应用、运行期合同和 HOLD 回归仍使用正式资格逻辑。证据流水线的存储故障注入回归显式启用 `SM693_TESTING`，该宏不用于正式应用构建。

## FPGA 参考与限制

`rtl-test` 展开六个原创 ADC 模块，并仿真历史双样点 DAC 数据通路；不验证实际 DAC SPI、时钟、电源、模拟输出或整机约束。`experiments/ddc765` 含 DSP48E1 实例，使用 Vivado 2020.2 的原语库，不纳入 Icarus 回归。

DDC 实验目录的 `impl765.tcl` 是隔离 OOC 综合与实现脚本，使用 XC7Z020-2、200 MHz/30 MHz 时钟；会生成 `physical765/`，并拒绝覆盖已存在结果。它不接生产 BD、PS、LCD、mailbox 和板级 I/O。运行前应独立审查资源、CDC、复位及约束；本次开源打包不运行 Xilinx 工具。

完整整机重建还需要合法取得厂商工程、IP 配置、板级约束及匹配的软件/硬件接口，并重新进行系统级验证。本仓库不附带推测或未经验证的“一键生成正式固件”命令。
