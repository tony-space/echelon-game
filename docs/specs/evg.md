# `EVG1` — миссии, россыпь, меню, настройки

Контейнер переменных `UniVars.dll` (`CreateUnifiedVariableDB`). Им пользуются
и первая, и вторая игра. Сжатия в начале файла нет. Раскладка ниже **разобрана**
на всех 24 файлах `EVG1` второй игры (`Data/` и `Data/Scenes/`): парсер
`tools/evg_dump.py` проходит их без ошибок. Установки первой игры на диске нет,
её файлы заново не читались.

Читает `legacy/Echelon Wind Warriors/UniVars.dll` (образ `0x10000000`,
29 октября 2002). Экспорты: `Version` (`0x59C0`),
`_CreateUnifiedVariableDB@12` (`0x59D0`), `_GetClassIDByExt@4` (`0x5ED0`),
`_GetExtByClassID@4` (`0x5C70`). `CreateUnifiedVariableDB` принимает ровно
ключ базы `0x9A1FD1C7` (это не тип записи), путь и байт-флаг. Флаг 0 копирует
блоки в кучу (`10001FA0`), флаг 1 оставляет файл отображённым (`10001900`) —
так `Terrain.dll` открывает `.ros`, см. [terrain_dll.md](terrain_dll.md).

## Файл — разобрано

```
0x00  char magic[4] = "EVG1"
0x04  u32  indexBytes          // кратно 8
0x08  {u32 offset, u32 size} × (indexBytes / 8)
      // offset и size — от начала секции данных
8+indexBytes:
      char magic[4] = "MIKH"
      u32  dataBytes            // = длина секции − 8
      ... плотно упакованные блоки ...
```

Магики лежат в DLL по VA `0x100080D0` (`EVG1`) и `0x100080C8` (`MIKH`).
Пример `Scenes/NetArena.ros`: файл 5335 байт, `indexBytes = 1200` (150
записей), секция данных 4127 байт, `dataBytes = 0x1017 = 4119`.

Старая догадка «семь dword'ов заголовка, `count` на смещении 24» — это уже
каталог: первые записи индекса нулевые, следующая имеет `offset = 8`,
`size = 8`.

Значение блока — не байты по `offset`. Загрузчик отдаёт окно
`data_base + offset + 8` длиной `size` (**проверено**: `lea eax,[ecx+edx+8]`
в `10001766` и то же `+8` в копирующем обходе `100017B4`). Эти 8 байт заходят
в следующий блок; у последнего блока они лежат сразу за `dataBytes`, но ещё
внутри файла. Первые 8 байт по `offset` — хвост предыдущего значения.

- Блок 0 — пустой слот каталога (`offset = 0`, `size = 0`). Ссылка на него
  значит «пусто».
- Блок 1 — сам заголовок `MIKH` (`offset = 0`, `size = 8`). Его значение, 8
  байт сразу после заголовка, — корень: `{u32 classId, u32 blockIndex}`.

Дальше блоки идут вплотную: `offset` следующего равен `offset + size`
предыдущего.

## Типы значений — разобрано

`GetExtByClassID` (`10005C70`) сопоставляет id и расширение. Строки —
`0x100083B0` и дальше, по 8 байт на имя (`.unknown` для чужого id).

| Расширение | class id | Полезная нагрузка |
| --- | --- | --- |
| `.int` | `0x189596B9` | `i32` |
| `.flt` | `0x3FCFC71D` | `f32` |
| `.v3f` | `0x2AF9C65D` | три `f32`, X Y Z |
| `.txt` | `0xAF293CAF` | ровно `size` байт cp1251; загрузчик дописывает NUL |
| `.bin` | `0x75710EAA` | сырые `size` байт |
| `.ref` | `0x9E045FA0` | та же рамка, что у `.txt`: путь, например `\Root\Locations\Instant Action` |
| `.arr` | `0x5766773F` | см. ниже |
| `.ctr` | `0xF705FAA8` | см. ниже |

Дырка массива — class id `0xFFFFFFFF`. Это не отдельное расширение.

`.arr` (конструктор `10003080`): `u32 count`, затем `count` пар
`{u32 classId, u32 blockIndex}`.

