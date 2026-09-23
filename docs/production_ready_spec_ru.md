# Техническое задание: production-ready portable C shell

## 1. Назначение

Разработать независимый shell-модуль на языке C с пользовательским поведением,
близким к Zephyr Shell, который можно подключать к:

- FreeRTOS;
- bare-metal проектам;
- UART и USB CDC;
- TCP-соединению или UART-to-network bridge;
- host-приложению Windows/Linux для автоматической и ручной проверки.

Модуль должен предоставлять редактор командной строки, историю, навигацию
стрелками, Tab completion, вложенные команды, справку, безопасную работу с
секретными аргументами и удобный API регистрации команд. Ядро не должно
зависеть от конкретной ОС, драйвера UART, socket API или heap allocator.

Под словом "идеальный" в этом документе понимается не POSIX shell и не полная
копия всех функций Zephyr, а готовый к повторному использованию embedded shell:
детерминированный, тестируемый, документированный, переносимый и устойчивый к
ошибочным входным данным.

## 2. Итоговый результат

После завершения работ репозиторий должен содержать:

1. Независимую статическую библиотеку shell core.
2. Публичные заголовочные файлы с документированным API.
3. Статическую модель памяти без обязательного `malloc`.
4. Удобную модель добавления корневых и вложенных команд.
5. Полный редактор строки с ANSI/VT100 клавишами.
6. Историю команд с корректным повторным выполнением строк с кавычками.
7. Zephyr-подобный Tab completion.
8. Транспортный контракт с обработкой ошибок и partial write.
9. Рабочие адаптеры host, FreeRTOS UART, bare-metal UART и TCP.
10. Автоматические unit, integration, transport и fuzz-тесты.
11. Отчёт покрытия и quality gates.
12. Интерактивный terminal demo для финальной проверки пользователем.
13. Документацию по интеграции, конфигурации памяти и добавлению команд.
14. Версионируемый release package без зависимостей от приложения.

## 3. Границы проекта

### 3.1. Обязательные функции

- побайтовый и пакетный ввод;
- `CR`, `LF` и `CRLF` без двойного выполнения;
- редактирование строки;
- Left, Right, Up, Down, Home, End, Delete и Backspace;
- восстановление черновика после просмотра истории;
- Ctrl+C, Ctrl+U, Ctrl+W и Ctrl+L;
- статическая история команд;
- корневые и вложенные команды;
- compact help и contextual help;
- обязательные и необязательные аргументы;
- quoted arguments и escaping;
- completion команд и подкоманд;
- optional completion значений аргументов;
- echo on/off/status;
- скрытие секретов во время ввода и в истории;
- несколько независимых экземпляров shell;
- отсутствие обязательного heap;
- host, FreeRTOS, bare-metal и TCP integration examples.

### 3.2. Не входят в обязательный объём

- POSIX-совместимые pipes и redirects;
- запуск процессов;
- environment variable expansion;
- command substitution;
- job control;
- файловая система;
- TLS и аутентификация TCP-клиентов внутри shell core;
- владение аппаратным UART-драйвером внутри shell core;
- прямой вызов FreeRTOS API из core-библиотеки.

Эти возможности могут добавляться позже отдельными расширениями.

## 4. Исходное состояние и обязательные исправления

Текущий прототип использовать как основу, но до добавления новых функций
исправить следующие блокирующие дефекты:

1. История не должна терять кавычки и изменять `argc/argv` при повторном запуске.
2. Последовательность `CRLF` должна считаться одним Enter.
3. Completion должен учитывать позицию курсора, а не только конец строки.
4. Точная команда после Tab должна получать пробел или переходить к completion
   подкоманд.
5. Встроенные команды должны находиться в общей таблице и участвовать в Tab.
6. Ограничение списка совпадений не должно влиять на вычисление общего префикса.
7. Runtime limits должны соответствовать реальным размерам внутренних буферов.
8. `history_depth` должен иметь один источник истины.
9. Sensitive-команды должны скрывать секрет при вводе, а не только в истории.
10. `write` должен возвращать результат записи и поддерживать partial write.
11. FreeRTOS-пример должен стать исполняемым адаптером, а не псевдокодом.
12. Host terminal должен работать в raw mode и реально принимать стрелки и Tab.

Каждый дефект сначала закрепить regression-тестом, затем исправить.

## 5. Требования к архитектуре

### 5.1. Слои

```text
Application commands
        |
Command registry and dispatcher
        |
Tokenizer / completion / history / line editor
        |
Portable shell core
        |
Transport interface
        |
Host | FreeRTOS UART | Bare metal | TCP | USB CDC
```

Core-библиотека должна знать только о входных байтах и транспортных callback.
Она не создаёт task/thread, не обращается к UART и не вызывает socket API.

