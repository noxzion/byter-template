# Byter template

Шаблон запрашивает framebuffer, очищает его в чёрный цвет и останавливается.
Он не выводит текст и не рисует другие элементы.

```sh
make
make boot
```

`make` скачивает нужные заголовки, загрузчик и упаковщик с GitHub, затем собирает
`build/byter-boot.img`. `make kernel` собирает только `build/kernel.elf`.
`make boot` отправляет RAM-образ в телефон через `fastboot boot`.

Нужны `clang`, `ld.lld`, `llvm-objcopy`, `python3`, `mkbootimg` и `fastboot`.
Шаблон можно собирать отдельно от основного репозитория; для `make boot` телефон
должен быть подключён в режиме Fastboot.

Исходник ядра находится в `src/main.c`; настройки компоновки и
Byter-конфигурация лежат в корне шаблона.