`.ctr` (конструктор `10004850`): `u32 count`, затем `count` записей по 12 байт
`{u32 classId, u32 blockIndex, u32 nameOffset}`. Имя — C-строка cp1251 по
смещению `nameOffset` от начала этой же нагрузки (конструктор ищет NUL и
копирует строку). В памяти запись разворачивается до `0x14` байт. Имена полей
на диске — текст, не хеш.

Фабрика блоков — `10005AB0`: по class id вызывает конструктор `.ctr`, `.arr`
или скаляр.

## Хеши имён — разобрано

Поля `Name`, `Type`, `Layout1`…`Layout4`, когда они `.int`, хранят не номер,
а CRC32 имени в cp1251 **без финального дополнения**:

```
(zlib.crc32(bytes) & 0xFFFFFFFF) ^ 0xFFFFFFFF
```

Тот же хеш у имён записей контейнера `DATA` ([containers.md](containers.md)).
Словарь строится из кавычек и идентификаторов `Data/gdata.dat` и из
идентификаторов в `*.dll` / `*.ai` рядом с `Game.exe`. Неизвестное значение
печатается как `0x........`. `0` и `-1` — пустой слот, не хеш: парсер оставляет
их числами.

Проверка: все 23118 полей `Name` в `Wind Warriors.gsd` находятся в `gdata.dat`
(`Human_BF1` = `0x804A43AF`, `Ore Complex Part 1`, `Highway`, …). `Type`
события — `Mission` или `Selection`; 22 значения `0x63676558` в кампании
словарём не закрыты.

`Flags`, `Side`, `Voice` — обычные целые, не хеши. Биты `Flags` не разбирались.

## Россыпь `.ros` — разобрано

Корень — `.arr` на 64 слота (пустые — `0xFFFFFFFF`). Живые слоты — группы,
каждая `.ctr`:

| Поле | Тип | Смысл |
| --- | --- | --- |
| `Name` | txt | подпись группы, часто по-русски |
| `Red`, `Green`, `Blue` | int 0…255 | ключ цвета, не окраска меша (**гипотеза**: маска на карте, отдельной карты в файле нет) |
| `Items` | arr или пусто | прототипы россыпи |

Элемент `Items`:

| Поле | Тип | Пример на Continent |
| --- | --- | --- |
| `ObjectName` | txt | `treesf1`, `treesa`, `bf1ruins`, `stone1` — имя меша, не хеш |
| `Density` | flt | `1` у леса, `0` у «случайных» |
| `DensityDisp` | flt | `0`, `0.001`, `0.05` |
| `Dispersion` | flt | почти везде `0.5` |
| `Distance` | flt | `1500`, `4000`, `5000` — по величине метры (**гипотеза**, потребитель только читает ключ) |
| `TranspDist` | flt | `500` или `0` |
| `Normalize` | int | `4`, `5` или `6` |

Формулу плотности этот разбор не восстанавливает. Суффикс `_Im0` в файле не
хранится: его дописывает `Terrain.dll`, когда берёт меш
([terrain_dll.md](terrain_dll.md)).

`continent.ros` — 666 блоков, 19 живых групп из 64:

| `Name` | RGB | предметов |
| --- | --- | --- |
| None | 0,0,0 | 0 |
| ForestG200 (лиственный) | 0,200,0 | 1 (`treesf1`) |
| ForestG250 (лес) | 0,250,0 | 1 (`treesf2`) |
| ForestG250 (лес Dark) | 0,200,100 | 1 |
| Water | 0,0,50 | 0 |
| ForestG250 (лес Light) | 0,220,120 | 1 |
| Random Object (угол наклона 0) | 161,141,119 | 7 |
| Random Object (угол наклона 5) | 130,110,88 | 7 |
| Random Object (угол наклона 10) | 98,78,56 | 4 |
| Snow | 255,255,255 | 3 (`stone1`, …) |
| ForestG250 (лес Saturation) | 0,200,200 | 1 |
| ForestG200 (лиственный Dark / Light / Saturation) | 100,200,0 / 150,220,0 / 200,200,0 | по 1 |
| Random Trees | 0,110,0 | 17 (`treesa`, …) |
| Trees around Road | 0,150,0 | 0 |
| Trees LayerA / LayerB | 50,150,50 / 50,100,50 | по 10 |
| Random Object (угол наклона <70) | 100,100,100 | 3 |