### 5.2. Рекомендуемая структура каталогов

```text
shell/
  include/
    sh_config.h
    sh_status.h
    sh_command.h
    sh_transport.h
    sh_shell.h
  src/
    sh_shell.c
    sh_input.c
    sh_ansi.c
    sh_line_editor.c
    sh_tokenizer.c
    sh_command.c
    sh_completion.c
    sh_history.c
    sh_output.c
    sh_builtin.c
  ports/
    host/
      sh_port_windows.c
      sh_port_posix.c
    freertos/
      sh_port_freertos.c
      sh_port_freertos.h
    baremetal/
      sh_port_baremetal.c
      sh_port_baremetal.h
    tcp/
      sh_port_tcp.c
      sh_port_tcp.h
  examples/
    host_terminal_shell.c
    command_registration.c
    freertos_uart_shell.c
    freertos_tcp_shell.c
    baremetal_uart_shell.c
  tests/
    unit/
    integration/
    transport/
    fuzz/
```

Все файлы, принадлежащие shell, должны оставаться внутри `shell/`.

### 5.3. Правила зависимостей

- `shell/src` использует только C99 и стандартные заголовки.
- `shell/src` не включает FreeRTOS, ESP-IDF, Zephyr, POSIX или Windows headers.
- Platform headers разрешены только внутри соответствующего `shell/ports`.
- Application code не обращается к внутренним функциям `sh_*_internal`.
- Один `sh_t` обслуживается только одним execution context одновременно.
- Для нескольких UART/TCP сессий создаются отдельные экземпляры `sh_t`.
- Thread safety и ownership должны быть явно описаны в публичной документации.

## 6. Конфигурация и модель памяти

### 6.1. Основной режим

Основным режимом является static allocation:

- shell instance принадлежит приложению;
- line buffer принадлежит приложению;
- argv buffer принадлежит приложению;
- history storage принадлежит приложению;
- completion scratch storage принадлежит приложению или shell instance;
- core не вызывает `malloc`, `calloc`, `realloc` и `free`.

### 6.2. Удобное объявление storage

Предоставить macro для типового случая:

```c
SH_DEFINE_INSTANCE(app_shell,
                   256,  /* line bytes */
                   32,   /* argv entries */
                   16,   /* history entries */
                   64);  /* visible completion matches */
```

Macro должен создавать память, но не регистрировать transport или команды.
Также оставить ручной API для проектов с нестандартным размещением в памяти.

### 6.3. Конфигурационные ограничения

Конфигурация должна проверяться при `sh_init()`:

- line capacity не меньше двух байт;
- `max_line_len < line_buffer_size` или используется однозначно описанная
  семантика capacity;
- `max_args <= argv_capacity`;
- history depth соответствует фактическому storage;
- размер history slot достаточен для NUL terminator;
- completion capacity соответствует storage;
- несовместимая конфигурация возвращает `SH_ERR_INVALID_CONFIG`.

Нельзя иметь отдельные значения `cfg.history_depth` и аргумент
`history_depth`, которые могут расходиться.

### 6.4. Отчёт памяти

Пример и документация должны показывать:

- `sizeof(sh_t)`;
- RAM line/argv/history/completion buffers;
- ROM command tables и help strings;
- конфигурацию minimum, default и extended.

Сборка должна предоставлять `sh_memory_report` host utility или тест, который
печатает эти значения.

## 7. Публичный API

### 7.1. Основные функции

```c
sh_status_t sh_init(sh_t *shell,
                    const sh_config_t *config,
                    const sh_storage_t *storage,
                    const sh_transport_t *transport,
                    const sh_registry_t *registry);

sh_status_t sh_start(sh_t *shell);
sh_status_t sh_stop(sh_t *shell);
sh_status_t sh_feed_byte(sh_t *shell, uint8_t byte);
sh_status_t sh_feed(sh_t *shell, const uint8_t *data, size_t len);
sh_status_t sh_execute_line(sh_t *shell, const char *line);
sh_status_t sh_execute_argv(sh_t *shell, size_t argc, char **argv);
```

`sh_feed()` должен обрабатывать произвольное разбиение входа: один байт,
несколько байтов или несколько команд в одном блоке.

### 7.2. Публичный вывод для command handlers

```c
sh_status_t sh_write(sh_t *shell, const void *data, size_t len);
sh_status_t sh_puts(sh_t *shell, const char *text);
sh_status_t sh_printf(sh_t *shell, const char *format, ...);
```

Application handler не должен обращаться к `shell->cfg.write` напрямую.
Форматированный вывод можно отключить compile-time флагом.

### 7.3. Статусы

Минимальный набор:

