# Byter template

Шаблон запрашивает framebuffer, очищает его в чёрный цвет и останавливается.
Он не выводит текст и не рисует другие элементы.

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

Исходник ядра находится в `src/main.c`; настройки компоновки и
Byter-конфигурация лежат в корне шаблона.