Порог наклона записан только в подписи и в цвете: у трёх групп «угол наклона»
первый предмет один и тот же (`bf1ruins`, `Normalize = 5`). `NetArena.ros`,
`netislands.ros`, `rockyisland.ros` — по 2 живые группы, `Arctic.ros` — 6.

## Миссии `.gsl` / `.gsd` — разобрано

Пара файлов. `.gsl` — короткий текст: `Description`, `Title`, `Voice Files`
(массив `voice00.sfx`…), `Events` (те же id, что в `.gsd`), `Game Messages`.
Координат там нет. Размещение — в `.gsd`.

Корень `.gsd`:

- `StartEvent` (txt) — только у кампании, `Wind Warriors.gsd` начинает с `O1-0`;
- `Locations` — карты;
- `Events` — миссии (`IA-1`, `O1-0`, `Q5-3`, …).

Локация (`Instant Action`, `Continent`, `Arctic`, `Races`):

| Поле | Что это |
| --- | --- |
| `GameMapName`, `TerrainName` | `NetArena`, `Scenes\Continent`, `Scenes\Arctic`, `Scenes\PPK_race` |
| `MeMapName` | картинка, `Scenes\NetArena.bmp` |
| `GameMapSizeX`, `GameMapSizeZ` | мелкие целые: Continent 11×12, NetArena 3×3, Arctic 6×8, Races 13×13. **Не** размер поля высот |
| `AiDlls` | `StdLogic.ai` |
| `Groups` | статика карты; у Instant Action пусто |
| `Roads` | ломаные дорог, см. ниже |
| `RoadNet` | `.bin`, внутри контейнер `DATA` |
| `Markers` | на локации пусто; маркеры лежат в событии |

`Wind Warriors.gsd` / Continent: 326 групп и 717 дорог. Arctic: 95 групп, 17
дорог. Races: группы и дороги пустые.

Событие (пример `IA-1`): `Ai` (`StdInstantAction`), `Type` (хеш `Mission`),
`Location` (`.ref` на `\Root\Locations\Instant Action`), `Groups`, `Markers`,
`VisConfigName` (`dawn3`), дата (`DateYear` 2354, …), `AiScript` (текст команд),
`NextEventScript`, сырые `.bin` `EnabledCrafts` и `EnabledWeapons` по 72 байта
(**не разобраны**).

### Где стоят объекты

Группа — `.ctr` с полями `Flags`, `Ai`, `Side`, `AiScript`, `Units`, `Voice`,
`Points` (часто пусто). `Ai` группы: `StdStaticGroup` у зданий карты,
`StdCraftGroup` у звена самолётов.

Юнит в `Units`:

| Поле | Тип | Пример |
| --- | --- | --- |
| `Name` | хеш | `Velian_BF1`, `Ore Complex Part 1`, `Multiplayer Start` — имя из [gdata.md](gdata.md) |
| `Layout1`…`Layout4` | хеш или −1 | `V_PPC`, `Radar 100000m`; −1 — слота нет |
| `Ai` | txt | `StdCraft`, `StdStatic`, `StdStaticHangar`, `StdTank`, `StdCarrier`, `StdHTGR`, `StdStaticSfg`, `StdSingleCargo` |
| `Org` | v3f | положение, см. оси ниже |
| `Angle` | flt | градусы, в ±360 не заворачиваются (есть −869 и 425) |
| `Flags` | int | как есть, биты не читались |
| `AiScript` | txt или пусто | скрипт звена, не координата |

Камера в тексте `AiScript` — другие поля: `Vector=(x,y,z)`, `Heading`, `Pitch`.
Это не `Angle` юнита. У `IA-1` камера `Vector=(27946.05, 379.74, 26965.98)`,
`Heading=-83.55`, `Pitch=-4.75`.

Примеры второй игры:

- Звено `Group2` (`StdCraftGroup`, `Side = 2`), юнит `Velian_BF1`, `Ai = StdCraft`,
  `Org (20224, 100, 20480)`, `Angle = 0`, `Layout1 = Layout2 = V_PPC`.
- Ангар `Base Bravo`: `Multiplayer Start`, `Ai = StdStaticHangar`,
  `Org (27937.19, 8, 26979.58)`, `Angle = 260`.
- Статика Continent, группа `New Ore Complex` (`StdStaticGroup`): `Ore Complex
  Part 1`, `Ai = StdStatic`, `Org (246416, 12.3, 253401.2)`, `Angle = 180`,
  все `Layout*` = −1.