```c
SH_OK
SH_ERR_INVALID_ARG
SH_ERR_INVALID_CONFIG
SH_ERR_LINE_TOO_LONG
SH_ERR_TOO_MANY_ARGS
SH_ERR_UNTERMINATED_QUOTE
SH_ERR_UNKNOWN_COMMAND
SH_ERR_AMBIGUOUS
SH_ERR_INCOMPLETE_COMMAND
SH_ERR_TRANSPORT
SH_ERR_WOULD_BLOCK
SH_ERR_TRUNCATED
SH_ERR_BUSY
```

Статус command handler должен передаваться вызывающему коду без потери.

## 8. Модель команд

### 8.1. Command descriptor

```c
typedef struct sh_cmd {
    const char *name;
    const char *summary;
    const char *usage;
    const char *help;
    sh_cmd_handler_t handler;
    const struct sh_cmd *children;
    size_t child_count;
    uint16_t min_args;
    uint16_t max_args;
    uint32_t flags;
    void *user_ctx;
    sh_completion_provider_t completion;
} sh_cmd_t;
```

### 8.2. Контракт handler

Для совместимости мышления с Zephyr принять единый контракт:

- `argv[0]` содержит имя вызванной leaf-команды;
- payload начинается с `argv[1]`;
- `argc` включает `argv[0]`;
- `min_args` и `max_args` также включают `argv[0]`;
- указатели `argv` действительны только во время handler call;
- handler не сохраняет их без копирования;
- handler может писать только через публичные `sh_write/sh_printf`.

Если сохраняется текущий контракт, где handler получает только payload, это
должно быть осознанно выбрано, переименовано и полностью документировано. Два
варианта одновременно не допускаются.

### 8.3. Macro API

Предоставить читаемые macro:

```c
SH_CMD(name, usage, summary, handler)
SH_CMD_ARG(name, usage, summary, handler, min_args, max_args)
SH_CMD_SUB(name, usage, summary, children)
SH_CMD_CTX(name, usage, summary, handler, ctx)
SH_CMD_SENSITIVE(name, usage, summary, handler, first_secret_arg)
SH_CMD_HIDDEN(name, usage, summary, handler)
SH_CMD_END
```

Macro должны создавать обычные `const` структуры без зависимости от linker
sections. Linker-section registration можно добавить как optional extension.

### 8.4. Компоновка команд разных модулей

Поддержать несколько command sets:

```c
static const sh_command_set_t command_sets[] = {
    SH_COMMAND_SET(system_commands),
    SH_COMMAND_SET(wifi_commands),
    SH_COMMAND_SET(tag_commands),
};
```

Registry при инициализации должен:

- проверить NULL name;
- проверить пустые имена;
- проверить дубликаты на одном уровне;
- проверить конфликт с built-ins;
- проверить `min_args <= max_args`;
- проверить children/count;
- проверить максимальную глубину;
- вернуть точную диагностическую ошибку.

### 8.5. Большое количество аргументов

- Default: 32 элемента `argv`, включая имя leaf-команды.
- Extended profile: минимум 64 элемента.
- Compile-time override должен работать во всех подсистемах, включая Tab.
- Runtime limit не может превышать реально предоставленное storage.
- Ровно `max_args` должно работать.
- `max_args + 1` должно завершаться ошибкой до вызова handler.
- Ошибка не должна повреждать следующую команду.

## 9. Tokenizer

Обязательная грамматика:

- spaces и tabs как разделители;
- repeated whitespace;
- double-quoted arguments;
- empty quoted argument `""`;
- escaped quote `\"`;
- escaped backslash `\\`;
- escaped space `a\ b`;
- concatenated quoted/unquoted fragments, если это явно принято в контракте;
- запрет embedded NUL через length-based feed API;
- точная ошибка unterminated quote.

Нужно выбрать и документировать поддержку single quotes. Предпочтительный
вариант для удобства: поддержать `'text with spaces'`, без expansion.

Tokenizer должен работать in-place или с caller-provided scratch buffer. Он не
должен выходить за границы даже на повреждённом или случайном вводе.

## 10. Line editor и ANSI decoder

### 10.1. Клавиши

Обязательные действия:

| Ввод | Действие |
|---|---|
| printable ASCII | вставить символ в позицию курсора |
| Backspace | удалить символ слева |
| Delete | удалить символ под курсором |
| Left/Right | переместить курсор на один символ |
| Home/End | перейти в начало/конец |
| Up/Down | навигация по истории |
| Tab | completion |
| Enter | выполнить строку |
| Ctrl+C | отменить строку и показать новый prompt |
| Ctrl+U | удалить строку |
| Ctrl+W | удалить слово слева |
| Ctrl+L | очистить экран и перерисовать строку |

### 10.2. ANSI state machine

Поддержать как минимум:

