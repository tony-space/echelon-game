# Управление

Отдельного файла раскладки клавиш нет. Связки «клавиша → команда» лежат в
`Data/Configs.cfg` (контейнер `EVG1`, игра 1). Макросы поверх команд — в
текстовом `Data/common.dlc`. Во второй игре экран переназначения —
`Modules/Menu15/KeysControls.py`; он редактирует те же сущности (действие,
модификаторы `Alt` / `Ctrl` / `Shift`, две клавиши на действие: front и back).

## Команды, которые читаются из `Configs.cfg`

Имена местами склеены с именами клавиш (`F1cm_mode`, `TABcl_weapon_next`),
потому что между полями `EVG1` нет разделителя, который мы уже поняли. Сами
команды такие:

Полёт: `cl_turn_left`, `cl_turn_right`, `cl_turn_up`, `cl_turn_down`,
`cl_bank_left`, `cl_bank_right`, `cl_move_forward`, `cl_move_backward`,
`cl_move_left`, `cl_move_right`, `cl_move_up`, `cl_move_down`.

Оружие и цель: `cl_attack`, `cl_weapon`, `cl_weapon_next`, `cl_weapon_prev`,
`cl_target_next`, `cl_target_prev`, `cl_target_nearest`, `cl_target_threat`,
`cl_target_accept`, `cl_target_at_recticle`.

Камера и вид: `cm_mode`, `cm_fov`, `cm_night_vision`, семейство `cm_move_*` и
`cm_turn_*` (свободная камера отдельно от самолёта).

Радар и связь: `cm_radar_range`, `cl_radio`, `cl_radio_command`,
`cl_radiolog_mode`, `cl_icons_enemy`, `cl_icons_friendly`.

Прочее: `cl_auto_throttle`, `eng_bay`, `quit`.

Клавиши, которые удалось прочитать рядом: стрелки, `SPACE`, `TAB`, `ESCAPE`,
`F1`, `F3`–`F9`, `BACKSPACE`, `SHIFT`, `CTRL`, цифровой блок, кнопки мыши,
колесо, `j_button*`, `j_hat*` (джойстик; в ремейке геймпад отложен).

## Макросы `common.dlc`

Текст, не `EVG1`. Действие — имя и список команд в скобках. Файл задаёт
переключатели, а не оси:

- зум 30° / 90° (`cm_fov`, `hud_horizon_step`)
- дальность радара 1000, 2000, 5000, 10000, 20000 (`cm_radar_range`)
- ПНВ вкл/выкл (`cm_night_vision`)
- автопилот вкл/выкл
- режим журнала радио и его положение на HUD (`hud_radio_x/y/w/h`)
- взгляд в сторону (`GLANCE`, `GLANCEALT`) и упор по крену (`MAX_TURN`)

`user.dlc` — комментарий `// put your custom settings here`. Игровые настройки
пользователя в этот файл не записаны (установка почти пустая).

## Режимы, которые заведены в настройках, но не в песочнице

По строкам `Configs.cfg`, без разбора значений: коллизия с землёй и с
объектами (и читы `NOGroundCollision`, `NOObjectCollision`, `UnlimitedAmmo`,
`UnlimitedArmor`), автопилот, инженерный отсек, пауза, ускорение времени
(`srv_accel` — имя серверное, к одиночной паузе тоже привязано), скриншот,
гамма, детализация текстур и звука, флаги рендера `USEMIPMAP`, `USEBUMP`,
`USE32BPPTEX`, `USEAA`, `DRAWTERRAIN`, `DRAWSKY`, `DRAWWATER`, `DRAWPARTICLES`.

Ось визира мышью, которая есть в ремейке, в этих именах прямо не называется.
Ближайшее — `ReticuleSelect`, `cl_target_at_recticle` и чувствительность
`Sensitivity` в блоке мыши.
