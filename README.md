# SM64 Vice City Native

Mod para **GTA Vice City clásico (PC 1.0)** que permite jugar como Mario de Super Mario 64, utilizando sus físicas, movimientos, animaciones y modelo originales.

A diferencia de un reemplazo de personaje tradicional, este mod utiliza [libsm64](https://github.com/libsm64/libsm64) para ejecutar la lógica de Mario dentro de Vice City. Esto permite conservar movimientos como el triple salto, salto largo, wall kick, ground pound y las físicas de natación originales de SM64.

El mod funciona mediante un plugin ASI de 32 bits. No necesita ejecutar Super Mario 64 por separado ni utilizar un emulador.

## Características

- Modelo y animaciones originales de Super Mario 64.
- Físicas y movimientos mediante libsm64.
- Movimiento relativo a la cámara de Vice City.
- Saltos simples, dobles y triples.
- Long jump, wall kick y ground pound.
- Natación y movimiento bajo el agua.
- Interacción con las colisiones del mapa de Vice City.
- Colisiones con vehículos cercanos.
- Posibilidad de entrar y conducir vehículos.
- Integración con el sistema de armas y daño de GTA.
- Animación de muerte de Mario y transición al hospital.
- HUD de Super Mario 64 adaptado a Vice City.
- Sprint adicional.
- Activación y desactivación de Mario durante el juego.

Las físicas de Mario funcionan a 30 Hz, como en SM64, mientras que su geometría se interpola para renderizarse a 60 FPS.

## Controles

| Tecla | Acción |
|---|---|
| WASD | Movimiento |
| Shift | Sprint |
| Espacio | Saltar (A) |
| Ctrl / Z | Agacharse (Z) |
| Botón de ataque | Acción B, cuando Mario está desarmado |
| Correr + Z + Espacio | Salto largo |
| Espacio + Z en el aire | Ground Pound |
| Espacio al tocar una pared | Wall Kick |
| Saltos consecutivos al aterrizar | Salto doble / triple |
| Espacio en el agua | Nadar |
| F / Enter | Entrar o salir de vehículos |
| M | Activar / desactivar Mario |

Las armas y los vehículos mantienen los controles habituales de Vice City.

## Cómo funciona

El mod utiliza **libsm64** para controlar a Mario y **Plugin-SDK** para comunicarse con el motor de Vice City.

El plugin obtiene las superficies de colisión cercanas desde `CWorld` y las convierte al formato utilizado por libsm64. De esta manera, Mario puede caminar, correr, saltar y chocar con la geometría del mapa utilizando sus físicas originales.

También se convierten las coordenadas entre ambos juegos:

```cpp
// GTA (x, y, z) -> SM64 (x, z, -y)
sm64_pos = { gta_x / 0.0085f,
             gta_z / 0.0085f,
            -gta_y / 0.0085f };
```

Se utiliza un origen horizontal local para la conversión de posiciones.

Para el renderizado, libsm64 genera los triángulos animados de Mario y el plugin los dibuja mediante RenderWare y Direct3D 8.

El modelo, las texturas, los vértices y las paletas se obtienen de la ROM original de Super Mario 64 proporcionada por el usuario.

Vice City sigue encargándose de la cámara, los vehículos, NPC, armas, dinero y demás sistemas del juego.

Todo se ejecuta dentro del mismo proceso de GTA Vice City.

## Requisitos

Para compilar el mod necesitas:

- Windows.
- Python 3.
- Git.
- CMake.
- Visual Studio 2022 Build Tools con soporte C++ x86.
- Windows SDK.
- w64devkit x86 para compilar libsm64.
- Una ROM US de Super Mario 64 compatible.
- GTA Vice City clásico 1.0 para PC.

El mod está desarrollado para `GAME_10EN`. No es compatible directamente con Vice City: Definitive Edition ni con otras versiones del ejecutable.

### ROM compatible

Se requiere la ROM estadounidense de Super Mario 64 en formato big endian de 8 MiB.

SHA1:

```text
9bef1128717f958171a4afac3ed78ee2bb4e86ce
```

La ROM no está incluida en el repositorio.

## Compilación

Desde la carpeta del proyecto, ejecuta:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

Después, prepara libsm64 utilizando tu propia ROM:

```powershell
.\package\physics.exe 'C:\ruta\a\Super Mario 64 (U) [!].z64'
```

Los scripts se encargan de descargar y preparar las dependencias necesarias.

Para instalar el mod en una copia compatible de Vice City:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/install.ps1 `
  -Destination 'C:\ruta\a\GTA Vice City' `
  -Rom 'C:\ruta\a\Super Mario 64 (U) [!].z64'
```

El instalador es independiente de Universal Modder. Requiere Python 3 en el PATH, los dos binarios compilados en `package/` y Vice City cerrado. `-Destination` y `-Rom` son obligatorios; el proyecto puede moverse sin cambiar el script.

Antes de instalar, conserva las verificaciones del ejecutable, `GAME_10EN`, los bytes de los hooks y la ROM mediante `scripts/inspect.py`. También comprueba que ambos binarios sean DLL x86. Si falta algún archivo o falla una comprobación, no reemplaza archivos del juego.

Busca las partidas en `GTA Vice City User Files` dentro de la carpeta Documentos que informa Windows, incluyendo las redirecciones de Documentos. Debe encontrar `gta_vc.set` o una partida `GTAVCsf1.b` a `GTAVCsf8.b`; si no puede identificar la carpeta, se detiene. Para una ubicación personalizada, añade `-Saves 'ruta a GTA Vice City User Files'`. No crea ni modifica partidas.

Antes de sobrescribir nada, crea un respaldo en `<Destination>/SM64VCNative-backups/<fecha-identificador>/`:

- `previous/`: los ASI, DLL y ROM anteriores que vaya a reemplazar.
- `saves/`: una copia completa de la carpeta de partidas, con sus subcarpetas.
- `install/`: los archivos preparados para esta instalación.
- `manifest.json`: rutas, validación y hashes SHA256.

Verifica las copias por hash y vuelve a comprobar que el juego esté cerrado antes de instalar. Si una copia de instalación falla, intenta restaurar los archivos anteriores. Al finalizar muestra los archivos instalados y la ubicación del respaldo. Guarda los respaldos en privado: pueden contener tu ROM y partidas.

### Pruebas del instalador

Después de compilar, ejecuta las pruebas con tu ejecutable y ROM compatibles:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-install.ps1 `
  -GameExe 'ruta a gta-vc.exe' `
  -Rom 'ruta a Super Mario 64 (U) [!].z64'
```

Las pruebas utilizan el verificador real y escriben únicamente en una carpeta temporal. Comprueban instalaciones nuevas, actualizaciones, respaldos, rechazo de archivos modificados o ausentes, juego abierto y restauración tras fallos de copia simulados. La prueba de detección automática lee las partidas reales para copiarlas al respaldo temporal y verifica que sus hashes no cambien. No ejecutan el juego ni instalan en tu instalación real. Las carpetas temporales se conservan para inspección.

Los archivos principales del mod son:

```text
SM64VCNative.asi
sm64.dll
sm64.us.z64
```

Se necesita un cargador ASI compatible. Se recomienda utilizar una copia separada de Vice City para probar el mod.

## Compatibilidad

Actualmente el plugin está preparado para un ejecutable específico de GTA Vice City 1.0.

SHA256 del ejecutable compatible:

```text
04e4db72629eaa786fdd182ac224f4fd6d68806f5f4fe5c1fb5f756dc4da11d7
```

El plugin verifica la versión del ejecutable y las ubicaciones de los hooks antes de aplicarlos.

No se garantiza el funcionamiento con ejecutables modificados u otras versiones del juego.

## Estado actual

El mod ya es jugable. Mario puede moverse por Vice City utilizando las físicas y animaciones originales de SM64, interactuar con el escenario y utilizar varios sistemas de GTA.

Todavía quedan cosas por mejorar:

- Ajustar la posición de las armas en las manos de Mario.
- Revisar las animaciones de apuntado.
- Mejorar las interacciones con determinados vehículos.
- Corregir posibles problemas de colisión en espacios estrechos.
- Comprobar la compatibilidad con más interiores y misiones.
- Pulir las transiciones entre las acciones de Mario y los sistemas de GTA.

No se han probado todas las misiones ni todas las situaciones posibles del juego.

## Dependencias y créditos

Este proyecto utiliza:

- [libsm64](https://github.com/libsm64/libsm64) — Física, movimientos y animaciones de Mario.
- [n64decomp/sm64](https://github.com/n64decomp/sm64) — Proyecto de decompilación de Super Mario 64.
- [Plugin-SDK](https://github.com/DK22Pac/plugin-sdk) — Integración con GTA Vice City.
- [MinHook](https://github.com/TsudaKageyu/minhook) — Hooks nativos.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) — Cargador de plugins ASI.
- [SilentPatch](https://github.com/CookiePLMonster/SilentPatch) — Correcciones de compatibilidad.
- [Framerate Vigilante](https://github.com/shaneomac1337/FramerateVigilante) — Correcciones relacionadas con el framerate.
- [Universal Modder](https://github.com/rehan-remade/universal-modder) — Herramientas utilizadas durante el desarrollo.

Desarrollado con asistencia de OpenAI Codex.

## Aviso

Este es un proyecto de modding no oficial. Super Mario 64 y Grand Theft Auto: Vice City pertenecen a sus respectivos propietarios.

El repositorio contiene el código fuente de la integración. No incluye el juego, la ROM de Super Mario 64 ni recursos originales extraídos de ambos títulos.

Para utilizarlo se necesitan copias propias compatibles de los juegos.
