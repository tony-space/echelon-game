# Экспорт в ремейк

`python tools\legacy_export.py [--craft Human_BF1|all --lod 0 --damage N]`
`python tools\legacy_export.py [--static <имя>|all --lod 0 --damage N]`

пишет в `assets/legacy/` (каталог в `.gitignore`, в репозиторий не входит).
`--static` и `--craft` вместе не задаются. Каталог данных по умолчанию —
`legacy/Echelon Wind Warriors/Data`.

`--static` принимает заголовок `Static("...")` либо `FileName`. Имя без блока
`Static` (секция моста `railroadbridge600m_part`) экспортируется как один
меш. `--static all` обходит каждый `Static` — для песочницы это сотни
экспонатов, пакетом не гонялось. Позиции — из `objects.dat` (система D3D,
`d3d_to_engine`), как у самолёта; лимит поиска узла для статика поднят
(мост и радар не влезают в 40 м). Если bbox не совпал, узел добирается по
имени из `objects2.dat` (см. [containers.md](containers.md)) и отбрасывается,
когда bbox расходится больше чем на 20 м. Текстура, которой нет в
`textures.dat`, берётся из `atextures.dat`.

Без `--damage` — каждое состояние `Im`, которое есть в `mesh.dat`.
`--craft all` обходит каждый `Craft("...")` в `gdata.dat` и пропускает те,
у чьего `FileName` нет записей под шаблоном `Im`/`Ld` (вельяне с `Img`/`Lod`
поэтому пропускаются, см. [containers.md](containers.md)).

На каждое состояние:

- `models/<file>_im<N>.model.json`
- `models/<file>_im<N>_<part>.emesh`
- `textures/<tex>.png`

Смещения считаются один раз по целому мешу (`Im0`) и переиспользуются для
остальных состояний: у повреждённой геометрии другой bbox, и узел
`objects.dat` по нему не находится.

`cockpit_CoPilot` — катапультировавшийся пилот на дельта-крыле, единственная
часть с состоянием `Im3`. В целом состоянии он скрыт (`HIDDEN_BY_DEFAULT`,
в JSON `"visible": false`). Показ — флаг песочницы `--show-hidden`.

## `model.json`

У самолёта в корне поле `craft`, у статика — `static` (заголовок из
`gdata` или `FileName`, если блока `Static` нет). Загрузчик это поле не
читает: ему нужны только `parts`. Иерархии в загрузчике нет, части ставятся
абсолютными `position` и `orientation`, как у самолёта. Статик без дерева
частей — один элемент `HULL` в нуле.

```
parts: [{
  name, mesh, parent,
  position,          // абсолютная, в системе корня модели
  orientation,       // абсолютный кватернион x, y, z, w
  local_position,    // относительно родителя
  textures, materials, visible
}]
```

Загрузчик читает кватернион как `(w = q[3], x = q[0], y = q[1], z = q[2])`
и ставит часть матрицей `translate(offset) · mat4_cast(orientation)`.

## EMSH v2 — разобрано

```
char magic[4] = "EMSH"
u32 version = 2
u32 nVerts, nIndices, nSubsets
nSubsets × {
  u32 firstIndex, u32 indexCount,
  char texture[64], char material[64],
  f32 diffuse[4], f32 specular[3], f32 power
}
nVerts × 8 f32 (pos, normal, uv)
nIndices × u32
```

Читает `src/app/render/model.cpp`. При другой версии модель не грузится,
в логе подсказка перезапустить экспортёр. Песочница показывает все
экспортированные модели (`*_im0.model.json`); если их нет, самолётов в сцене нет.

Рендер: сначала непрозрачные сабсеты, потом прозрачные (diffuse alpha в
(0, 1)) со смешиванием и выключенной записью глубины. Diffuse материала идёт
в `uTint` уже после перевода в линейный RGB, альфа — в `uAlpha` шейдера
`lit.frag`. Про освещение — [rendering.md](rendering.md).

## Статики, выгруженные для проверки

Восемь, LOD 0, все состояния `Im`, которые есть. Песочница ставит каждый
`*_im0.model.json` в ряд; кадр сверху —
`build-x64-msvc17/screenshots/smesh_check.png`
(`--cam 43220 500 -31488 0 -80`). Размер — AABB в системе движка, метры.

| файл | `static` | части Im0 | треугольники | размер xyz |
|---|---|---|---|---|
| `bunker3` | Bunker Light | 3 | 292 | 40.1 × 15.7 × 40.1 |
| `angar_sklad_150m` | Military Hangar 150m | 2 | 44 | 44.2 × 18.4 × 150.0 |
| `dom_mil` | Military Bld 0 | 1 | 282 | 87.3 × 33.6 × 154.6 |
| `tower1` | Tower 1 | 4 | 350 | 27.9 × 88.7 × 32.7 |
| `radarc1` | Radar Complex 1 | 20 | 1239 | 336.8 × 202.3 × 372.9 |
| `gravang_big` | Craft Hangar Big | 9 | 428 | 190.6 × 75.8 × 865.1 |
| `vs_bunker3` | V Bunker 3 | 3 | 932 | 55.4 × 14.6 × 53.5 |
| `railroadbridge600m_part` | то же имя, блока `Static` нет | 1 | 508 | 80.0 × 853.7 × 600.0 |

Габариты совпадают с комментариями брони в `gdata` (ангар ровно 150 м по Z,
бункер 40×16×40, `dom_mil` 87×34×155). Полосы `PolA`…`PolF` у
`gravang_big` — кусок ВПП. У моста длина по Z ровно 600 м, но шесть
индексированных вершин исходного меша лежат на Y = +777 (в движке −777,
ниже настила). Песочница сажает модель по `boundsMin`, поэтому настил
оказывается в воздухе. Вершины не вырезались: это геометрия оригинала, не
ошибка осей. Корпус ангара и башни оставлен в нуле — узел `objects.dat`,
на который указывал `objects2`, расходился с bbox меша больше чем на 20 м;
длина ангара при этом всё равно 150 м за счёт части `AngA`.
