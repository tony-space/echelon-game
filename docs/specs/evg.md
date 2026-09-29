# `EVG1` — миссии, меню, настройки

Самый частый непрочитанный контейнер. Им пользуются и первая, и вторая игра.
Сжатия zlib в начале файла нет: строки лежат открытым текстом среди бинарных
полей.

## Заголовок — опознано

```
char magic[4] = "EVG1"
u32  field          // не размер файла. У Configs.cfg это 5664 при файле 24726
u32  0
u32  0
u32  8
u32  8
u32  count          // правдоподобно: число корневых объектов. Не доказано
... дальше смесь строк и бинарных полей ...
```

Поле `count` для ориентира: `AiData.dat` — 4, `NetArena.ros` — 4,
`Instant Action.gsl` — 14, `AICommands.dsc` — 45, `Flares.dat` — 132,
`Menu.dat` — 1054. Границы объектов после заголовка не восстановлены.
Имена иногда склеены через `#` (`dawn#scene`) — тот же приём, что у записей
`rdata.dat`.

Крупные `.gsd` (сотни килобайт и мегабайты) в первых 8 КБ строк не содержат.
Текст миссии сидит в парном маленьком `.gsl`.

## Что каким файлом кодируется

| Расширение / имя | Игра | Содержимое по строкам |
| --- | --- | --- |
| `*.cmp` | 1 | Миссия целиком (логика и данные в одном файле) |
| `*.gsl` | 2 | Текст миссии, список `Voice Files`, события, описание |
| `*.gsd` | 2 | Бинарные данные той же миссии. Пары: `Instant Action.gsl` + `.gsd`, `Wind Warriors`, `Training Course`, … |
| `*.ros` | 2 | Россыпь объектов на карте (`Density`, `ObjectName`, `treesa`) |
| `Configs.cfg` | 1 | Видео, звук, управление. См. [input.md](input.md) |
| `VisualConfigs.dat` | обе | Погода и частицы погоды: `day`, `dusk`, `night`, `cloudy_rain_thunder`, поля `LifeTime`, `SpawnDist`, `PlayThunder` |
| `GameData.dat` | обе | В первой игре строки в начале не читаются (в отличие от остальных `EVG1`) |
| `AiData.dat` | обе | Параметры бота и сетевые сообщения. См. ниже |
| `AICommands.dsc` | 2 | Схема команд миссии: имя команды и имена её полей |
| `Flares.dat` | обе | Вспышки: `ExplFlareSmall`, `Brightness`, `TexCoords`, `FadeOffStart` |
| `Menu.dat` | 2 | Экраны: `EngBayScreen`, `TitleBox`, `CraftsListTitle` |
| `Dedicated.epp` | 2 | Маленький `EVG1`. Сеть, не разбирался |

`common.dlc` и `user.dlc` — **не** `EVG1`, а обычный текст. `user.dlc` содержит
только комментарий-заготовку.

## Команды миссии — каталог по именам

`AICommands.dsc` перечисляет команды и фабрики, не раскладку байт. Команды,
которые относятся к одиночной игре:

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
есть в том же файле. Это уже пограничный с мультиплеером слой: формат один,
режим другой. Поля команд не разбирались.

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
Во второй игре ту же роль играет `StdLogic.ai` рядом с `Game.exe`.

Поведение бота, судя по этому разделению: числа и пороги — в `AiData.dat`,
команды миссии — в `EVG1`, сам исполнитель — в нативном модуле `.ai`.
