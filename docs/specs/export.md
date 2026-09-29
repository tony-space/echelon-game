# Экспорт в ремейк

`python tools\legacy_export.py [--craft Human_BF1|all --lod 0 --damage N]`
пишет в `assets/legacy/` (каталог в `.gitignore`, в репозиторий не входит).

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
в логе подсказка перезапустить экспортёр. Если файла модели нет, песочница
показывает болванку `makePlaceholderCraft()`.

Рендер: сначала непрозрачные сабсеты, потом прозрачные (diffuse alpha в
(0, 1)) со смешиванием и выключенной записью глубины. Diffuse материала идёт
в `uTint` уже после перевода в линейный RGB, альфа — в `uAlpha` шейдера
`lit.frag`. Про освещение — [rendering.md](rendering.md).
