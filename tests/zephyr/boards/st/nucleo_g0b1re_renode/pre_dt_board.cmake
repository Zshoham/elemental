# The devicetree reuses the stock board's (nucleo_g0b1re_renode.dts).
list(APPEND DTS_EXTRA_CPPFLAGS "-I${ZEPHYR_BASE}/boards/st/nucleo_g0b1re")