- Маркер события `IA-1` / `RunAway`: `v3f (27968, 10000, 27040)`.

Статика всей карты Continent — `Locations/Continent/Groups` (326 групп), а не
только события кампании. Юниты мгновенного боя на NetArena сидят в
`Events/IA-*/Groups`: у локации `Groups` пустой.

### Оси — разобрано по числам, знак курса сверен на стенах УТЦ

`Org`, точки дорог и маркеры — метры D3D: X вправо, Y вверх, Z вперёд.
У статика и танка `Y` — поправка к земле в метрах (часто 0, бывает −4…+6),
не абсолютная высота: стены `O1-1` с `Y = 0` стоят на лётном поле, а не
на уровне моря. У самолёта `Y` абсолютный (`100`, `210`). Маркер `RunAway`
бывает на `Y = 10000`. У кусков `New Avalon *` поправка почти гасит высоту
земли — см. [statics.md](statics.md).

Отсчёт поля высот `(i, j)` в оригинале стоит на D3D `(64·i, y, 64·j)`.
Continent — 5376×6016 отсчётов. Тот же рудный комплекс:

- `i = 246416 / 64 = 3850.25`
- `j = 253401.2 / 64 ≈ 3959.39`

обе координаты внутри карты. В систему ремейка точка переводится как
`(x, y, −z)`, см. [containers.md](containers.md).

`Angle` — градусы вокруг вертикали. **Проверено** на периметре УТЦ в
`O1-1`: `heading_deg = Angle` (минус уже внутри поворота сцены вокруг Y)
кладёт секции стены вдоль ряда, в котором они расставлены. Отражение Z
отдельным минусом к `Angle` не добавляется.

### Дороги — только то, что видно снаружи

`Roads[]` = `{Name, Points}`. `Name` — тот же CRC и резолвится в имя дороги
из `gdata` (`Highway`, `Dirty road`, `Narrow Tarmac`, `Snow Highway`).
`Points` — массив `v3f` в тех же метрах; Y часто 0. Первая точка шоссе
NetArena: `(27466, 0, 21942)`. `RoadNet` локации — вложенный `DATA`
(`Instant Action` 7200 байт, Continent около 1.7 МБ). Внутреннюю раскладку
`DATA` этот файл не описывает.

## Что каким файлом кодируется

Все строки таблицы ниже — файлы второй игры, и все они теперь читаются
парсером. Пометки первой игры оставлены по старому каталогу: установка
`legacy/Echelon` недоступна.

| Расширение / имя | Игра | Содержимое |
| --- | --- | --- |
| `*.cmp` | 1 | Миссия целиком (логика и данные в одном файле). Заново не читался |
| `*.gsl` | 2 | Текст миссии: описание, `Voice Files`, id событий |
| `*.gsd` | 2 | Данные той же миссии: локации, группы, юниты, дороги. Пары `Instant Action`, `Wind Warriors`, `Training Course`, `Deathmatch Arena`, `Net Islands`, `Rocky Island` |
| `*.ros` | 2 | Россыпь на карте, раздел выше |
| `Configs.cfg` | 1 | Видео, звук, управление. См. [input.md](input.md). Заново не читался |
| `VisualConfigs.dat` | обе | Погода: `day`, `dusk`, `storm_rain_thunder`, … Во второй игре корень — `.ctr` этих имён |
| `GameData.dat` | обе | Во второй игре: `WeaponsList`, `Menu`, `DefaultOptions`, `MenuPresets`, `AwardsList`, `CraftsList`, `ActionsList`, `DefaultControls` |
| `AiData.dat` | обе | `Skills`, `GroupProperties`, `Teams`, `Events`, `Messages`, `Ranks`, `Scores`. См. ниже |
| `AICommands.dsc` | 2 | `Logics` и `Factories`: схема команд, не раскладка их байт в скрипте |
| `Flares.dat` | обе | Вспышки по имени: `ExplFlareSmall`, `Brightness`, `TexCoords`, `FadeOffStart` |
| `Menu.dat` | 2 | Экраны: `MainScreen`, `EngBayScreen`, … |
| `Dedicated.epp` | 2 | Корень `Options`. Сеть, поля не разбирались |