- `ESC [ A/B/C/D`;
- `ESC [ H/F`;
- `ESC [ 1 ~`, `ESC [ 3 ~`, `ESC [ 4 ~`;
- разбиение escape sequence между разными вызовами `sh_feed()`;
- неизвестную или слишком длинную последовательность без зависания;
- возврат в normal state после ошибки;
- настраиваемый maximum CSI length.

Обычный символ после одиночного ESC не должен бесшумно теряться без явно
описанной причины.

### 10.3. Вывод редактора

- Добавление символа в конец не должно перерисовывать всю строку.
- Полная перерисовка допустима после history, completion или middle edit.
- На медленном UART объём TX не должен расти квадратично от длины строки.
- ANSI output должен отключаться для dumb terminal.
- Prompt и текущая строка не должны дублироваться после CRLF.
- При echo off command output начинается с новой строки.

## 11. История

### 11.1. Хранение

- История использует caller-provided ring buffer.
- Глубина по умолчанию: 16 записей.
- Размер и заполнение доступны через API.
- При переполнении удаляется самая старая запись.
- Пустые строки не сохраняются.
- Политика duplicate-last должна быть configurable или явно зафиксирована.
- `history clear` очищает entries, cursor и draft state.

### 11.2. Сохранение семантики

Повтор через Up + Enter обязан передать handler те же аргументы, что исходный
ввод. Команда с кавычками не должна превращаться в другую команду.

Допустимы два решения:

1. Хранить исходную строку и отдельно маскировать диапазоны секретов.
2. Сериализовать токены обратно с обязательным quoting/escaping.

Первый вариант предпочтительнее, если реализация надёжно отслеживает secret
spans. Нельзя сохранять просто соединённые пробелами токены.

### 11.3. Навигация

- Первый Up сохраняет текущий draft.
- Последующие Up идут к более старым entries.
- На самой старой записи дополнительный Up ничего не меняет.
- Down идёт к более новым entries.
- Down после самой новой записи восстанавливает draft.
- Редактирование recalled line не изменяет сохранённую запись.
- После выполнения или Ctrl+C navigation state сбрасывается.

## 12. Tab completion

### 12.1. Точный алгоритм

Completion анализирует токен под курсором и command level слева от курсора.
Текст справа от курсора должен сохраняться.

Поведение:

1. Нет совпадений: строка не меняется, кандидаты не печатаются.
2. Одно prefix match: вставить остаток имени.
3. Одно полное match: добавить пробел, если он отсутствует.
4. Несколько matches с более длинным общим prefix: вставить общий prefix.
5. Несколько matches без дальнейшего общего prefix: вывести кандидатов.
6. Повторный Tab после частичного дополнения: вывести кандидатов.
7. После вывода кандидатов: восстановить prompt, строку и cursor position.

### 12.2. Уровни

- Пустая строка + Tab показывает root commands и built-ins.
- `w<Tab>` может дополниться до `wifi `.
- `wifi <Tab>` работает только с children команды `wifi`.
- `wifi s<Tab>` дополняет общий prefix или показывает `scan`, `set`, `status`.
- После leaf-команды command-name completion прекращается.
- Optional provider может предлагать значения аргумента: интерфейсы, устройства,
  enum values или динамические объекты приложения.

### 12.3. Масштабирование

- Общий prefix вычисляется по всем matches, даже если отображается только
  ограниченное число.
- При усечении списка пользователь получает индикатор `... N more` или статус
  `SH_ERR_TRUNCATED`.
- Нельзя ошибочно считать match уникальным из-за заполнения output array.
- Проверить минимум 128 зарегистрированных команд на одном уровне в host test.

## 13. Help и built-ins

Обязательные формы:

```text
help
help wifi
wifi
wifi help
wifi --help
wifi -h
```

`help` показывает короткий список. Contextual help показывает:

- полный command path;
- summary;
- usage;
- min/max args;
- children;
- подробное описание, если оно задано.

Built-ins должны использовать тот же registry и completion, что и application
commands:

- `help`;
- `history`;
- `history status`;
- `history clear`;
- `echo on/off/status`;
- `clear`;
- optional `shell stats`;
- optional `version`.

Built-ins должны отключаться compile-time или через config без изменения core.

## 14. Секретные данные

### 14.1. Требования

- Пароль, bearer token, API key и provisioning secret не выводятся при вводе.
- В history вместо значения хранится `<hidden>` или команда получает флаг
  `NO_HISTORY`.
- Help не содержит реальные credentials.
- Error output не повторяет secret argument.
- Test output и logs не содержат исходный secret.
- После выполнения временный parse buffer с секретом можно очистить через
  configurable secure-clear policy.

### 14.2. Модель маркировки

