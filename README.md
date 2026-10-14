# mupen64-360 (fork actualizado)

Emulador de **Nintendo 64 para Xbox 360**. Está basado en [mupen64-360 de GliGli](https://github.com/gligli/mupen64-360), que a su vez es un port de Wii64/Mupen64. Este fork tiene tres objetivos:

- compilarlo con la **toolchain actual de libxenon** (GCC 16),
- corregir fallos que afectan a consolas recientes, sobre todo las **Winchester**,
- que se pueda usar con **ABadAvatar / BadUpdate** (sin chip ni soldadura) casi sin tocar XeLL.

> ✅ **Probado en una Xbox 360 Winchester** (ABadAvatar → FreeMyXe → XeLL Reloaded v0.993, disco USB de 1 TB en FAT32): arranca, da video y corre Super Mario 64.
>
> ⚠️ **En Winchester la consola se sobrecalienta y puede encender la luz roja de advertencia.** Lee la sección de [sobrecalentamiento](#️-advertencia-sobrecalentamiento-en-winchester) antes de usarlo.
>
> Proyecto en desarrollo. Si algo falla, abre un *issue* con tu modelo de consola, la placa y lo que pasó.

---

## Qué cambia respecto al original

| Cambio | Dónde | Por qué |
|---|---|---|
| Carpeta `mupen64-360/` detectada automáticamente en cualquier unidad (USB, HDD…) | `source/main/main.cpp` | Antes solo buscaba en `uda0:`. Si la USB montaba con otro nombre, no se cargaban la configuración, las partidas ni el fondo |
| Arreglo de un **puntero nulo** en el driver USB EHCI | `patches/libxenon/` | Al fallar una transferencia USB, el emulador se colgaba (pantalla de crash) |
| La recuperación de USB **reseteaba el puerto 0** en vez del puerto real | `patches/libxenon/` | Si la USB estaba en otro puerto, al reconectarse no se recuperaba |
| Build reproducible: dependencias con commit fijo y compilación automática en GitHub Actions | `scripts/`, `.github/workflows/` | Cualquiera puede descargar el binario compilado desde la pestaña **Actions** |

---

## ⚠️ Advertencia: sobrecalentamiento en Winchester

En la Winchester de prueba la consola **se calienta de más** y puede encender la **luz roja de advertencia**. No es el "aro rojo de la muerte" (falla de hardware) de las Xbox 360 originales: es el aviso de temperatura alta de la consola.

**Por qué pasa:** el emulador corre sobre XeLL/libxenon, que se diseñaron para las placas antiguas y no están bien adaptados a la Winchester. A diferencia del sistema oficial, libxenon no administra la temperatura: solo pone los ventiladores en un modo automático básico, no baja el consumo cuando el procesador está libre, y el emulador mantiene la CPU al 100 %.

**Recomendaciones mientras se corrige:**
- Juega en **sesiones cortas** y deja que la consola se enfríe entre partidas.
- Deja **espacio libre** alrededor de las rejillas, sin muebles cerrados ni cosas encima.
- **No dejes la consola encendida en el menú** del emulador si no estás jugando.
- **Si aparece la luz roja, apaga la consola** de inmediato y espera a que se enfríe antes de volver a usarla.

Úsalo bajo tu propio riesgo. Controlar los ventiladores desde el emulador está en la lista de pendientes.

---

## Cómo usarlo con ABadAvatar (consola sin chip, dashboard 17559)

### Necesitas
- Una Xbox 360 con **dashboard 2.0.17559.0** (cualquier modelo, incluido Winchester).
- Una USB formateada en **FAT32**.
- [ABadAvatar](https://github.com/shutterbug2000/ABadAvatar) y [FreeMyXe](https://tangled.org/invoxiplaygames.uk/FreeMyXe) (descárgalos de sus páginas oficiales).
- **XeLL Reloaded**: descarga la [v0.993](https://github.com/Free60Project/xell-reloaded/releases) y copia **solo** `xell-2f.bin` dentro de `BadUpdatePayload/`. No uses los `_cygnos_demon` (son para consolas con ese chip).
- El paquete de este emulador. Lo descargas desde **Actions → la última ejecución en verde → artifact `mupen64-360-usb`**, o desde *Releases* cuando haya una.

### Prepara la USB
Copia todo en la raíz de la USB. Debe quedar así:

```
USB:/
├── BadUpdatePayload/      ← de ABadAvatar + FreeMyXe (renombrado a default.xex) + xell-2f.bin
├── Content/               ← de ABadAvatar
├── xenon.elf              ← este emulador (XeLL lo arranca solo)
└── mupen64-360/
    ├── bg.png             ← fondo del menú (puedes cambiarlo)
    ├── mupen64plus.ini    ← base de datos de juegos
    ├── RiceVideoLinux.ini ← ajustes de video por juego
    ├── roms/              ← pon aquí tus ROMs (.z64 .n64 .v64 .zip)
    ├── saves/
    └── sstates/
```

### Arranca
1. Desconecta la consola de internet y desactiva el inicio de sesión automático del perfil.
2. Conecta la USB y enciende la consola.
3. ABadAvatar lanza el exploit y FreeMyXe aparece. Elige **"Launch XeLL instead"**.
   ⚠️ Tiene que ser **FreeMyXe**: XeUnshackle no arranca XeLL.
4. XeLL encuentra `xenon.elf` en la USB y abre el emulador **solo**. No tienes que hacer nada en XeLL.

El exploit no es permanente: hay que repetir esto cada vez que enciendas la consola.

### Controles del menú
- **A**: abrir carpeta o juego
- **B**: subir de carpeta
- **Back**: cambiar de unidad (en juego: alternar el límite de velocidad)
- **Botón Guía**: salir del juego

Usa solo ROMs de cartuchos que tengas.

---

## Compilar tú mismo

GitHub Actions ya lo compila en cada *push* (`.github/workflows/build.yml`). Para hacerlo en Linux:

```bash
# 1. Toolchain (binutils + GCC + newlib para Xenon), tarda ~1 h
git clone https://github.com/Free60Project/libxenon
cd libxenon/toolchain && sudo ./build-xenon-toolchain toolchain && cd ../..
export DEVKITXENON=/usr/local/xenon
export PATH="$PATH:$DEVKITXENON/bin:$DEVKITXENON/usr/bin"

# 2. Librerías con los parches de este repo
scripts/build-deps.sh

# 3. Emulador y carpeta lista para la USB
make -j$(nproc)
scripts/package.sh mupen64-360.elf32 dist
```

### Estructura
- `source/`: emulador (núcleo mupen64plus, dynarec PowerPC en `r4300/ppc`, video `Rice_GX_Xenos`, audio, controles)
- `patches/libxenon/`: parches sobre libxenon. Se aplican solos en `build-deps.sh`
- `scripts/`: compilación de dependencias y empaquetado
- `files/`: fondos, `.ini` y shaders

---

## Estado y pendientes
- [x] Compilar con libxenon / GCC actuales (GCC 16.2)
- [x] Arreglos del driver USB (probado en Winchester)
- [x] Video en Winchester (probado)
- [ ] Detección de USBs sin tabla de particiones: se quitó porque en un disco de 1 TB montaba mal la unidad; hay que rehacerla con una validación más estricta
- [ ] **Controlar ventiladores y temperatura en Winchester** (sobrecalentamiento con luz roja de advertencia)
- [ ] Probar compatibilidad juego por juego
- [ ] Versión `.xex` para lanzarla desde el dashboard o Aurora sin XeLL (en investigación, con OpenXeChain)

## Créditos
Equipos de Wii64 / Mupen64 · GliGli (port a Xbox 360) · Ced2911 (librería ZLX) · Razkar (fondos) · Free60 / libxenon · Grimdoomer (BadUpdate) · shutterbug2000 (ABadAvatar) · InvoxiPlayGames (FreeMyXe).

Licencia: GPLv2, igual que el proyecto original.
