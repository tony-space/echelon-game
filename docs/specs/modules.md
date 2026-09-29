# Модули движка

Снято с PE обеих установок: таблица импортов, экспорты, баннеры версии и
строки ассертов. Дизассемблирования нет. Повторить обход:

`python tools/pe_survey.py legacy/Echelon "legacy/Echelon Wind Warriors"`

`--json <file>` пишет тот же разбор машиной (сырые и расшифрованные имена,
импорты по функциям). Один файл вместо каталога тоже принимается. `P1 K6x/` — вторая копия DLL первой
игры, отдельно не разбиралась. Сокеты, GameSpy и `dedicated.exe` здесь только
как имена зависимостей.

Уверенность: граф DLL и публичные экспорты — **разобрано**. Список `.cpp` —
**опознано**: в файл попали только те единицы, которые оставили строку
ассерта или лога. Остальные исходники в бинарнике безымянны.

## Чего в файлах нет

Линкер MSVC 6 (`6.00`), COFF-символов ноль, секций отладки нет. RTTI
выключен: `.?AV` есть только у классов исключений `MadTools` и у меню второй
игры на Boost.Python. Файлов `.pdb` в установке нет. У нескольких DLL второй
игры в каталоге CodeView записан путь, самого файла рядом нет:

| Модуль | Путь, который ждал линкер |
| --- | --- |
| `StormGame.dll` | `D:\Echelon\ECHELON15\[api]\StormGame.pdb` |
| `Renderer.dll` | `D:\Echelon\Echelon15\[Version]\Renderer.pdb` |
| `StdLogic.ai` | `D:\Echelon\ECHELON15\[api]\StdLogic.pdb` |
| `MDlg.dll` | `D:\Echelon\Echelon15\[api]\MDlg\Debug\MDlg.pdb` |
| `Preview.dll` | `D:\Echelon\Echelon15\[api]\Preview.pdb` |

Внутренние функции `StormGame` и `Renderer` по имени не восстановить.
Навигация — по фабрике модуля, по экспорту библиотеки или по строке лога,
которая указывает на `.cpp`.

Дерево, зашитое в эти строки:

- первая игра — `C:\Echelon\sources\`, заголовки `C:\Echelon\Include\`
  (`tarray.inl`);
- вторая — `D:\Echelon\ECHELON15\sources\`.

Даты линковки первой игры — 17–24 апреля 2001 (`Product.dll` — 23 июля 2001).
Второй — с 29 октября 2002 (`StdLogic.ai` — 6 апреля 2004).

## Как это стыкуется

Почти каждая DLL отдаёт наружу одну-две функции `Create*`. Остальное
вызывается уже через указатель, полученный из фабрики.

Первая игра. `Game.exe` (~76 КБ) поднимает окно и читает флаги рендера
(`DRAWSKY`, `DRAWTERRAIN`, `DRAWWATER`, `DRAWPARTICLES`, `ZSPLITSCENE`),
затем зовёт `CreateStormMenu`. Игру собирает `StormMenu.dll`. Единственный
вход в симуляцию:

`CreateGameHolder(LOG*, MadSocketsManager*, MadInput*, CommandsApi*, RendererApi*, ISound*) → iGameHolder*`

Вторая игра. `Game.exe` — 5 КБ и статически тянет только `Menu.dll` и
`Product.dll`. `Menu.dll` экспортирует `ShowMenu` и уже оттуда грузит
рендер, звук, ввод (`EInput.dll`, DirectInput 8), `createGameHolder2` и
`createLocationHolder`. Локация во второй игре вынесена из держателя игры.
Меню само на Python 2.2: `PyMenu.dll` (`Menu15W`, `GameCore::openGame`,
`GameCore::start`), диалоги `MDlg.dll`, превью техники `Preview.dll`.

`setup.dat` первой игры — список файлов установщика, не порядок `LoadLibrary`.

## Карта

| Модуль | Вход | Роль |
| --- | --- | --- |
| `Game.exe` | — | Лаунчер. Во второй игре почти пустой |
| `StormMenu.dll` / `Menu.dll` | `CreateStormMenu` / `ShowMenu` | Сборка подсистем, экраны. Во второй игре меню уехало в `PyMenu` |
| `StormGame.dll` | `CreateGameHolder` / `createGameHolder2`, `createLocationHolder` | Симуляция: сцена, борт, оружие, снаряды. См. файлы ниже |
| `StormData.dll` | таблицы `*_DATA` | Читает `gdata`: борт, объекты, обломки, пыль, взрывы, дороги |
| `Renderer.dll` | `CreateRenderer` → `RendererApi` | DirectDraw. Классы в логах: `Cd3d`, `TexFillerP8`, `TextureFactory`, `DllData` |
| `Textures.dll` | `CreateTextureFactory` | Поверхности и видеорежимы, внутри куски D3DX (`CD3duContext`, `CDDrawDeviceNode`) |
| `TextureData.dll` | `GetMipLinearSize`, `GetSubMipSize` | Размеры мипов, без импортов |
| `Clip.dll` | `clip::ClipIndexed` | Отсечение индексного буфера по плоскости. Импортов нет |
| `Terrain.dll` | `TERRAIN_DATA::*` | Высота, вода, луч, открытие `.bx` / `.sq` / `.vb` |
| `Environment.dll` | `CreateDH`, `CreateNS`, `CreateRB` | `CreateDH` — data hasher, `CreateNS` — navigation system. `CreateRB` баннера не имеет; в этой же DLL загрузчик `ROADDATA` |
| `HashTools.dll` | `CreateHasher2`, `CreateCl` | «Hasher». Импортирует `Terrain.dll` |
| `Feature.dll` | `CreateFeatureManager` | Меши фич: рендер логирует `FEATURE failed load mesh` |
| `Sound.dll` | `CreateSInstance` → `ISound` | DirectSound. В логах `Sound`, `Listener3D` |
| `MadInput.dll` / `EInput.dll` | `CreateMadInput` / `createInput` | DirectInput / DirectInput 8 |
| `MadCommands.dll` | `CreateCommands` → `CommandsApi` | Консоль и команды |
| `Controls.dll` | `CreateMDialog` | Диалоги Win32. Во второй игре их сменил `MDlg` |
| `UniVars.dll` | `CreateUnifiedVariableDB` | База переменных, соответствие расширения файла и class id |
| `MadTools.dll` | см. ниже | Общая библиотека: лог, архив, текст, матрицы |
| `Standart.ai` / `StdLogic.ai` | `CreateAiModule` | ИИ. Это обычный PE, не скрипт |
| `AiCompiler.dll` | `CreateAiCompiler` | Только вторая игра, рядом редактор миссий |
| `GameSet.dll` | `CreateGameSet`, `CreateAiEnumerator`, `CreateStormDataEnumer` | Только вторая игра: набор миссии поверх `StormData` |
| `Video.dll` | `CreateVideo` | Только вторая игра, `AVIFIL32` |
| `Player.dll` | `PlayerCode` | Проигрыватель роликов. В логах `MediaPlayer` |
| `Product.dll` | `GetPI` | Идентификация продукта, реестр |
| `RendererConfig.dll` | `LoadRendererConfigUV` | Только первая игра |
| `DxEnum.dll` | `CreateVideoInfo`, `GetSoundInfo` | Перечисление устройств |
| `GSView.dll`, `GSEdit.dll`, `Options.exe` | `createEditor`, `$DoGSEdit` | Редактор. `GSView` на MFC 4.2 и GDI+, к полёту не относится |
| `PyMenu.dll`, `MDlg.dll`, `Preview.dll` | `Menu15W::*`, `initializeMDlg`, `Preview::reset` | Меню второй игры. Единственные модули с живым RTTI |

`StdTools.dll` во второй игре забирает у `MadTools` создание лога
(`createLOG`).

## `StormGame`: файлы, которые светятся в строках

Ассерт склеен с `__FILE__`, поэтому имя единицы компиляции видно даже без
символов. Это не полный список исходников модуля — только файлы, где ассерт
остался в релизе.

Общие для обеих игр:

| Файл | О чём строка |
| --- | --- |
| `BaseClasses.cpp` | базовые классы симуляции |
| `BaseScene.cpp` | сцена, `MustExist` |
| `BaseObject.cpp` | объект на сцене, связь `Link` |
| `BaseCraft.cpp` | борт; слоты `PrimaryWeapon` / `SecondaryWeapon` / `RocketWeapon` класса `SC_WEAPON_SLOT` |
| `BaseHangar.cpp` | ангар. Во второй игре ещё `myHaveLand`, `myHaveTakeoff`, `myHaveBase` |
| `BaseWeaponSlot.cpp` | слот, `pBarrel`, `pFlash`, `DetachObject` |
| `HostClient.cpp` | локальный клиент |
| `HostSceneCreateItems.cpp` | создание предметов на хосте |
| `RemoteScene.cpp` | удалённая сцена |
| `RemoteSceneCreateItems.cpp` | создание предметов на клиенте, слот оружия |
| `PlayerInterface.cpp` | интерфейс игрока |
| `ProjectileMissile.cpp` | ракета |
| `ProjectileVisualMeshAndParticle.cpp` | снаряд: меш и частица |
| `ProjectileVisualOnlyMesh.cpp` | снаряд: только меш |
| `WeaponSystemForTurretWithMissiles.cpp` | турель с ракетами |

Только вторая игра: `BaseSubobj.cpp`, `SubobjFactory.cpp`,
`BaseCraftAutopilotsHangar.cpp`, `BaseItemPacketsCacher.cpp`,
`WeaponSystemForTurretWithHTGR.cpp`.

Отдельные логи называют класс, даже если файла в списке нет:

- `BaseCraft::CornerSpeed`
- `BaseCraftAutopilotRemote::OnUpdate`
- `BaseObject` — `Org`, `HeadingAngle`, `PitchAngle`, `RollAngle`, `Condition`,
  `IsRemote`; вложенный `pFPO` с `Org`, `Right`, `Up`, `Dir`, `MaxRadius`
- `LocalClient::Capture(iContact*)` — захват юнита игроком

Рендер своих `.cpp` в строки не кладёт (ассерты идут форматом
`Assertion failed at file %s`). Классы видны по логам: `Cd3d`,
`TexFillerP8` (заливки `P8`, `ARGB16`, `ARGB32`, `DUDV`), `TextureFactory`,
`DllData`. Окно второй игры называется `MadiaRendererWindow`.

## `StormData`: публичные таблицы

Экспорты совпадают с разделами `gdata.dat`. У каждой таблицы
`GetByCode`, `GetByName`, `GetFirstItem`, `nItems`:

`CRAFT_DATA`, `OBJECT_DATA`, `SUBOBJ_DATA`, `DEBRIS_DATA`, `DUST_DATA`,
`EXPLOSION_DATA`, `ROADDATA`, плюс `UnitDataTable` (`GetIdxByName`,
`GetIdxByType`).

У борта наружу торчат ровно две кривые, которые мы уже читаем из текста:
`CRAFT_DATA::ThrustCFromAlt`, `CRAFT_DATA::CornerCFromSpeed`, и скаляр
`CRAFT_DATA::BaseDragC`. Гравитация — `STORM_DATA::GAcceleration`.
Раскладка частей объекта — `OBJECT_DATA::GetLayout` → `LAYOUT_DATA`.
Во второй игре добавлен безымянный вход `getGameData`.

## `Terrain`: публичный API

Класс один, `TERRAIN_DATA`. Для чтения карты: `Open`, `OpenBx`, `OpenSq`,
`OpenVb`, `OpenLight`, `ResetHdr`, `Close`. Для запросов: `GroundLevel`,
`GroundLevelMedian`, `WaterLevel`, `TraceLine`, `TraceLineIn`,
`TraceLineOut`, `TraceSquareLine`, `TraceBoxLine` (боксы `T_BOX` /
`T_VBOX`), `ClipLine`. Во второй игре ещё `openRandomObject` /
`saveRandomObject` → `IRObjectsData` (россыпь `.ros`).
Разбор кода — [terrain_dll.md](terrain_dll.md).

## ИИ

`Standart.ai` / `StdLogic.ai` экспортируют `CreateAiModule`. Внутри три
файла, всплывшие из ассертов: `BaseCraftAi.cpp`, `SingleBFAi.cpp`
(`BFSkill`, `BaseCraftSkill`), `StdGroupAI.cpp`. Строки второй игры
добавляют имена поведения, не функции: `AvoidTerrain`, `HangarStatus`,
`StdCraft`, `StdCraftGroup`, `StdStaticHangar`, `FireAimError`,
`TurretFireAimError`. `ParseScript` есть только в первой игре.

## `MadTools`

Общая база, экспорты декорированы, поэтому классы читаются:

- файлы и архив — `DB`, `DbServer`, `DbResolve`, `_HFILE`, `_HMAP`,
  `READ_TEXT_FILE`, `WRITE_TEXT_FILE`;
- лог — `LOG` (`VMessage`, `AddException`, окно лога);
- математика — `MATRIX` (`TurnRightPrec`, `TurnUpPrec`, `BankRightPrec`,
  `Vectors2Angles`, `Angles2Vectors`), `Matrix3f`, `Matrix34f`, `Matrix4f`,
  `PLANE::ClipVector`;
- прочее — `BMP_TOOL`, `Crc32`, `CodeString`.

Исключения те же, что торчат RTTI у игровых DLL: `EXCEPTION`,
`GENERIC_EXCEPTION`, `ASSERT_EXCEPTION`, `DISKIO_EXCEPTION`,
`DISKIO_PARSING_EXCEPTION`. Игровые модули ловят их из `MadTools`, своих
иерархий классов в RTTI нет.

## Куда смотреть дальше

Имя алгоритма внутри `StormGame` или `Renderer` в экспорте не лежит.
Следующий шаг точечный: взять строку из таблицы файлов (например
`BaseCraft::CornerSpeed` или `ProjectileMissile.cpp`), найти её в DLL и
перейти по ссылке на функцию. Имеет смысл делать это по одной системе —
борт, снаряд, высота земли, — а не снимать весь модуль.
