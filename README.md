# COMProxy – TCP tunnel over serial port

COMProxy allows you to forward TCP traffic (HTTP/HTTPS) through a serial link (e.g. null‑modem cable, radio modem, USB‑to‑serial adapter).  
It consists of two independent console applications:

- **comproxy‑client** – listens for incoming TCP connections, acts as an HTTP/HTTPS proxy, encodes requests and forwards them over the serial port.
- **comproxy‑server** – connects to the serial port, receives frames, establishes outgoing TCP connections to the requested destinations, and relays data bidirectionally.

Both ends use a simple binary framing protocol.

Suitable for those who don't want or can't connect their computer directly to the network. I tested simple web surfing on lightweight sites and the ability to access popular neural network sites (Deepseek, Qwen, etc.).

---

## Features

- HTTP proxy with `CONNECT` method support (HTTPS, WebSocket, etc.)
- Plain HTTP request rewriting (removes hop‑by‑hop headers)
- CRC‑16 (CRC‑16/ARC) integrity check
- Multiple concurrent connections are multiplexed over the same serial link

---

## Requirements

- Qt 5 (core, network, serialport modules)
- C++17 compiler (gcc, clang, MSVC)
- qmake (or you can adapt the `.pro` files for CMake)

---

## Building

Clone the repository and compile each application separately.

```bash
# Build server
cd server
qmake server.pro
make

# Build client
cd ../client
qmake client.pro
make
```

The common code (`serial_link.cpp`, `serial_link.h`) is placed in a `../common` directory relative to the project files. Make sure the directory structure is preserved.

---

## Usage

### Server side (remote end that reaches the internet)

```bash
comproxy‑server <serial‑port> <baud‑rate>
```

Example:

```bash
comproxy‑server /dev/ttyUSB0 115200
```

The server listens on the serial port, waits for connection requests, and initiates outgoing TCP connections. No local TCP listener is created.

### Client side (local machine where the proxy is used)

```bash
comproxy‑client <serial‑port> <baud‑rate> <listen‑addr> <listen‑port>
```

Example:

```bash
comproxy‑client COM3 115200 127.0.0.1 8080
```

After starting, the client accepts HTTP proxy connections on `127.0.0.1:8080`. Configure your browser or application to use this proxy address.

---

## Protocol description

Each frame has the following structure (big‑endian):

| Field          | Size (bytes) | Description                          |
|----------------|--------------|--------------------------------------|
| Magic          | 4            | `0xC0DECAFE`                         |
| Connection ID  | 4            | unique ID for the multiplexed stream |
| Type           | 1            | frame type (see below)               |
| Payload length | 4            | length of the payload (0 … 65536)    |
| Payload        | variable     | actual data                          |
| CRC‑16         | 2            | CRC‑16 over header + payload         |

**Frame types:**

- `FR_CONNECT` (1) – client → server: payload contains `"host:port"` (UTF‑8)
- `FR_CONNECTED` (2) – server → client: remote connection established
- `FR_CONN_FAIL` (3) – server → client: connection failed, payload = error message
- `FR_DATA` (4) – bidirectional: payload is raw TCP data
- `FR_CLOSE` (5) – bidirectional: close the connection with given ID

The serial link uses no hardware flow control (RTS/CTS). The application implements a simple high‑water mark (256 KiB) to pause reading from TCP sockets when the serial port’s write buffer becomes full; reading resumes when the buffer drops to 64 KiB.

---

## Limitations

- Only HTTP proxy mode (no SOCKS support).
- No encryption – the serial link transmits data in plain text. Use an encrypted serial link (e.g. encrypted radio modem) or add a VPN/TLS tunnel if security is required.
- Maximum frame payload is 64 KiB. Larger writes are split automatically.

---

# COMProxy – TCP-туннель через последовательный порт

COMProxy позволяет передавать TCP-трафик (HTTP/HTTPS) через последовательный порт (например, нуль-модемный кабель, радиомодем, адаптер USB‑RS232).  
Проект состоит из двух независимых консольных приложений:

- **comproxy‑client** – слушает входящие TCP-подключения, работает как HTTP/HTTPS-прокси, кодирует запросы и пересылает их через последовательный порт.
- **comproxy‑server** – подключается к последовательному порту, принимает кадры, устанавливает исходящие TCP-соединения к запрошенным узлам и ретранслирует данные в обе стороны.

Обе стороны используют простой двоичный протокол.

Подходит для тех, кто не хочет или не имеет возможности подключить компьютер напрямую к сети. Протестирован простой веб-серфинг по легковесным сайтам, возможность заходить на популярные сайты нейросетей (Deepseek, Qwen и т.д).

---

## Возможности

- HTTP-прокси с поддержкой метода `CONNECT` (HTTPS, WebSocket и т.д.)
- Перезапись обычных HTTP-запросов (удаляются заголовки hop‑by‑hop)
- Контроль целостности CRC‑16 (CRC‑16/ARC)
- Мультиплексирование нескольких одновременных соединений через один последовательный порт

---

## Требования

- Qt 5 (модули core, network, serialport)
- Компилятор с поддержкой C++17 (gcc, clang, MSVC)
- qmake (файлы `.pro` можно адаптировать для CMake)

---

## Сборка

Склонируйте репозиторий и соберите каждое приложение отдельно.

```bash
# Сборка сервера
cd server
qmake server.pro
make

# Сборка клиента
cd ../client
qmake client.pro
make
```

Общий код (`serial_link.cpp`, `serial_link.h`) расположен в каталоге `../common` относительно файлов проектов. Убедитесь, что структура каталогов сохранена.

---

## Использование

### Сервер (удалённая сторона, имеющая выход в интернет)

```bash
comproxy‑server <последовательный‑порт> <скорость>
```

Пример:

```bash
comproxy‑server /dev/ttyUSB0 115200
```

Сервер слушает последовательный порт, ожидает запросы на соединение и инициирует исходящие TCP-соединения. Локальный TCP-слушатель не создаётся.

### Клиент (локальная машина, где используется прокси)

```bash
comproxy‑client <последовательный‑порт> <скорость> <слушать‑адрес> <порт>
```

Пример:

```bash
comproxy‑client COM3 115200 127.0.0.1 8080
```

После запуска клиент принимает HTTP-прокси-соединения на `127.0.0.1:8080`. Настройте браузер или приложение на использование этого прокси-адреса.

---

## Описание протокола

Каждый кадр имеет следующую структуру (порядок байт big‑endian):

| Поле             | Размер (байт) | Описание                                 |
|------------------|---------------|------------------------------------------|
| Magic            | 4             | `0xC0DECAFE`                             |
| Идентификатор    | 4             | уникальный ID мультиплексированного потока |
| Тип              | 1             | тип кадра (см. ниже)                     |
| Длина полез. данных | 4          | длина полезной нагрузки (0 … 65536)      |
| Полезные данные  | переменная    | собственно данные                         |
| CRC‑16           | 2             | CRC‑16 от заголовка + полезных данных    |

**Типы кадров:**

- `FR_CONNECT` (1) – клиент → сервер: полезные данные содержат `"host:port"` (UTF‑8)
- `FR_CONNECTED` (2) – сервер → клиент: удалённое соединение установлено
- `FR_CONN_FAIL` (3) – сервер → клиент: ошибка соединения, полезные данные = текст ошибки
- `FR_DATA` (4) – в обе стороны: полезные данные – это сырой TCP-трафик
- `FR_CLOSE` (5) – в обе стороны: закрыть соединение с указанным ID

Последовательный порт работает без аппаратного управления потоком (RTS/CTS). Приложение реализует простое управление на основе high‑water mark (256 КиБ): чтение из TCP-сокетов приостанавливается, когда буфер записи последовательного порта переполняется; чтение возобновляется, когда буфер опускается до 64 КиБ.

---

## Ограничения

- Только HTTP-прокси (нет поддержки SOCKS).
- Нет шифрования – данные через последовательный порт передаются в открытом виде. При необходимости защиты используйте шифрованный канал (например, зашифрованный радиомодем) или дополнительный VPN/TLS-туннель.
- Максимальный размер полезной нагрузки кадра – 64 КиБ. Бо́льшие объёмы данных автоматически разбиваются.