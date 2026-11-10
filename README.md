<div align="center">

# LCEReborn Live

**Minecraft: Wii U Edition, rebuilt for PC and connected with friends.**

[English](#en) · [Русский](#ru) · [Downloads](https://github.com/LCEReborn-Live/LCEReborn/releases/latest)

</div>

<a id="en"></a>

## English

**LCEReborn Live** is the PC version of LCEReborn with Live accounts and online features. The game is based on the reconstructed Minecraft: Wii U Edition source, corresponding to the Aquatic Update 1.13.

### Features

- Browser-based Live sign-in with a persistent launcher session.
- Friends, launcher and in-game presence, invitations, and a list of joinable worlds.
- Direct connections with NAT traversal when possible, with relay fallback.
- Live account skins and capes in game.
- The original LCE leaderboard menu; global score syncing is currently being integrated.
- Keyboard, mouse, and controller support; single-player and split-screen play.

### Installation

1. Download the installer or portable archive from the [releases page](https://github.com/LCEReborn-Live/LCEReborn/releases/latest).
2. Start the launcher and import **your own dumps** of the base game and Update 688.
3. Sign in to Live from the launcher, choose a profile, and select **Play**.

Game data, saves, and update files are not included in downloads. The launcher imports the files from your own copy of the game.

### Live networking

Sign in to Live in the launcher, add friends by username, and open a world with online play enabled. Friends' available worlds appear in game automatically. The client uses a direct connection when NAT traversal succeeds and falls back to the relay otherwise.

### Build from source

Client build instructions are in [`docs/pc-platform.md`](docs/pc-platform.md). Release build details are in [`docs/live-release.md`](docs/live-release.md). The Live API source is in [`live-api/`](live-api/).

### Credits and license

LCEReborn is based on reconstructed Minecraft: Wii U Edition source and other open-source components. This project is not affiliated with or endorsed by Mojang Studios, Microsoft, 4J Studios, or Nintendo. Minecraft is a trademark of Mojang Studios and Microsoft; Wii U is a trademark of Nintendo.

LCEReborn code is licensed under the [GNU GPL v3 or later](LICENSE). Third-party components retain their respective licenses. Game files and assets are not included in this repository or installer.

<a id="ru"></a>

## Русский

**LCEReborn Live** — PC-версия проекта LCEReborn с учётными записями и сетевыми функциями Live. Основа игры — восстановленный исходный код Minecraft: Wii U Edition на версии Aquatic Update 1.13.

### Возможности

- Вход в Live через браузер с сохранением сессии в лаунчере.
- Друзья, статусы «в лаунчере» и «в игре», приглашения и список доступных миров.
- Подключение напрямую с обходом NAT, когда это возможно, и через relay в остальных случаях.
- Скины и плащи Live-аккаунта в игре.
- Оригинальное меню таблиц рекордов LCE; глобальная синхронизация результатов сейчас интегрируется.
- Клавиатура, мышь и геймпады; одиночная игра и разделённый экран.

### Установка

1. Скачайте установщик или portable-архив со [страницы релизов](https://github.com/LCEReborn-Live/LCEReborn/releases/latest).
2. Запустите лаунчер и импортируйте **собственные дампы** базовой игры и Update 688.
3. Войдите в Live через кнопку в лаунчере, выберите профиль и нажмите **Play**.

Игровые данные, сохранения и файлы обновления не входят в загрузки. Лаунчер импортирует файлы вашей копии игры с компьютера.

### Сеть Live

Войдите в Live в лаунчере, добавьте друзей по имени пользователя и откройте мир с включённой сетевой игрой. Доступные миры друзей появляются в игре автоматически. Прямое соединение используется, когда удаётся пройти NAT; иначе подключение идёт через relay.

### Сборка из исходников

Инструкции по сборке клиента находятся в [`docs/pc-platform.md`](docs/pc-platform.md), а сведения о сборке релизов — в [`docs/live-release.md`](docs/live-release.md). Исходный код API Live находится в [`live-api/`](live-api/).

### Благодарности и лицензия

LCEReborn основан на восстановленном коде Minecraft: Wii U Edition и других открытых компонентах. Проект не связан с Mojang Studios, Microsoft, 4J Studios или Nintendo и не одобрен ими. Minecraft — товарный знак Mojang Studios и Microsoft; Wii U — товарный знак Nintendo.

Код LCEReborn распространяется по лицензии [GNU GPL v3 или новее](LICENSE). Лицензии сторонних компонентов остаются за их авторами. Игровые файлы и ресурсы в репозитории и установщике отсутствуют.