Команда должна иметь один из вариантов:

- `first_secret_arg` и маскирование всех следующих аргументов;
- bit mask для фиксированных позиций;
- application masking callback для сложной команды;
- `SH_CMD_FLAG_NO_HISTORY`.

Interactive obscure mode включается после распознавания command path. При
неоднозначном path ввод продолжает отображаться, пока команда не определена;
такое поведение должно быть задокументировано.

Сетевая аутентификация и шифрование остаются ответственностью приложения.

## 15. Transport API

```c
typedef sh_status_t (*sh_transport_write_fn)(
    void *ctx,
    const uint8_t *data,
    size_t len,
    size_t *written);

typedef struct {
    sh_transport_write_fn write;
    sh_transport_flush_fn flush;
    void *ctx;
    uint32_t capabilities;
} sh_transport_t;
```

Контракт:

- `written` может быть меньше `len`;
- zero write с `SH_OK` запрещён или однозначно трактуется;
- `SH_ERR_WOULD_BLOCK` не приводит к бесконечному циклу;
- shell имеет bounded retry/output policy;
- disconnect возвращается как transport error;
- callback не вызывает рекурсивно тот же shell;
- input передаётся только через `sh_feed/sh_feed_byte`;
- output ordering сохраняется.

Для систем с жёсткими ограничениями допускается blocking transport, но policy
должна задаваться адаптером, а не скрываться в core.

## 16. FreeRTOS UART adapter

### 16.1. Поток данных

```text
UART ISR/DMA callback
        |
        v
RX StreamBuffer / Queue
        |
        v
Dedicated shell task
        |
        v
sh_feed()
        |
        v
Command handler

sh_write()
        |
        v
TX StreamBuffer / mutex-protected UART write
        |
        v
UART driver / DMA
```

### 16.2. Правила

- `sh_feed()` и command handlers не вызываются из ISR.
- ISR использует только `...FromISR` API и bounded copy.
- Shell task является единственным владельцем изменяемого `sh_t`.
- Размер task stack задаётся приложением и проверяется high-water mark в demo.
- RX overflow имеет счётчик и видимую диагностику.
- TX overflow/timeout возвращается в shell как transport error.
- Blocking handlers не должны блокировать RX навсегда; длительная работа должна
  отправляться в application worker task.
- Для shared UART output нужен mutex или единый TX owner.
- Асинхронные application logs должны использовать отдельную policy
  перерисовки prompt или быть отключены.

### 16.3. API адаптера

```c
typedef struct {
    StreamBufferHandle_t rx_stream;
    StreamBufferHandle_t tx_stream;
    uint32_t read_timeout_ticks;
    uint32_t write_timeout_ticks;
    size_t read_chunk_size;
} sh_freertos_config_t;

sh_status_t sh_freertos_run(sh_t *shell,
                            const sh_freertos_config_t *config);

BaseType_t sh_freertos_rx_from_isr(sh_freertos_port_t *port,
                                   const uint8_t *data,
                                   size_t len,
                                   BaseType_t *higher_priority_woken);
```

Создание task может оставаться ответственностью приложения. Helper допустим,
но core-библиотека не должна автоматически создавать task.

## 17. TCP и network UART

### 17.1. Сессии

- Каждый TCP client имеет собственные `sh_t`, line buffer, history и auth state.
- Command registry может быть общим read-only.
- Disconnect полностью освобождает или сбрасывает session state.
- Одновременная передача байтов в один `sh_t` запрещена.

### 17.2. Протокол

Нужно явно выбрать один из режимов:

- raw TCP character stream;
- Telnet с обработкой IAC negotiation;
- WebSocket/другой framed transport как отдельный adapter.

Raw TCP adapter не должен называться Telnet. Для Telnet нужно фильтровать
служебные байты и договариваться о character mode/local echo.

### 17.3. Обязательные проверки

- fragmented receive;
- несколько команд в одном packet;
- CRLF;
- partial send;
- `EWOULDBLOCK`;
- disconnect во время вывода;
- два одновременных клиента;
- медленный клиент и bounded TX queue;
- отсутствие утечки history/secret между sessions.

## 18. Bare-metal adapter

- Non-blocking `read_byte` или RX ring buffer.
- `sh_baremetal_poll()` имеет bounded work per call.
- Никаких ожиданий без timeout внутри poll.
- Command handlers либо короткие, либо запускают application state machine.
- UART ISR только кладёт байты в ring buffer.
- Overflow counter доступен приложению.
- Работа без ANSI должна быть поддержана конфигурацией.

## 19. Host terminal

### 19.1. Windows

- Перевести console input в raw/character mode.
- Не использовать обычный canonical `getchar()` как единственный путь.
- Преобразовывать console key events в внутренние key events или ANSI bytes.
- На выходе восстанавливать исходный console mode даже после Ctrl+C.

