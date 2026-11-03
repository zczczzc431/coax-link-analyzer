# Coax Link Analyzer

A Zynq-7000 research prototype for coaxial RF-link condition monitoring and intermittent-fault evidence capture. [中文说明](README.md)

The project combines controlled excitation, normal-reference learning, amplitude/frequency/spectral-mask checks, event statistics, key-frame capture and CSV/report workflows. The Linux application includes scope, FFT, generator controls, sweep and manual two-stage reflection workflows.

## What is released

- The verified 699 r2 application source dependency closure and twelve selected existing offline regressions.
- Original ADC capture, trigger-ring, mailbox, CDC-bridge and link-event modules.
- A historical 400 MSPS dual-sample DAC reference implementation and its matching CLI source.
- Isolated experimental DDC/filter/decimation/frozen-buffer sources under `experiments/ddc765`.
- Documentation, source checksums and offline CI.

This is **a source snapshot, not a complete deployable board firmware release**. Vendor Vivado IP, board examples, schematics, BSPs, boot files and SD images are excluded. The historical DAC reference does not reproduce every later ROM ECO in the stable bitstream.

## Build without hardware

```bash
python3 tools/audit_release.py
python3 tools/build.py test
python3 tools/build.py app
# Optional: installed ARM compiler and Icarus Verilog
python3 tools/build.py app --cc arm-linux-gnueabihf-gcc
python3 tools/build.py rtl-test
```

Use Linux, Python 3.8+, GCC, pthread and libm. Tests use synthetic inputs and temporary directories. The application is compiled but never executed, installed or deployed by these commands.

## Important status

The replacement hardware still has an unresolved DAC initialization/SPI-response problem. Generator output has **not** been restored on that setup. CUSTOM provides formula editing, preview and guarded backend work; arbitrary analog output is not accepted. The experimental narrowband DDC has isolated simulation/OOC evidence but no accepted end-to-end 1 kHz-RBW measurement. HOLD is a recovery-duration setting, not a sampling interval or a guaranteed transient capture duration.

See [status](docs/STATUS.md), [build](docs/BUILD.md), [hardware](docs/HARDWARE.md) and [third-party dependencies](THIRD_PARTY_NOTICES.md). Original released work is licensed under [MIT](LICENSE).
