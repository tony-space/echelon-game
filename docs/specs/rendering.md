# Цвет и освещение ремейка

Это не формат оригинала, а то, как песочница считает кадр.

Весь свет считается в **линейном RGB**.

- Альбедо грузится как `GL_SRGB8_ALPHA8` (`Texture(path, ColorSpace::Srgb)` —
  значение по умолчанию). Маски и прочие нецветовые текстуры — явно
  `ColorSpace::Linear`, внутренний формат `GL_RGBA8`.
- Кадровый буфер sRGB: `GLFW_SRGB_CAPABLE` при создании окна и
  `glEnable(GL_FRAMEBUFFER_SRGB)` в `platform/window.cpp`. Включение **нельзя
  гейтить** по `GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING`: WGL для окна отвечает
  `GL_LINEAR`, хотя кодирование работает. Условная версия давала тёмный кадр.
- Константа цвета, подобранная на глаз (небо, тинты, цвет визира, diffuse из
  `materials.dat` — тот умножал гамма-кодированные тексели), — это sRGB.
  В линейный её один раз переводит `ech::srgbToLinear`
  (`src/core/include/echelon/math/color.hpp`, тест `tests/color_test.cpp`).
  В шейдеры sRGB-числа не попадают: `lit.frag` получает `uSunColor`,
  `uSkyAmbient`, `uGroundAmbient`, `uFogColor` уже линейными (структура
  `Atmosphere` в `app.cpp`). `glClearColor` — тоже линейное небо.

Туман — Бугер–Ламберт в линейном пространстве: `T = exp(−σ·dist)`,
`color = mix(fogColor, color, T)`. `σ = 0.000085` 1/м, оптическая толщина 1
на ~12 км. Прежний коэффициент `0.00012` подгонялся под смешивание в sRGB и
больше не годится.
