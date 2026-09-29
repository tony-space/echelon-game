# Каталог файлов

Обе игры устроены одинаково на верхнем уровне: папка `Data/` с ресурсами и
набор DLL движка рядом с `Game.exe`. Вторая игра добавляет меню на Python 2.2
и другой набор миссий.

`P1 K6x/` в первой игре — вторая копия тех же DLL и `Game.exe`, не новые
данные. `Python/` во второй — стандартная библиотека Python 2.2 (`python22.dll`),
не скрипты игры.

## Первая игра, `Data/`

| Файл | Магик | Роль | Уверенность |
| --- | --- | --- | --- |
| `Graphics/mesh.dat` | `DATA` | Меши | разобрано, см. [containers.md](containers.md) |
| `Graphics/textures.dat` | `TEXS` | DXT-текстуры | разобрано |
| `Graphics/materials.dat` | `DMAT` | D3DMATERIAL7 | разобрано |
| `Graphics/ctrlobj.dat`, `ctrltex.dat` | `DATA`, `TEXS` | Геометрия и текстуры интерфейса | каталог |
| `Graphics/particles.dat` | `PARS` | Определения частиц | опознано |
| `Graphics/dxm.dat` | `DXMC` | Профили рендера (туман, смешение, вода) | опознано |
| `Graphics/rdata.dat` | `DATA` | Погода, небо, вода, сцена | опознано |
| `objects.dat` | `MEOS` | Узлы частей и точки подвески | разобрано частично |
| `objects2.dat` | `DATA` | Меши статики мира (здания, мосты) | опознано |
| `boxes.dat` | `DATA` | Коллизия частей: сфера и плоскости, не матрицы | каталог, см. [containers.md](containers.md) |
| `gdata.dat` | `DATA` | База юнитов, текст внутри контейнера | разобрано частично |
| `Scenes/*` | см. [terrain.md](terrain.md) | Ландшафт | опознано |
| `*.sfx`, `Voice*.sfx` | `DSFX` | Звуковые банки | опознано |
| `sounds.dat` | `GSND` | Индекс звуков | каталог |
| `Menu.wav` | `RIFF`/`WAVE` | Музыка меню, PCM | разобрано |
| `Movies/*.wmv` | ASF | Ролики | каталог |
| `pictures.dat` | `PICS` | Картинки кнопок меню | опознано |
| `*.cmp`, `Configs.cfg`, `*Data.dat`, `Flares.dat`, `common` нет | `EVG1` | Миссии, настройки, вспышки | опознано |
| `common.dlc` | текст | Макросы действий (`ZOOM30`, радар, ПНВ) | разобрано |
| `user.dlc` | текст | Пустая заготовка под свои настройки | разобрано |
| `setup.dat` | текст | Список DLL, которые грузит лаунчер | разобрано |
| `WinRes.dat` | `MZ` | PE-ресурсы, не контейнер данных | каталог |

Миссии первой игры — файлы `*.cmp` с магиком `EVG1` (`Battle of Rockade IV.cmp`,
`Deathmatch Arena.cmp`, `Instant Action.cmp`).

## Вторая игра, отличия

Те же контейнеры графики, плюс:

| Файл | Магик | Роль |
| --- | --- | --- |
| `Graphics/atextures.dat` | `TEXS` | Дополнительный атлас текстур (714 имён: земля, небо, повреждения `va_*dmg`) |
| `Graphics/dxm.dat` | `DXMC2` | Тот же смысл, другая версия магика (`DXMC` → `DXMC2`) |
| `*.gsl` + `*.gsd` | `EVG1` | Миссия: логика (маленький `.gsl`) и данные (большой `.gsd`) |
| `Scenes/*.ros` | `EVG1` | Растительность и россыпь объектов на карте |
| `Scenes/*.bmp`, `*.bmq` | `BM` | Обзорные карты. `.bmq` тоже начинается с `BM`, это не отдельный магик |
| `Menu.dat` | `EVG1` | Раскладка экранов меню («EngBay», списки техники) |
| `AICommands.dsc` | `EVG1` | Каталог команд миссии: `AddBot`, `Patrol`, `OnDeath`, … |
| `Movies/*.avi` | `RIFF`/`AVI` | Ролики вместо WMV |
| `Modules/**/*.py` | исходники | Меню и кампания на Python 2.2. Рядом лежат `.pyc` и тесты `*_Test.py` |
| `StdLogic.ai` | рядом с DLL | Нативный модуль ИИ. Копия `Standart.ai` из первой игры в `P1 K6x/` — обычный PE (`MZ`) |

Кампании второй игры, по именам файлов: `Wind Warriors`, `Training Course`,
`Instant Action`, `shizograd`, `tahiti`, `Net Islands`, `Rocky Island`,
`Deathmatch Arena`, плюс наборы `teampack`, `mp_set_v2`, `dq_map_pack`.
Файлы с префиксом `rs_` и `yar_` — варианты тех же карт.

## Что не разбиралось

`BanList.cfg`, `DedServer.cfg`, `Dedicated.epp`, `GameSpySupport.dll`,
`MadSockets.dll` / `MadSockets2.dll`, `dedicated.exe`, экраны `ServersList` /
`ServerOpt` в `Modules/`. Это сеть и лобби.

`Modules/` в основном **тесты и исходники меню**, а не геймплейный скрипт
миссии. Геймплей миссии сидит в `.gsl`/`.gsd` и в нативном `.ai`.