### 19.2. POSIX

- Использовать `termios` raw mode.
- Восстанавливать terminal settings через normal cleanup и signal handling.

### 19.3. Режимы запуска

```text
host_terminal_shell                 interactive mode
host_terminal_shell --script file  deterministic script mode
host_terminal_shell --no-ansi      dumb terminal mode
host_terminal_shell --limits       print configured limits
```

Demo должен содержать root, nested, sensitive, many-args и dynamic-completion
commands.

## 20. Производительность

Установить и измерить критерии на host:

- parser не использует heap в static profile;
- время lookup документируется для 16, 64, 128 и 256 команд;
- ввод строки не создаёт O(n^2) объём UART output;
- ни один цикл не зависит от неограниченного внешнего ввода;
- completion и help имеют bounded memory usage;
- stack usage основных функций измеряется или оценивается compiler tooling;
- длинный help output отправляется chunks без большого stack buffer.

Оптимизация binary search допустима только если command tables гарантированно
отсортированы и это проверяется. Для небольших embedded таблиц линейный поиск
предпочтительнее сложной инфраструктуры.

## 21. Автоматические тесты

### 21.1. Tokenizer

- zero args и whitespace-only;
- один и 32/64 аргумента;
- limit и limit + 1;
- double/single quotes;
- empty quoted arg;
- escaped quote, slash и space;
- unterminated quote;
- line length boundary;
- random byte input;
- отсутствие out-of-bounds под ASan/UBSan.

### 21.2. Registry и dispatch

- root и nested dispatch до максимальной глубины;
- правильный handler `argc/argv` контракт;
- min/max args;
- unknown/incomplete/ambiguous command;
- duplicate registration;
- malformed descriptor;
- handler return propagation;
- несколько command sets;
- built-in conflict;
- separate per-command `user_ctx`.

### 21.3. History

- capacity 0, 1 и default;
- wrap-around;
- duplicate policy;
- empty line policy;
- Up/Down boundaries;
- draft restore;
- editing recalled line;
- quoted-command semantic preservation;
- sensitive/no-history command;
- clear и повторное использование;
- отдельная история двух instances.

### 21.4. Line editor и ANSI

- insert at start/middle/end;
- Backspace/Delete boundaries;
- Left/Right/Home/End;
- Ctrl+C/U/W/L;
- split escape sequences;
- malformed/long CSI;
- CR/LF/CRLF;
- echo on/off;
- overflow recovery;
- next valid command after every error.

### 21.5. Completion

- empty root completion;
- built-ins присутствуют;
- zero/one/many matches;
- exact match adds space;
- common prefix;
- repeated Tab lists variants;
- nested level;
- cursor in middle;
- text to the right remains intact;
- no buffer space;
- more candidates than display capacity;
- 128+ commands;
- dynamic argument provider;
- hidden commands excluded.

### 21.6. Transport

Fake transport должен уметь программно задавать:

- full write;
- partial write;
- would-block;
- failure after N bytes;
- disconnect;
- bounded output buffer.

Проверить отсутствие infinite loop, сохранение порядка и корректный status.

### 21.7. FreeRTOS adapter tests

- Компиляция adapter с официальными FreeRTOS headers или test doubles.
- Chunked RX из StreamBuffer.
- ISR submission helper.
- RX overflow counter.
- TX timeout.
- task shutdown.
- stack high-water mark в example.

### 21.8. TCP integration tests

- loopback client/server;
- fragmented packets;
- CRLF;
- multiple commands per send;
- simultaneous sessions;
- partial send simulation;
- reconnect и state reset.

### 21.9. Fuzzing

Создать минимум два fuzz targets:

```text
fuzz_tokenizer
fuzz_input_state_machine
```

Invariant:

- нет crash/hang/out-of-bounds;
- cursor <= line_len;
- line_len < capacity;
- строка всегда NUL-terminated;
- history indices валидны;
- ANSI state возвращается в допустимое значение.

## 22. Quality gates

Release запрещён, если не выполнено хотя бы одно условие:

- все tests passed;
- core line coverage не ниже 90%;
- core branch coverage не ниже 85%;
- каждый исправленный дефект имеет regression test;
- GCC и Clang builds проходят с warnings as errors;
- MSVC или MinGW Windows build проходит;
- ASan и UBSan не находят ошибок;
- static analysis не содержит unresolved high-severity findings;
- host interactive checklist пройден;
- FreeRTOS adapter собран в reference project;
- размер RAM/ROM и stack опубликован;
- public headers не включают platform headers;
- в output/history fixtures нет secret values.

Coverage исключает только platform-specific недоступные ветви с явно
документированным обоснованием. Простое исключение сложного кода запрещено.

