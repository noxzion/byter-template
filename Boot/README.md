# Boot

Телефонная загрузочная цепочка Byter: ARM64 shim запускает AArch64 ELF из
Android boot image, переданного командой `fastboot boot`.

```text
arch/aarch64/entry.S       ARM64 Image entry и переход к ядру на EL1
arch/aarch64/page_tables.c таблицы страниц и права PT_LOAD
arch/aarch64/image.ld      linker script shim
core/elf_loader.c          проверка и загрузка ELF, ответы Byter ABI
include/byter.h           публичные структуры и ID Byter ABI
../tools/                  утилиты, включая упаковщик Android boot image
```

Собрать только загрузчик можно из корня проекта командой `make -C Boot shim`;
результат `Boot/build/boot-shim.bin` используется потребляющим проектом.
Сборка ядра и Android boot image здесь не выполняется. Для самостоятельной
сборки и запуска используй [Byter template](https://github.com/noxzion/byter-template),
который получает ABI, shim и упаковщик из ветки `assets` этого репозитория.

Формат конфигурации Android boot image поддерживает несколько ELF и cmdline
через повторяющиеся блоки `entry=`:

```ini
entry=Byter example
kernel=build/kernel.elf
cmdline=byter.example=1
# Repeat module= to attach files to this entry.
# module=build/initrd.cpio

entry=Recovery
kernel=build/recovery.elf
cmdline=mode=recovery
```

Каждый `entry=` начинает новый блок; следующие `kernel=` и `cmdline=` относятся
к нему. Пути kernel и module относительны к конфигу. Повторяющиеся
`module=PATH` прикрепляют файлы к текущему пункту; всего поддерживается до
128 модулей в каталоге. Название показывается в меню; Volume Up/Down перемещают
выбор, Power запускает выбранный ELF, Essential Key запрашивает возврат в
Fastboot, таймаут выбирает первый пункт.
Framebuffer shim получает из DTB-узла `simple-framebuffer`. Если ABL его не
передаёт, используется изолированный проверенный профиль A059 в
`platform/a059/framebuffer.c`. Shim отвечает на bootloader-info, executable
cmdline, DTB, framebuffer, HHDM, memory-map, executable-address и revision-0
module requests. Module response содержит файлы выбранного пункта; payload
встраивается в Android kernel Image и помечается в memory map как
`EXECUTABLE_AND_MODULES`. Строки module пока пустые. Карта памяти строится по
RAM и known reservations из DTB и однозначно переведённым device `reg` из DTB,
с отдельными типами для областей загрузчика, ELF-ядра, модулей и framebuffer.
Локальный диагностический `example` запрашивает ровно один модуль из
`example/module_payload.bin`, сверяет его path, size и содержимое, а зелёный
маркер в правом верхнем углу подтверждает запрос и сохранение страниц модуля.
Shim включает MMU и передаёт ядру identity map для RAM и boot data,
а сегменты ядра отображает по ELF VMA с правами `PT_LOAD`. HHDM отображает RAM
первые 4 GiB, RAM из DTB и framebuffer. Shim сразу пишет ход загрузки в
framebuffer встроенным 8×16 шрифтом, не очищая весь экран; при остановке остаётся
последняя строка.

Перед передачей управления shim показывает меню A059 по центру над логотипом
Nothing, сохраняя исходный фон splash screen. Volume Up/Down перемещают выбор,
Power запускает пункт, а Essential Key записывает reboot reason в PMK8550 SDAM
и вызывает PSCI reset для возврата в bootloader Fastboot. Опрос использует SPMI
PMIC Arbiter v7 для PMK8550 PON и PM7550 GPIO, а Essential Key читает через
TLMM GPIO71. Запись SDAM выполняется только по нажатию Essential Key и
проверяется чтением; если перезагрузка не состоялась, прежнее значение
восстанавливается. Если PMIC Arbiter недоступен или имеет неподдерживаемую
версию, shim выбирает первый пункт и продолжает загрузку.

Memory map не является полной картой физических областей: она включает только
RAM и известные reservations/device ranges из DTB. В диагностическом примере
голубой маркер означает, что подходящего внешнего диапазона выше 4 GiB в DTB
нет; пурпурный подтверждает проверку такого диапазона через identity map и HHDM.
Полной совместимости с Limine Boot Protocol ещё нет.
