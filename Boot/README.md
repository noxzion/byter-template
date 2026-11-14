# Boot

Телефонная загрузочная цепочка Byter: ARM64 shim запускает AArch64 ELF из
Android boot image, переданного командой `fastboot boot`.

```text
arch/aarch64/entry.S       ARM64 Image entry и переход к ядру на EL1
arch/aarch64/page_tables.c таблицы страниц и права PT_LOAD
arch/aarch64/image.ld      linker script shim
core/elf_loader.c          проверка и загрузка ELF, ответы Byter ABI
include/byter.h           публичные структуры и ID Byter ABI
../example/                 пример ядра
../tools/                  упаковщик Android boot image
```

Собрать образ можно из корня проекта командой `make`; результат появится в
`Boot/build/byter-boot.img`. `make boot` дополнительно запускает его через
Fastboot. Передаётся только временный RAM-образ, разделы не прошиваются.
`Boot/byter.cfg` задаёт ELF и `cmdline`; другой файл можно выбрать через
`make boot CONFIG=Boot/dev.cfg`, а `ELF=/путь/ядро.elf` переопределяет конфиг.
Сейчас конфиг читается при сборке образа и его cmdline встраивается в RAM-образ.

Пример ядра в `example/main.c` проверяет ответы framebuffer, memory map и
executable address, затем рисует цветной квадрат.
Framebuffer shim получает из DTB-узла `simple-framebuffer`. Если ABL его не
передаёт, используется изолированный проверенный профиль A059 в
`platform/a059/framebuffer.c`. Shim отвечает на bootloader-info, DTB,
framebuffer и memory-map запросы. Карта памяти строится по RAM и known
reservations из DTB и однозначно переведённым device `reg` из DTB, с отдельными
типами для областей загрузчика, ELF-ядра и framebuffer. Shim включает MMU и
передаёт ядру identity map для RAM и boot data,
а сегменты ядра отображает по ELF VMA с правами `PT_LOAD`. HHDM отображает RAM
первые 4 GiB, RAM из DTB и framebuffer. Shim сразу пишет ход загрузки в
framebuffer встроенным 8×16 шрифтом, не очищая весь экран; при остановке остаётся
последняя строка.
Memory map не является полной картой физических областей: она включает только
RAM и известные reservations/device ranges из DTB. В диагностическом примере
голубой маркер означает, что подходящего внешнего диапазона выше 4 GiB в DTB
нет; пурпурный подтверждает проверку такого диапазона через identity map и HHDM.
Полной совместимости с Limine Boot Protocol ещё нет.
