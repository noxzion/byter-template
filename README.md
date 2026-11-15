# Byter template

Шаблон запрашивает framebuffer, HHDM и один модуль из `byter.cfg`. После
handoff ядро проверяет путь `assets/probe.bin`, размер 20 байт и каждый байт
содержимого через переданные HHDM-указатели. Полностью зелёный экран означает,
что проверка прошла; красный — что ответ или содержимое модуля не совпало.

```sh
make
make boot
```

`make` скачивает ветку `assets` этого репозитория в `.assets/` и собирает
`build/byter-boot.img`. В ветке находятся Byter ABI, готовый shim, упаковщик и
`mkbootimg`; template не зависит от исходного репозитория Byter. `make kernel`
собирает только `build/kernel.elf`. `make boot` отправляет RAM-образ в телефон
через `fastboot boot`.

Нужны `git`, `clang`, `ld.lld`, `python3` и `fastboot`. Для `make boot` телефон
должен быть подключён в режиме Fastboot.

Исходник ядра находится в `src/main.c`, бинарный fixture модуля — в
`assets/probe.bin`; настройки компоновки и Byter-конфигурация лежат в корне
шаблона.
