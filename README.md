# Byter template assets

The `assets` branch contains the Byter boot shim sources and prebuilt ARM64
shim image, ABI headers, Android boot image packer, and the AOSP `mkbootimg`
Python utility used by the standalone template. The template Makefile fetches
this branch into `.assets/`.

`tools/mkbootimg.py` and `tools/gki/generate_gki_certificate.py` are from the
Android Open Source Project and are licensed under Apache-2.0; see
`licenses/Apache-2.0.txt`. `Boot/core/font8x16_basic.h` contains CC0 font data.