## 23. CI matrix

Минимальная матрица:

| Job | Проверка |
|---|---|
| Linux GCC | C99, `-Wall -Wextra -Wpedantic -Werror`, tests |
| Linux Clang | warnings as errors, tests |
| Linux Sanitizers | ASan + UBSan |
| Windows MinGW | build + tests + host runner |
| Windows MSVC | public headers и build compatibility |
| Coverage | line/branch report и threshold |
| Static profile | build без malloc/printf/history |
| Extended profile | line 512, args 64, history 64 |
| FreeRTOS smoke | compile adapter/reference example |
| Fuzz | bounded CI corpus run |

Из-за не-ASCII пути Windows host build должен использовать проверенный ASCII
staging script. Это должно быть частью официального workflow, а не ручным
обходным решением.

## 24. Документация

Создать:

- `README.md`: назначение, быстрый старт, build/test;
- `docs/api.md`: публичные типы и ownership;
- `docs/command_registration.md`: root/nested/many-args examples;
- `docs/freertos_uart.md`: ISR, queues, task и TX policy;
- `docs/tcp.md`: sessions, raw TCP/Telnet distinction;
- `docs/terminal_behavior.md`: клавиши, Tab и history;
- `docs/security.md`: secrets, echo и transport security boundary;
- `docs/testing.md`: test matrix, coverage и manual checklist;
- `CHANGELOG.md`;
- license и version header.

Документация должна содержать полностью компилируемые примеры, а не фрагменты
с неопределёнными функциями.

## 25. Порядок реализации

### Этап 0. Baseline

- Зафиксировать текущее поведение тестами.
- Сохранить текущий coverage report.
- Определить стабильные public names и C99 requirement.
- Завести список известных дефектов.

Gate: текущая версия воспроизводимо собирается одним скриптом.

### Этап 1. API и storage redesign

- Устранить противоречивые config/storage limits.
- Добавить status types, transport contract и output API.
- Добавить instance declaration helpers.
- Добавить command argument metadata и per-command context.

Gate: headers проходят API compile tests в C и C++ translation unit.

### Этап 2. Tokenizer и command dispatch

- Зафиксировать quoting/escaping grammar.
- Реализовать min/max validation.
- Реализовать registry validation и multiple command sets.
- Перенести built-ins в общий registry.

Gate: tokenizer/dispatch tests и sanitizer job проходят.

### Этап 3. История

- Исправить сохранение quoted commands.
- Реализовать ring edge cases и draft behavior.
- Интегрировать secret/no-history policy.

Gate: повтор команды всегда сохраняет исходный `argc/argv`.

### Этап 4. Line editor

- Выделить ANSI decoder и editor modules.
- Добавить полный набор клавиш.
- Исправить CRLF и recovery после overflow/error.
- Уменьшить объём UART redraw.

Gate: byte-stream integration tests проходят для всех edit actions.

### Этап 5. Completion

- Сделать cursor-aware token detection.
- Реализовать exact/common-prefix/list behavior.
- Добавить built-ins, nesting и dynamic provider.
- Обработать candidate overflow без ложного unique match.

Gate: completion matrix проходит, включая 128+ commands.

### Этап 6. Sensitive input

- Добавить obscure echo mode.
- Маскировать history, errors и test logs.
- Добавить secure clear policy.

Gate: автоматический тест ищет исходный secret во всём captured output.

### Этап 7. Transport reliability

- Добавить partial write и would-block policy.
- Реализовать fake transport tests.
- Добавить stats/counters для RX/TX errors.

Gate: transport fault-injection tests проходят без hang.

### Этап 8. Host ports

- Реализовать Windows raw console.
- Реализовать POSIX termios.
- Добавить script и no-ANSI modes.

Gate: interactive checklist пройден на Windows и Linux/PTY CI.

### Этап 9. FreeRTOS и bare-metal

- Реализовать StreamBuffer/Queue adapter.
- Реализовать ISR submission helper.
- Реализовать bare-metal bounded poll.
- Добавить compile tests и reference applications.

Gate: FreeRTOS example компилируется и обрабатывает scripted UART stream.

### Этап 10. TCP

- Реализовать per-client sessions.
- Добавить raw TCP loopback example.
- Отдельно определить optional Telnet mode.
- Проверить disconnect/backpressure/multiple clients.

Gate: loopback integration suite проходит стабильно.

### Этап 11. Hardening

- Fuzzing.
- ASan/UBSan.
- Static analysis.
- Performance и memory measurements.
- Проверка warnings as errors на compiler matrix.

Gate: все quality thresholds выполнены.

### Этап 12. Release и финальная проверка

