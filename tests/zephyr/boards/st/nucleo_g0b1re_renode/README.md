# nucleo_g0b1re on a modified Renode model

An out-of-tree Zephyr board for running Elemental's test image on an ARMv6-M
CPU, a Cortex-M0+, under Renode
([docs/build.md](../../../../../docs/build.md#tests-on-zephyr)). It is the
in-tree `nucleo_g0b1re` (STM32G0B1RE: 512 KB flash, 144 KB RAM) with:

- [stm32g0b1.repl](support/stm32g0b1.repl): Renode's stock STM32G0 model,
  which is an STM32G071, with flash and RAM enlarged to the G0B1's and the
  CPU declared a Cortex-M0+. Nothing else is changed.
- [The devicetree](nucleo_g0b1re_renode.dts): the stock one, with the HSI48
  clock off, because the model cannot report it ready.

A run on it is an emulator run on a modified model, not a hardware run.
Peripherals the model lacks or models loosely are not exercised by the
tests, which use the core, SysTick, and USART2.

A separate board, rather than an extension of `nucleo_g0b1re`, because
Twister takes a board's emulator from its own platform file and does not
let an extension replace it.
