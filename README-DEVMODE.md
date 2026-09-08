# README-DEVMODE.md — Baseline del Developer Mode (FrogShell + TreeFrogUI)

**Fecha:** 2026-09-08 (iteración 8 — grid 10 cols + flecha caps + auditoría profunda)

## Iteración 8 — Grid 10 columnas + caps flecha + auditoría completa (2026-09-08)

**Teclado (layout final, 10 columnas exactas, sin columna extra)**:
```
[1][2][3][4][5][6][7][8][9][0]
[Q][W][E][R][T][Y][U][I][O][P]
[A][S][D][F][G][H][J][K][L][⇧]   ⇧ = flecha Mayús dibujada (llena=caps ON, hueca=OFF)
[Z][X][C][V][B][N][M][_][-][SYM]  ← SYM fijo en su posición en AMBAS páginas
[CTRL ][ ALT  ][   SPACE    ][DEL][ENTER]
```
- **⇧ flecha de mayúsculas dibujada con rects** (independiente de la fuente, como los teclados físicos); fondo resaltado cuando caps activo.
- **SYM persiste**: tecla fija (fila 3 col 9) en ambas páginas; el cursor se mantiene sobre SYM al cambiar de página (igual que ⇧).
- **Fix toggle mayúsculas en FM**: la conversión a minúscula ya no depende del contexto — funciona igual en FM y terminal (FM arranca en ABC por defecto, terminal en abc).
- Caps = solo case de letras (BloqMayus). Los `!@#$%` viven en la página SYM.
- X = espacio (coexiste con la tecla SPACE).