`common.dlc` и `user.dlc` — **не** `EVG1`, а обычный текст. `user.dlc` содержит
только комментарий-заготовку.

Имена вроде `dawn#scene` встречаются внутри текстовых полей. Это склейка
через `#`, как у записей `rdata.dat`, а не отдельный тип контейнера.

## Команды миссии — каталог по именам

`AICommands.dsc` перечисляет команды и фабрики. Команды одиночной игры:

- Юниты: `AddBot`, `BotCount`, `SetSkill`, `SetSpec`, `Appear`, `Disappear`,
  `SetPlayable`, `ChangeSide`, `ShowCallsign`
- Движение: `Patrol`, `RouteTo`, `Escort`, `SetFormation`, `SetSpeed`,
  `Takeoff`, `AvoidTerrain`, `UseRoads`, `ReachRadius`
- Бой: `SetFireMode`, `AttackRadius`, `AttackCourse`, `Defend`, `Capture`,
  `Repair`, `Scramble`
- Урон и цели: `AddDamage`, `OnDamage`, `OnDeath`, `OnGroupDeath`, `AddObjective`,
  `SetObjective`, `DeleteObjective`, `Success`
- События: `OnTimeExceed`, `OnReach`, `OnContact`, `OnLostContact`, `OnMessage`,
  `Delay`, `SetTrigger`, `IncTrigger`, `DeclareGlobalTrigger`
- Связь с игроком: `SendPlayerMessage`, `SendMessage`, `AddMenuItem`,
  `OnMenuItemSelect`, поля `Voice`, `Caption`, `String1`
- Прогресс: `AddScore`, `AddAward`, `SetRank`, `SetStatistics`, `EnableEngBay`
- Камера: `SetCamera`, `SetPlayerPosition`

Фабрики типов групп: `StdCraftAiFactory`, `StdCraftGroupFactory`,
`StdTankGroupFactory`, `StdHangarAiFactory`, `StdSFGAiFactory`,
`StdMsnFactory`, `InstantMsnFactory`, `CaptureGroupFactory`,
`CoopMsnFactory`, `TeamMsnFactory`. Шаблоны поведения называются
`StdCraft`, `StdInstantAction`, `StdSingleCargo`, `StdTank`, `StdCarrier`,
`StdCooperative`, `StdStatic`, `StdStaticHangar`.

Кооператив и команды (`CoopMsnFactory`, `TeamMsnFactory`, `StdTeamplay*`)
есть в том же файле. Формат один, режим другой. Поля каждой команды в
`AICommands.dsc` — имена и типы аргументов схемы; байты внутри `AiScript`
остаются текстом, их грамматика не разбиралась.

## Параметры бота в `AiData.dat`

Читаемые имена, без сетевых `OnBanConnection` / `OnPlayerKick`:

- Стрельба: `FireDistanceMin`, `FireAimError`, `FireCannonBurstTimeMin`,
  `FireRocketBurstTimeMin`, `FireRelaxMin`, `MissileCheckTimeMin`
- Маневр: `TargetStrafeIntensityMin`, `TargetAltitudeMin`, `TargetSearchAltitude`,
  `ThreatSpeed`, `AwayThreatFactor`, `StayOnSixThreatFactor`, `AvoidStartMin`,
  `CheckPointMin`
- Ранги сложности: `Cadet`, `Novice`, `Veteran`, `FLieutenant`, `SLieutenant`,
  `Captain`, `Major`, `Colonel`
- Радиообмен позывными: `Alpha`…`Juliet`, команды `ENGAGE`, `COVER`, `DEFEND`,
  `REPAIR`, `TAKEOFF`, `RETURN`, `FORMATION`

Рядом лежат ключи счёта (`Kills`, `Deaths`, `Scorelimit`, `Timelimit`) — они
общие для мгновенного боя и сетевой комнаты.

## Нативный ИИ

`setup.dat` первой игры перечисляет `Standart.ai` в одном списке с DLL.
Копия в `P1 K6x/standart.ai` — обычный PE-файл (`MZ`), не текст и не `EVG1`.
Во второй игре ту же роль играет `StdLogic.ai` рядом с `Game.exe`. Локации
`.gsd` ссылаются на него полем `AiDlls`.

Поведение бота: числа и пороги — в `AiData.dat`, команды миссии — текст
`AiScript` внутри `EVG1`, исполнитель — нативный модуль `.ai`.