- Обновить README и integration guides.
- Создать versioned package.
- Выполнить clean build из release archive.
- Запустить host terminal.
- Пройти manual acceptance checklist.
- Сохранить test/coverage/memory reports.

Gate: release можно подключить через `add_subdirectory()` или копированием
каталога `shell/` без редактирования исходников shell.

## 26. Разделение работы между агентами

### Агент A: Public API и memory model

Результат:

- новые headers;
- config/storage validation;
- public output API;
- API compile tests;
- migration note со старого API.

Не изменяет editor/completion logic, кроме необходимого подключения API.

### Агент B: Tokenizer, registry и dispatch

Результат:

- grammar implementation;
- argument count validation;
- command sets и descriptor validation;
- unified built-ins;
- unit tests.

Начинает после стабилизации headers агентом A.

### Агент C: History и sensitive data

Результат:

- semantic-preserving history;
- ring/draft logic;
- secret masking/obscure policy;
- history/security tests.

Работает поверх tokenizer contract агента B.

### Агент D: Editor и ANSI

Результат:

- editor module;
- ANSI state machine;
- CRLF handling;
- complete key set;
- byte-stream tests.

Может работать параллельно с C после фиксации API A.

### Агент E: Completion

Результат:

- cursor-aware completion;
- common prefix и candidate listing;
- nested/built-in/dynamic completion;
- scale/overflow tests.

Начинает после registry contract B и editor cursor API D.

### Агент F: Transports

Результат:

- transport contract implementation;
- fake transport;
- FreeRTOS, bare-metal и TCP adapters;
- fault-injection tests.

Не изменяет parser internals без согласования API.

### Агент G: Host terminal

Результат:

- Windows raw console;
- POSIX termios;
- script/no-ANSI modes;
- manual test executable.

### Агент H: Verification и release

Результат:

- CI matrix;
- coverage thresholds;
- sanitizer/fuzz jobs;
- static analysis;
- memory/performance report;
- documentation и release checklist.

Этот агент не закрывает дефекты самостоятельно без regression test.

## 27. Интеграционная последовательность агентов

```text
A: API/storage
   |-----------> D: editor/ANSI -----> E: completion
   |                                      |
   +-> B: tokenizer/registry --------------+
           |
           +-> C: history/security
           |
           +-> F: transports

G: host port uses A + D + E + F
H: verifies all merged results
```

Каждая ветка должна:

- иметь узкий scope;
- добавлять tests вместе с implementation;
- не менять несвязанные файлы;
- проходить clean build перед интеграцией;
- описывать изменённый public contract.

## 28. Финальный manual acceptance scenario

Пользователь запускает:

```powershell
.\scripts\build_host.ps1
.\build_host_artifacts\host_terminal_shell.exe
```

Затем проверяет:

```text
<Tab>                              list root commands and built-ins
w<Tab>                             complete wifi
wifi <Tab>                         list/complete nested commands
wifi s<Tab>                        show scan/set/status or common prefix
wifi set ssid "My Home WiFi"       quoted argument
Up, Enter                          repeat with same argc/argv
Down                               restore draft
tag config set interval 1000       deep nested dispatch
args a01 ... a31                   large argument list
wifi set pass secret               obscured typing and masked history
history status                     capacity and filled count
history                            no plaintext secret
echo off                           disable character echo
echo on                            restore echo
Home/End/Delete/Backspace          edit line
Ctrl+U/Ctrl+W/Ctrl+C/Ctrl+L        control keys
unknown                            stable diagnostic and recovery
```

Дополнительно script mode должен выполнить golden input/output fixture без
ручного терминала.

## 29. Definition of Done

Проект считается завершённым только когда одновременно выполнено всё ниже:

- core не зависит от ОС и hardware drivers;
- static-only profile не использует heap;
- API команд стабилен и документирован;
- root/nested commands удобно добавляются из независимых modules;
- 32 аргумента работают по умолчанию, 64 проходят extended test;
- история корректно повторяет quoted commands;
- Up/Down сохраняют и восстанавливают draft;
- Tab полностью соответствует разделу 12;
- CRLF не создаёт второе выполнение или prompt;
- secrets не появляются в echo, history, errors и tests;
- host terminal реально принимает стрелки и Tab;
- FreeRTOS UART adapter имеет рабочий queue/task path;
- TCP имеет независимые client sessions;
- transport errors и partial writes протестированы;
- coverage не ниже 90% lines и 85% branches;
- sanitizer, fuzz, static analysis и compiler matrix проходят;
- manual acceptance выполнен;
- release archive собирается с чистого состояния;
- все примеры компилируются;
- документация позволяет подключить модуль без чтения его внутренних файлов.

Только после этого реализацию можно называть production-ready portable shell с
Zephyr-подобным UX.