**Auditoría completa del ecosistema (subagent, hallazgos corregidos)**:
- BUG terminal.c: wrap de línea >127 chars perdía el carácter desencadenante → corregido.
- BUG ANSI: `[` cerraba el estado CSI inmediatamente → "31m" visible tras ESC[31m. Parser de 3 estados (ESC→CSI→final) corregido.
- BUG process.c: `[exit N]` no aparecía si un nieto heredaba el pipe → exit code se decodifica al reap del hijo, sin esperar EOF.
- BUG frogshell.c: zombie/bloqueo si el usuario volvía al FM con proceso vivo → `process_poll(NULL)` corre SIEMPRE que haya proceso, sin gate por mode.
- BUG estado: leaks de keyboard_ctrl/alt en START/B/dev-off → helpers únicos `osk_submit_terminal()/osk_save_fm()` reemplazan la lógica triplicada; begin_keyboard resetea TODO el estado.
- BUG devmode: stat()×2 por frame en el hot path → `is_enabled` ahora es latch puro en RAM; flags re-leídos en init y en cada toggle de chord (flag persistente tras boot sigue ganando).
- Tecla fantasma (0,9) fila 0 símbolos → fila completada a 10 chars.
- Duplicados eliminados: `join_path` con ramas idénticas unificadas; submit/rename triplicados unificados; rama muerta dev_evt>0 eliminada.
- Muertos eliminados: OP_NONE, process_terminate, devmode_reset, devmode_chord_event, terminal_note_launched, terminal_reset, terminal_history_count/terminal_history, tab/esc del protocolo usbkbd.
- Includes muertos eliminados (ctype, unistd, stdarg, string, sys/stat ×2 módulos).
- is_stderr eliminado end-to-end del callback de output.
- Menores: cwd se fija tras el check de proceso; physical_keyboard_input tras el check de chord; comentario de signal corregido (SIGTERM).

Deploy iteración 8: solo `G:\cubegm\cores\frogshell_libretro.so` (SHA256 0805DF20...). Compilación 0 warnings. Pendiente de commit (junto con la confirmación de prueba física del layout nuevo).

---

## Iteración 7 — Layout teclado físico unificado + auditoría multi-consola (2026-09-08)

**Teclado (layout físico real, unificado FM/terminal)**:
- Grid de **11 columnas**: 10 de letras + **rail derecho** (col 10) con teclas laterales como un teclado físico: fila 2 = `abc/ABC` (encima de CTRL), fila 3 = `SYM`; en página símbolos el rail fila 2 = `abc` (volver a letras).
- **Fila inferior de modificadores**: `CTRL(2) ALT(2) SPACE(4) DEL(1) ENTER(2)` — CTRL completamente abajo-izquierda, ALT a su derecha, luego SPACE, DEL y ENTER — igual en FM y terminal (solo el default de case difiere: FM=ABC, terminal=abc).
- **ENTER unificado**: en terminal ejecuta el comando; en FM hace rename/new-folder (mismo look, acción por contexto). DONE eliminado.
- **Tamaño reducido** (ENTER se salía en 480px): key_h 26px (era 30), panel 40 (era 44), gaps 3 (era 4), hint compacto. Grid completo: 40+6+4×29+29+5+14 ≈ 210px de alto en 480.
- **Fix bug SYM→Space**: el toggle de página ya no resetea `keyboard_col` a 0; mantiene la columna (clamp solo si estaba en rail).
- Fix: símbolos de la página SYM no se producían (kbd_char_at con lógica invertida).
- Navegación LEFT/RIGHT salta celdas de rail vacías (filas 0-1 col 10); al caer verticalmente en fila sin rail desde col 10 → col 9.

**AUDITORÍA DE PORTABILIDAD 7 CONSOLAS (evidencia en TreeFrogUI/picoarch/upstream)**:

| Contrato | r36sx (v2.6/v2.7, R36HD) | sf3000 | sf3500 (SF3000HD, SF3100) | gb350 |
|---|---|---|---|---|
| Kernel 4.4.186-release | ✔ (modules.txt físico) | ✔ (usb_mode usa `uname -r`) | ✔ | ✔ (config idéntico R36SX) |
| /bin/sh busybox ash | ✔ | ✔ (mismo template zhijack) | ✔ | ✔ |
| /tmp tmpfs escribible | ✔ | ✔ | ✔ | ✔ |
| joy_key shm (input) | ✔ | ✔ | ✔ | ✔ |
| fork en proceso core | ✔ (tfhijack físico, FrogUI fork+waitpid, system() screenshots) | ✔ | ✔ | ✔ |
| Rotación para cores | ninguna (fbwrite, canvas 640×480 landscape) | picoarch rota al presentar (854×480) | ídem sf3000 | ninguna (disp_frame 640×480) |
| evdev /dev/input | ✔ confirmado (hcprojector stock) | ~ presumido (sin evidencia directa) | ✔ citado (sleep en kernel evdev) | ~ presumido |

**Conclusiones de la auditoría**:
1. **Sin bloqueadores estructurales** en ninguna familia: kernel/sh//tmp/shm/fork son contrato universal de boot.
2. **Rotación**: el OSK dibuja canvas landscape y picoarch transforma al presentar (igual que FrogUI). NO re-introducir lógica TF_ROTATE en el core (fue eliminada deliberadamente en 7894d88/21d7abf).
3. **Riesgo medio único**: HID de teclado USB OTG — sin evidencia de hid.ko en los firmwares stock (solo gadget/crypto .ko). `usbkbd.c` degrada limpio si no hay nodos. **Prueba física por familia requerida** para OTG.
4. **RAM**: DEV añade ~100KB vs 128MB documentados (R36SX) — irrelevante.
5. **dynarec caution** (plat_sdl.c L2699: fork perturba mapeos de gpsp): NO aplica — FrogShell corre en picoarch (no-hi) y nunca con cores dynarec; hijos exec-ean y mueren; fd>2 cerrados.

Deploy iteración 7: solo `G:\cubegm\cores\frogshell_libretro.so` (SHA256 4CB0EEA7...). Compilación 0 warnings. Cambios pendientes de commit (se acumulan con la siguiente prueba física).

---

## Iteración 6 — Fila modificadores + teclado físico OTG (2026-09-08)

**Teclado virtual (más parecido a físico)**:
- Fn-row terminal ahora es una fila de **modificadores reales**: `SPACE(2) abc/ABC(2) CTRL(2) ALT(1) DEL(1) SYM(1) ENTER(1)` — abc/ABC a la derecha de SPACE (donde va shift en un teclado físico).
- **CTRL/ALT sticky** (se iluminan al activarse, one-shot: se limpian al teclear la siguiente tecla o ENTER). Reservados para extensiones futuras (Ctrl+C del OSK no aplica — el interrupt es botón Y).
- **SHIFT+dígitos** produce los símbolos físicos (`!@#$%^&*()`) como en un teclado real (la página SYM queda para el resto: `/.,;:!"'\` `()[]{}<>|&` `*=~+@#%^$_`).
- **Botón X = espacio** en todo MODE_KEYBOARD (FM y terminal), sin eliminar la tecla SPACE del grid.
- Hint bar actualizado.

**Teclado físico OTG (`usbkbd.c/.h`, nuevo módulo ~130 líneas)**:
- evdev POSIX puro: abre `/dev/input/event0-7` O_NONBLOCK, **detección de teclado por capability bitmap** (`EVIOCGBIT(EV_KEY)`: KEY_Q + KEY_SPACE presentes) — ignora nodos que no son teclados (gpads).
- Mapa US QWERTY (plain + shift), teclas: Enter, Backspace, Tab, Esc, flechas, PgUp/PgDn, **Ctrl+C** (interrupt).
- Modificadores trackeados (shift/ctrl/alt), autorepeat ignorado (el OSK no necesita repeat).
- Buffer de eventos 32 slots (ring).
- **Integración**: `physical_keyboard_input()` en `input_loop()`:
  - En `MODE_TERMINAL`: **escribe directo en la línea de comandos** (sin abrir el OSK): chars, Backspace, Enter=submit, ↑↓=historial, PgUp/PgDn=scrollback, Ctrl+C=interrupt.
  - En `MODE_KEYBOARD` (OSK abierto): alimenta el mismo `prompt` que las teclas virtuales (FM rename/new-folder incluido, con Enter=Save).
- Init en `retro_init` (después de terminal_init), close en `retro_deinit`.
- NOTA portabilidad: si una consola no tiene `/dev/input/event*` o el kernel no expone HID, `usbkbd_init` no abre nada y el resto funciona igual (degradación limpia). Pendiente prueba física con teclado OTG real.

Deploy iteración 6: solo `G:\cubegm\cores\frogshell_libretro.so` (SHA256 B4BBCAEC...). Compilación 0 warnings. Commit + push a ozkaoz/FrogShell (main) — sin PR.

---

## Iteración 5 — Teclado shift/enter + terminal case-sensitive + cursor (2026-09-08)

**CAUSA RAÍZ de "uname/ls/clear/exit not found"**: el OSK solo tenía MAYÚSCULAS → los comandos llegaban como `UNAME -A`/`LS`/`CLEAR`/`EXIT` → sh no encontraba `UNAME` (Unix case-sensitive) y los builtins (strcmp exacto) tampoco matcheaban → todo caía a `/bin/sh` → "not found". El script .sh funcionaba porque se lanza desde el FM (path real, sin teclado).

**Fixes**:
1. **Tecla SHIFT (abc/ABC)** en fn-row terminal (col 4, label muestra estado actual). Default **minúsculas** en sesión terminal; FM sigue ABC (comportamiento original intacto).
2. **ENTER** = fn `RUN` renombrada a "ENTER" (ejecuta el comando = submit + vuelve a terminal view). START también ejecuta.
3. **Builtins case-insensitive** (`strcasecmp`): clear/exit/cd ahora funcionan en cualquier casing.
4. **PATH heredado + append** (no sobrescrito): `$PATH:/bin:/sbin:/usr/bin:/usr/sbin:/mnt/sdcard/cubegm` — respeta lo que zhijack exporte.
5. **Cursor solapado**: `text_width()` nuevo helper con métrica real stb (advance por codepoint + scale), fallback 8px. El cursor del prompt ya no tapa la última letra.

Fn-row terminal ahora: `SPACE(2) DEL(2) abc/ABC(1) SYM(3) ENTER(2)` | FM: `SPACE(4) DEL(2) DONE(4)`.

Deploy iteración 5: solo `G:\cubegm\cores\frogshell_libretro.so` (SHA256 1D9EE2B8...). Compilación 0 warnings.

**Pendiente**: prueba física: escribir `uname -a` (minúsculas default), `ls`, `clear`, `exit`, shift para mayúsculas, cursor sin solapar.

---

## Iteración 4 — Teclado OSK rediseñado + auditoría completa (2026-09-08)

**Teclado nuevo (estilo consola Switch/Vita OSK)**:
- Grid de teclas encajonadas de 10 columnas × N filas (letras 4 filas; símbolos terminal 3 filas × 10: `/.,;:!\"'\``, `()[]{}<>|&`, `*=~+@#%^$_`).
- Panel de prompt arriba con **cursor visible** (bloque accent al final del texto).
- **Fila de funciones navegable** (parte del grid): SPACE (4 cols) | DEL (2) | [DONE (4) en FM / SYM (2) + RUN (2) en terminal].
- Hint bar abajo con los botones físicos.
- Navegación: UP/DOWN cicla filas incluyendo fn-row; LEFT/RIGHT por columna (wrap 0-9); A escribe/ejecuta fn.
- SELECT ya no togglea símbolos (ahora es la tecla SYM del grid) — sin colisión con SELECT=paste del FM porque solo aplica en MODE_KEYBOARD.
- Historial (terminal): L1/L1 sincroniza `prompt` con `terminal_input_text()` al navegar.

**Auditoría — bugs encontrados y corregidos**:
1. **A7 — toggle vs flag persistente**: si DEV vino de `/mnt/sdcard/frogui/developer.flag`, el chord OFF ponía latch=0 pero `devmode_is_enabled()` re-leía el flag → mensaje "Disabled" con UI aún DEV. FIX: el chord solo apaga DEV activado por chord/session; el flag persistente SIEMPRE gana (no apagable desde el pad).
2. **A1 — proceso huérfano al desactivar DEV**: `terminal_update` dejaba de correr → zombie hasta deinit. FIX: `process_shutdown()` inmediato al evento Disabled en `input_loop`.
3. **A5 — partial buffer huérfano tras `clear`**: `chunk_append` tenía estado estático que `clear` no reseteaba → primera línea post-clear podía mezclar basura. FIX: buffers `partial/in_ansi` ahora file-scope con `partial_reset()` llamado desde `clear` y `terminal_init`.
4. **`exit` builtin inerte**: ahora pide salir de la vista terminal → `terminal_exit_pending()` → `retro_run` vuelve a MODE_NORMAL (Requisito 17 completo).
5. **Run desde FM no hacía echo del comando**: `terminal_note_launched_path(path)` imprime `$ <path>` real.
6. **`process_poll` simplificado**: drenaje de pipe + EOF/error detección en un solo bucle; cierre del pipe SOLO en EOF confirmado con child_done (evita perder output del último frame).
7. **`process_shutdown`**: grace 200ms + SIGKILL + reap único (bucle raro `while(...)break` eliminado).
8. **`close_extra_fds` simplificado**: cierra todo fd>3 sin exclusiones (llamado tras dup2, las exclusiones eran redundantes); in_pipe/out_pipe del hijo ya cerrados implícitamente.
9. Includes muertos limpiados (`fcntl.h` en devmode.c).

**Verificación de funcionamiento de terminal (flujos auditados)**: submit→echo→builtins/sh→pipe→poll→exit line→render; scrollback reset en output nuevo; historia clamp inferior restaura input; historial visible en teclado; interrupt solo con proceso; `sleep` no bloquea (poll por frame); `yes` acotado por ring 512×128.

Deploy iteración 4: solo `G:\cubegm\cores\frogshell_libretro.so` (93.284 B, SHA256 5C45483D...). Compilación 0 warnings. Backup original del 28/08 intacto en `D:\GitHub\_backup_frogshell_dev\`.

**Pendiente**: prueba física del teclado nuevo (comodidad visual) y del flujo exit/clear. Menú de 11 items y teclado solo validados visualmente en la consola del usuario; resto de consolas pendientes (compilación única validada).

---

## Iteración 3 — Fixes tras segunda prueba física (2026-09-08)

Feedback del usuario:
1. **Menú sin DEV terminaba en "Run"** — CAUSA RAÍZ: `menu_action_name()` mapeaba el bloque DEV por índice sin comprobar `devmode_is_enabled()` → con DEV off el índice 7 caía en el rango DEV. FIX: el mapeo DEV solo aplica si DEV está activo; sin DEV el menú es EXACTAMENTE el original de 8 con Cancel al final.
2. **Keyboard terminal se dibujaba sobre el FM** — FIX: reestructura del render en `draw()`: helpers `draw_keyboard(scale)` y `draw_status(scale)`; la vista terminal (`MODE_TERMINAL` **o** `MODE_KEYBOARD && keyboard_for_terminal`) hace early-return con `clear(0x000000)` — fondo negro SIEMPRE bajo el teclado terminal. `retro_run` también hace `terminal_update()` con el teclado abierto (procesos no se estancan).
3. **`sh` "no such file" con Run with /bin/sh** — CAUSA RAÍZ: argv fantasma `{"sh", path}` → sh intentaba abrir un archivo llamado "sh". FIX: argv correctos `{"/bin/sh","-c",cmd}` y `{"/bin/sh",path}`.
4. **Run vs Run with /bin/sh unificados** — ahora un solo **Run** inteligente: detecta ELF (`\x7fELF`) → `execv`; si no → `/bin/sh <path>` (cubre .sh y cualquier texto ejecutable). Menú DEV = 11 items: 7 normales + Run + Open terminal here + Terminal + Cancel.
5. Menú con rows compactos (30*scale) — cabe en 480px; verificar visualmente en las demás consolas (compilación única validada).

Deploy iteración 3: solo `G:\cubegm\cores\frogshell_libretro.so` (SHA256 635CA17D...). Compilación 0 warnings.

Feedback del usuario en hardware (comprobado en consola real) — iteración 2:
1. **Menú no-DEV correcto**: sin DEV son 8 opciones con **Cancel como 8ª** (Run/Run sh/Open terminal here/Terminal SOLO con DEV). Añadido clamp de `menu_item` al alternar DEV con el menú abierto.
2. **Terminal full-screen**: `MODE_TERMINAL` ahora es una vista INDEPENDIENTE en `draw()` (early-return): fondo negro `0x000000`, nada del file manager debajo, líneas + input line + footer DEV con cwd. B=Files, A=Keyboard, Y=Interrupt (solo con proceso), L1/R1=scrollback.
3. **Chord = TOGGLE**: L1+R1+X+Y ~2s ahora **alterna** DEV ON/OFF (devmode_chord_update devuelve evento +1/-1). ON crea `/tmp/treefrog_developer.flag`; OFF lo borra y limpia el latch (el flag persistente de SD sigue ganando en el próximo arranque). Mensaje en pantalla via status overlay: **"Developer Mode Enabled"/"Developer Mode Disabled"** (el status overlay existente — dura ~2.5s).
4. **PATH del child**: picoarch no exporta PATH utilizable → `setenv("PATH","/bin:/sbin:/usr/bin:/usr/sbin:/mnt/sdcard/cubegm",1)` antes de exec. Sin esto, `ls`/`uname` fallaban con "not found".
5. **Menú DEV 12 items**: row compacto 30*scale px → cabe en 480px de alto.
6. **Script de prueba en SD**: `G:\dev scripts\test.sh` (con espacio en la ruta a propósito) — imprime pwd/uname/date/stdout/stderr/3 ticks con sleep y exit 0. Probable desde Terminal (`sh "dev scripts/test.sh"` tras `cd` a /mnt/sdcard) o desde FM (X → Run with /bin/sh).

Deploy de la iteración 2: solo `G:\cubegm\cores\frogshell_libretro.so` (SHA256 0B3D27CE...). Backup original previo intacto en `D:\GitHub\_backup_frogshell_dev\`.

---

## 1. Repositorios de trabajo (verificados 2026-09-08)

| Repo | Remoto | Branch | HEAD | Estado |
|---|---|---|---|---|
| `D:\GitHub\TreeFrogUI` | github.com/ozkaoz/TreeFrogUI (upstream: tzubertowski/TreeFrogUI) | `main` | `10a5599` | LIMPIO, 0 cambios locales |
| `D:\GitHub\FrogShell` | github.com/ozkaoz/FrogShell (upstream: tzubertowski/FrogShell) | `main` | `7894d88` "refactor: run FrogShell through picoarch" | LIMPIO, 0 cambios locales |

### Procedencia
- Forks creados el 2026-09-08 desde WSL (`Ubuntu-24.04`, usuario `dafunknoise`) con `gh` autenticado como `ozkaoz`, y clonados a `D:\GitHub`.
- El fork TreeFrogUI requirió `--fork-name TreeFrogUI`: la cuenta ya tenía un fork en la misma red (`treefrog-ui-r36sx`); GitHub solo permite un fork por red.
- Submódulo `frogui/` de TreeFrogUI NO inicializado — no hace falta (el plan es NO tocar FrogUI).
- Master `D:\GitHub\AGENTS.md` ya indexa ambos repos.

## 2. Arquitectura FrogShell (hechos verificados)

### Repo mínimo (5 archivos)
`frogshell.c` (todo el core, 486 líneas), `Makefile`, `README.md`, `stb_truetype.h`. `libretro.h` NO está en el repo — se incluye como sibling (`-I../FrogUI`).

### Puntos de integración (frogshell.c, líneas exactas)
- **L43** `Mode` enum: `MODE_NORMAL, MODE_ACTIONS, MODE_CONFIRM, MODE_CONFLICT, MODE_REWRITE, MODE_KEYBOARD, MODE_INFO`
- **L64-78** estado global estático (entries, current, marked, clipboard, mode, menu_item, prompt, keyboard_row/col, status_text)
- **L144-145** `keys_now()`/`pressed()` — input por shm `/tmp/joy_key` (escrita por cubevol), bits default L35 `{7,5,2,3,13,14,10,11,12,15,0,1}` (LEFT,RIGHT,UP,DOWN,A,B,L1,R1,X,Y,SELECT,START), remapeable vía `/mnt/sdcard/frogui/keymap.txt`
- **L298-301** `action_names[]` (menú X, 8 acciones) y `kbd_rows[]` (teclado: `"1234567890","QWERTYUIOP","ASDFGHJKL","ZXCVBNM_-"` + SPACE/DEL/DONE)
- **L309-371** `draw()` — header `"FROGSHELL  %s"` L311, footer botones L326, status overlay L329
- **L373-383** `keyboard_input()` — START acepta, B cancela
- **L408-418** `normal_input()` — A abre, B sube/sale en ROOT, X menú, Y marca, SELECT paste, START teclado, L1/R1 página
- **L420-425** `input_loop()` — **chord START+SELECT = SALIR** (L422-423) → NO reutilizar
- **L457-461** `retro_init()` — load_theme, load_selected_font, load_keymap, open_keys, screen_open, scan
- **L462-466** `retro_deinit()` — screen_close, shmdt
- **L469-475** `retro_run()` — `input_poll → input_loop → status-- → draw → shutdown` (punto de inserción de `process_poll()`/`terminal_update()`, antes de draw)

### Rutas/constantes (L25-28)
`ROOT=/mnt/sdcard`, `DEVICE_FILE=/tmp/tfdevice.env`, `THEME=/mnt/sdcard/cubegm/skin/skin.txt`, `KEYMAP=/mnt/sdcard/frogui/keymap.txt`; fonts en `cubegm/fonts` y `frogui/fonts`; `font=` de `/mnt/sdcard/frogui/settings.txt`.

### Render
Canvas ARGB8888 interno → RGB565 → `video_cb` (libretro). Geometría de `/tmp/tfdevice.env` o env `TF_PANEL_W/H` (default 640x480). `ui_scale()` = h≥720 ? 2 : 1 (L169). Fuente stb_truetype con glyph cache residente + fallback 8x8 (`fontdata8x8` extern).

### Teclado virtual actual — LIMITACIÓN
Solo A-Z mayúsculas, 0-9, `_`, `-` + espacio. Insuficiente para terminal (faltan al menos `/`, `.`, quizá `"`, `|`, `>`, `&`, `*`, `?`, `~`, `=`, `:`). Plan: página de símbolos SOLO en contexto terminal, sin tocar el layout del file manager.

### Makefile (hechos)
- `TOOLCHAIN` default `/home/tomaszz/...` (ruta del upstream) → **siempre override local**.
- `CROSS = $(TOOLCHAIN)/opt/ext-toolchain/bin/mips-mti-linux-gnu-`; `SYSROOT = $(TOOLCHAIN)/mipsel-buildroot-linux-gnu/sysroot`
- CFLAGS: `-mips32r2 -march=mips32r2 -mtune=24kc -mfp32 -mhard-float -mlong-calls -EL --sysroot -G0 -Os -Wall -Wextra` (⚠️ TreeFrogUI `build_all.sh` usa `-mtune=74kc -mdspr2 -Ofast` — mantener flags de cada repo, NO unificar)
- Recipe: `-fPIC -I../FrogUI frogshell.c $(FONT_SOURCE) ... -shared -Wl,--gc-sections -lm` + strip
- `FONT_SOURCE ?= ../picoarch/libpicofe/fonts.c` (provee `fontdata8x8`)
- `TARGET ?= ../sf3000_treefrogui/sdcard/cubegm/cores/frogshell_libretro.so`

### Comando de build local propuesto (WSL, fase 11)
```sh
make -C /mnt/d/GitHub/FrogShell \
  TOOLCHAIN=$HOME/sf3000-work/sf3000toolchain/mipsel-buildroot-linux-gnu_sdk-buildroot \
  FONT_SOURCE=$HOME/sf3000-work/TreeFrogUI_picoarch-fn/libpicofe/fonts.c \
  FROGUI_DIR=$HOME/sf3000-work/FrogUI \
  TARGET=./build/frogshell_libretro.so
```
⚠️ `FROGUI_DIR` NO existe aún: el recipe hardcodea `-I../FrogUI`. Cambio mínimo necesario en la fase de build: parametrizar a `-I$(FROGUI_DIR ?= ../FrogUI)` (default idéntico al upstream). Cambios de Makefile ya justificados de todos modos por las fuentes nuevas (devmode/terminal/process).

### Assets en WSL (NO mover)
- Toolchain: `~/sf3000-work/sf3000toolchain/mipsel-buildroot-linux-gnu_sdk-buildroot` (gcc mips-mti-linux-gnu)
- libretro.h: `~/sf3000-work/FrogUI/libretro.h` (también `D:\GitHub\FrogUI-upstream\libretro.h`)
- fonts.c: `~/sf3000-work/TreeFrogUI_picoarch-fn/libpicofe/fonts.c`

## 3. Arquitectura TreeFrogUI (hechos verificados)

### Dispositivos soportados (install.md: "seven devices", build_release.sh L79-92)
| Familia build | Dispositivos | Panel | Toolchain |
|---|---|---|---|
| r36sx | R36SX v2.6, R36SX v2.7, R36HD | 640x480 4:3, rot 0, fbwrite | mismo |
| sf3000 | SF3000 | 854x480 16:9, rot 90, dispframe | mismo |
| sf3500 | SF3000 HD (HDMI), SF3100, SF3500 | 854x480 16:9, rot 90 | mismo |
| gb350 | GB350 | 640x480 4:3 | mismo |

Un solo toolchain MIPS32r2 hard-float EL, kernel 4.4.186, FAT32 **sin symlinks** (contrato). NO soportados: SF3000 V3, SF3000 Pro.

### Cadena de boot y launch
```
stock → icube → rkgame → libemu_tfhijack.so (fork + execl /bin/sh zhijack.sh)
      → zhijack.sh → picoarch frogui_libretro.so (bucle)
      → FrogUI escribe /tmp/frogui_launch.txt:
           2 líneas = core libretro  → picoarch <core> <rom>
           3 líneas = standalone    → binario directo
```
- FrogShell YA está registrada como app en FrogUI y se lanza como core: `picoarch /mnt/sdcard/cubegm/cores/frogshell_libretro.so <rom>`. **No hay nada que registrar para Developer Mode.**
- TreeFrogUI NO compila FrogShell en CI: usa `assets/frogshell_libretro.so` in-tree (78.516 bytes). Local: `make -C $FROGSHELL TARGET=...` si existe checkout hermano (build_release.sh L38, L122-124).
- `deploy_device.sh` L300-302 aún referencia el payload standalone ANTIGUO (`cubegm/frogshell`) — stale desde que FrogShell pasó a core; no tocar en este proyecto.

### Keybindings (verificados — NO crear conflictos)
- **FrogShell: START+SELECT = SALIR** (exit del core → retorno a FrogUI)
- TreeFrogUI global (picoarch in-game): SELECT+START = menú; SELECT+R1 = fast-forward; SELECT+L1 = screenshot; SELECT+L2/R2 = save/load state; SELECT+B = rewind
- **L1+R1+X+Y = LIBRE** → elegido como chord DEV (~2s)
- ⚠️ Problema identificado: en FrogShell normal, X abre menú de acciones, Y marca, L1/R1 paginan. El chord DEV debe detectarse en `input_loop()` ANTES de despachar a los handlers, y SUPRIMIR el manejo individual mientras los 4 botones estén pulsados simultáneamente.

### Sistema
- **`/bin/sh` = BusyBox ash, CONFIRMADO en todos los dispositivos** (el boot depende de él: tfhijack, zhijack.sh, tfupdate.sh, usb_mode.sh)
- `/tmp/tfdevice.env`: TF_DEVICE, TF_PANEL_W/H, TF_UI_SCALE=150, TF_ASPECT_*, TF_ROTATE, TF_PRESENT, TF_DRIVER
- Input: shm `/tmp/joy_key` (cubevol). Volumen: GPIO vía cubevol (fuera de joy_key)
- Logging opt-in: `log.txt` en raíz de SD

### Layout SD (/mnt/sdcard ↔ G:\)
```
MD/dummy.md                 ← rom dummy del autorun
cubegm/                     ← picoarch(_hi), zhijack.sh, driver_*.so, skin/skin.txt,
                               cores/*.so (frogui_libretro.so, frogshell_libretro.so), bios/, version.txt
frogui/                     ← settings.txt, keymap.txt, core_overrides.txt, fonts/, lang/,
                               system-icons/, icon-packs/, theme-packs/, wallpapers/
roms/ (por sistema, .res/)  screenshots/  log.txt  update.zip
```
- `frogui/` = dir de config compartida del sistema → **el persistent flag encaja ahí**.
- ⚠️ `G:\` NO estaba montada al hacer este baseline — verificar estructura real al insertar la SD (esperado `G:\cubegm\cores\frogshell_libretro.so`).

## 4. Decisiones de diseño ya tomadas (NO re-debatir)

| Tema | Decisión |
|---|---|
| Persistent flag | `/mnt/sdcard/frogui/developer.flag` (contenido irrelevante; NO incluir en releases) |
| Session flag | `/tmp/treefrog_developer.flag` |
| `developer_enabled` | `file_exists(persistent) \|\| file_exists(session)` |
| Chord temporal | **L1+R1+X+Y ≥2s** (libre; sin conflicto con START+SELECT ni SELECT+*) |
| Capability gate | `devmode_is_enabled()` comprobado en TODA entrada al process runner y toda acción DEV |
| Modo normal | Sin DEV: cero cambios visibles, de keybindings o de rendimiento |
| Contextos Files/Terminal | `MODE_TERMINAL` añadido al enum `Mode` existente (no un enum AppView paralelo) |
| Acceso a Terminal | Entrada "Terminal" condicional en el menú X (solo con DEV); acciones de archivo "Run", "Run with /bin/sh", "Open terminal here" |
| Teclado | Reutilizar `MODE_KEYBOARD` con página de símbolos solo-terminal |
| Process runner | fork/exec/pipe/dup2/fcntl O_NONBLOCK/waitpid WNOHANG/kill a `-pgid`; SIN system()/popen(); child: chdir(cwd), process group propio, FDs extra cerrados |
| Comandos | `/bin/sh -c <cmd>` (ash confirmado); `.sh` → `sh <path>`; binarios → execv |
| Built-ins | `cd`, `clear`, `exit` internos |
| Buffers | history 32 cmds, scrollback 512 líneas, ring buffer (RAM acotada) |
| ANSI | strip/ignorar en v1 |
| Cierre | retro_deinit: kill(-pgid) → waitpid reap → cerrar pipes → liberar buffers |
| TreeFrogUI | Cambios IDEALMENTE CERO (chord y flags resuelven dentro de FrogShell) |
| Archivos nuevos | `devmode.c/.h`, `process.c/.h`, `terminal.c/.h` + ediciones puntuales en `frogshell.c` y `Makefile` |

## 5. Fases (estado)

| Fase | Estado |
|---|---|
| 0. Inspección y baseline | ✅ COMPLETADA (este documento) |
| 1. devmode (flags + chord, sin tocar UI normal) | ✅ COMPLETADA |
| 2. Indicador DEV + navegación Terminal | ✅ COMPLETADA |
| 3. Teclado extendido terminal | ✅ COMPLETADA (página símbolos via SELECT) |
| 4. Terminal básica | ✅ COMPLETADA |
| 5. Process runner no bloqueante | ✅ COMPLETADA |
| 6. stdout/stderr/exit code | ✅ COMPLETADA (pipe compartido, [exit N]/[terminated SIGn]) |
| 7. cd/clear (exit = sin builtin: usar `exit` del sh no aplica; cerrar con START+SELECT) | ✅ COMPLETADA |
| 8. history + scrollback | ✅ COMPLETADA (L1/R1 scroll; historial con L1/R1 en teclado terminal) |
| 9. Acciones DEV en File Manager | ✅ COMPLETADA (Run / Run with /bin/sh / Open terminal here / Terminal) |
| 10. Integración TreeFrogUI | ✅ CERO cambios en TreeFrogUI (chord+flags resueltos dentro de FrogShell) |
| 11. Compilación | ✅ COMPLETADA — 0 warnings, 0 errores (incl. fixes de 3 warnings preexistentes del upstream) |
| 12. git diff review | ✅ Verificado (Makefile +7, frogshell.c +171/-18, 6 archivos nuevos) |
| 13. Backup core de G:\ | ✅ `D:\GitHub\_backup_frogshell_dev\frogshell_libretro.so.orig` (SHA256 3D7A3D89..., verificado) |
| 14. Deploy mínimo G:\ | ✅ SOLO `G:\cubegm\cores\frogshell_libretro.so` reemplazado (SHA256 19EB9587... verificado) |
| 15. Pruebas físicas | ⏳ PENDIENTES (ver §6) |

## 5b. Arquitectura final implementada (resumen)

**Archivos nuevos** (en `D:\GitHub\FrogShell\`):
- `devmode.c/.h` — gate: `devmode_is_enabled()` (flag persistente `/mnt/sdcard/frogui/developer.flag` OR `/tmp/treefrog_developer.flag` OR latch del chord); `devmode_chord_update()` detecta L1+R1+X+Y ≥2000ms con `now_ms()` y crea el session flag.
- `process.c/.h` — runner: `fork()`+`setpgid(0,0)`, `dup2` stdin/stdout/stderr a pipes, `close_extra_fds()` (cierra todo fd>2 heredado del frontend), `chdir(cwd)` antes de exec, `execvp`/`execl /bin/sh -c`; parent: `fcntl O_NONBLOCK` en el pipe de lectura, `waitpid(WNOHANG)`, `kill(-pgid)` para SIGINT/SIGTERM/SIGKILL; API: `process_start_command/script/executable`, `process_poll(cb)`, `process_interrupt/terminate/shutdown`, `process_is_running`, `process_exit_code` (128+sig para señales).
- `terminal.c/.h` — terminal: ring buffer 512 líneas × 128 cols, historia 32 cmds, input 256; ANSI-stripper en `chunk_append` (ESC[...@-~ descartado, CR ignorado, tab→espacio, no-imprimibles descartados); builtins `cd` (con `..` y rutas absolutas/relativas al cwd persistente), `clear`; `terminal_submit()` con capability gate; exit line `[exit N]`/`[terminated SIGn]` tras drenar el pipe.

**Ediciones en `frogshell.c`** (+171/−18): enum `MODE_TERMINAL`; estado `keyboard_symbols`/`keyboard_for_terminal`; `kbd_sym_rows[]` (4 filas de símbolos POSIX, fila 0 con espacio); título "FROGSHELL DEV"; menú X extendido dinámico (`menu_action_count()/menu_action_name()` — solo muestra Run/Run sh/Open terminal here/Terminal si DEV); render de MODE_TERMINAL (líneas + prompt + footer con cwd; Y=Interrupt solo cuando hay proceso); `terminal_input()` (B=Files, A=teclado, Y=interrupt, L1/R1=scrollback); `keyboard_input()` con rama terminal (SELECT=toggle símbolos, START=ejecutar, L1/R1=historia, B=volver sin perder input); `input_loop()` con chord DEV prioritario que suprime L1/R1/X/Y mientras se mantiene; `retro_init` (signal SIGTERM→die_signal + devmode_refresh + terminal_init), `retro_deinit` (terminal_free ANTES de screen_close), `retro_run` (terminal_update solo si DEV && MODE_TERMINAL).
Fixes de warnings preexistentes: `scan()` L246 y `normal_input()` L523 partidas en líneas; `die_signal` ahora instalada como handler SIGTERM (Requisito 26 mejorado).

**Makefile** (+7): `SOURCES := frogshell.c devmode.c process.c terminal.c`; `FROGUI_DIR ?= ../FrogUI` parametriza `-I` (default = ruta upstream, compatible).

**Modo normal intacto (verificado por diseño)**: sin DEV — `devmode_is_enabled()` es 0 → título normal, menú de 8 acciones idéntico, teclado solo letras, `terminal_update()` no corre, sin procesos. Los cambios al input solo actúan con el chord activo o DEV. Cero overhead cuando no hay proceso: `process_poll` solo lee si hay pipe abierto.

## 6. Pruebas físicas pendientes (checklist para la SD)

**Activación**: mantener L1+R1+X+Y ~2s → título cambia a "FROGSHELL DEV" (o crear `G:\frogui\developer.flag` — vacío — antes de arrancar).

**Regresión modo normal** (sin DEV, primero): navegación, X menú 8 acciones, copiar/pegar/renombrar/borrar/carpeta/info, teclado rename/new folder, START+SELECT sale.

**Terminal (con DEV)**: X → Terminal → A (teclado) → comandos:
- `pwd`, `ls`, `ls -la`, `echo hello`, `uname -a`, `cat install.md`
- `sleep 5` → UI responde (L1/R1 scroll), no congela
- `sh -c 'echo out; echo err >&2; exit 37'` → `[exit 37]`
- `yes` → buffer acotado, Y=Interrupt → `[terminated SIG2]`, sin zombie
- `sleep 60` + Y → interrumpe
- `cd ..`, `cd roms`, `clear`
- Historial: A→teclado, L1/R1 recorren comandos
- Paths con espacios: crear script en SD y `sh "dev scripts/test.sh"` / Run with /bin/sh desde FM sobre un `.sh` con espacios en el nombre

**File Manager DEV**: seleccionar `.sh` → X → "Run with /bin/sh"; carpeta → "Open terminal here"; binario → "Run".

## 6. Pendientes de verificación

- [x] Estructura real de `G:\` confirmada: `G:\cubegm\cores\frogshell_libretro.so` existía (78.516 bytes, 28/08) — reemplazado por el core DEV (92.408 bytes) con backup previo.
- [x] Compilación local real con el cambio `FROGUI_DIR` — funciona, 0 warnings.
- [ ] Pruebas físicas por dispositivo (compilación validada ≠ prueba funcional ≠ prueba física). Hasta ahora: NINGÚN dispositivo probado físicamente. Compilación validada para las 7 consolas (toolchain único MIPS32r2).
- [ ] `/bin/sh` en hardware: confirmado por diseño de boot en todos los dispositivos; validar con `uname -a` en primera prueba física.

## 7. Restricciones activas (recordatorio)

1. No romper TreeFrogUI / FrogShell; modo normal intacto al 100%.
2. Todo el runtime nuevo en C POSIX. Sin SDL/ncurses/Python/Lua/bash-dependency.
3. Diffs mínimos y auditables; no refactorizar; no cambiar keybindings, rutas ni nombres públicos existentes.
4. Toolchain y flags EXACTOS de cada Makefile.
5. `G:\` = entorno real preservado: solo copiar archivos del proyecto, con backup previo de lo reemplazado.
6. Sin `git push` ni PR sin autorización expresa; commits locales solo si el flujo lo requiere.
