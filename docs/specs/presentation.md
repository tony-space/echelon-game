# Интерфейс, погода, частицы

## Меню

Первая игра: `StormMenu.dll` и картинки в `pictures.dat` (`PICS`, 46 имён).
Имена — состояния кнопок: `ExitA` / `ExitP` / `ExitD`, так же `Help`, `Site`,
`Information`, `Uninstall`, `Multiplayer`, плюс `Pict1`, `StartsN`, `MsneN`.
Суффиксы A/P/D похожи на active / pressed / disabled. Тело записи `PICS` не
читалось: это может быть BMP, DXT или собственный кадр.

Вторая игра рисует меню из Python 2.2 (`Modules/Menu15/`). Экраны, которые
там заведены классами и файлами: кампания, выбор миссии, ангар (`EngBay`),
управление и клавиши, опции, профиль игрока, титры, статистика. Раскладка
виджетов продублирована в `Menu.dat` (`EVG1`): `EngBayScreen`, `TitleBox`,
`TextBox`, подписи вроде `Craft:`. Исходники меню — код игры, в репозиторий
их не копируем; для ремейка важен список экранов, не реализация виджетов.

`WinRes.dat` в обеих играх — PE (`MZ`), ресурсы окна, не игровой контейнер.

Локализация второй игры заведена в меню (`L10n.py` пустой, класс `L10n` живёт
в `Text.py`). Строки миссий лежат внутри `.gsl`.

## Погода и сцена — `rdata.dat`

Контейнер `DATA`, 213 имён в первой игре. Имя записи — `состояние#слой`:

| Суффикс | Сколько | Примеры |
| --- | --- | --- |
| `layer` | 39 | `daycld#layer`, `dawnsky#layer`, `engbaycld#layer` |
| `env`, `fog`, `scene`, `terr`, `map`, `glight` | по 20 | `default#env`, `NV#fog`, `night#scene`, `dusk2#terr` |
| `planet` | 18 | `dusk#planet`, `storm#planet`, `awards#planet` |
| `terrstate` | 11 | `default#terrstate` |
| `flares`, `sun`, `stars`, `font` | по 2–4 | `dusk#sun` |
| вода | единицы | `water_detail`, `water_base`, `waterbump`, `water_bump`, `terrmtl`, `terrfeatures` |

Набор состояний по строкам `VisualConfigs.dat`: `day`, `day1`…`day3`,
`dawn`, `dawn2`…`dawn4`, `dusk`, `dusk1`, `dusk2`, `night`, `cloudy`,
варианты `_rain`, `_thunder`, `_rain_thunder`, плюс `SceneConfigNormal` и
`SceneConfigNVision`.

Тела записей `rdata` не разбирались. Из имён следует, что время суток — это
набор слоёв (небо, туман, земля, солнце, звёзды, вода), а не один шейдер.

`Graphics/dxm.dat` (`DXMC`, во второй игре `DXMC2`) хранит именованные
состояния рендера: `default`, `infra`, `laser`, `terrain_light`,
`std_transp_add`, `std_transp_filter`, `std_transp_none`, `dayclouds`,
`detailtwater`, `shadow_main`, `sunflares`, `fogboxbstate`. Это профили
фиксированного конвейера DirectX 7, не HLSL.

## Частицы и вспышки

`particles.dat` (`PARS`, 206 имён в первой игре) называет эффекты, а не только
дымы: `HumanPlaneDebrisWaterExpl1`, `VelianGPlaneDuza`,
`HumanPlaneDuzaDustSnow`, `AAMLExpl2`, `TPDtrace`. Струя сопла `Duza*` здесь
встречается как частица. Точка подвески `Duza*` в `objects.dat` по-прежнему
не разобрана — это разные файлы об одном эффекте.

`Flares.dat` (`EVG1`) описывает блики: размер от FOV и интенсивности
(`SizeFOVDepence`, `SizeIntDepence`), затухание (`FadeOffStart`, `FadeOffEnd`),
текстурные координаты, несколько готовых взрывов `ExplFlareSmall` …
`ExplFlareBig`.

## Камеры

В `Configs.cfg` перечислены режимы, не их математика: `CameraCockpit`,
`CameraCombat`, `CameraTracking`, `CameraAttached`, `CameraFree`,
`CameraOrbite`, `CameraTV`, `CameraTactical`, плюс `cockpit_tracking`,
`tactical_inversed`, `attached_xz`. Переключение завязано на команды `cm_mode`
и клавиши F1, F3–F8. Отдельного файла анимации камеры нет.
