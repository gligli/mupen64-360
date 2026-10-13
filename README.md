# mupen64-360 (fork actualizado)

Emulador de **Nintendo 64 para Xbox 360**. Está basado en [mupen64-360 de GliGli](https://github.com/gligli/mupen64-360), que a su vez es un port de Wii64/Mupen64. Este fork tiene tres objetivos:

- compilarlo con la **toolchain actual de libxenon** (GCC 16),
- corregir fallos que afectan a consolas recientes, sobre todo las **Winchester**,
- que se pueda usar con **ABadAvatar / BadUpdate** (sin chip ni soldadura) casi sin tocar XeLL.

> ⚠️ Proyecto en desarrollo. Cada cambio se prueba en una consola real. Si algo falla, abre un *issue* con tu modelo de consola, la placa y lo que pasó.

---

## Qué cambia respecto al original

| Cambio | Dónde | Por qué |
|---|---|---|
| Carpeta `mupen64-360/` detectada automáticamente en cualquier unidad (USB, HDD…) | `source/main/main.cpp` | Antes solo buscaba en `uda0:`. Si la USB montaba con otro nombre, no se cargaban la configuración, las partidas ni el fondo |
| Detección de USBs **sin tabla de particiones** ("superfloppy") | `patches/zlx/` | El sector de arranque FAT se confundía con un MBR y la USB no montaba o montaba mal |
| Arreglo de un **puntero nulo** en el driver USB EHCI | `patches/libxenon/` | Al fallar una transferencia USB, el emulador se colgaba (pantalla de crash) |
| La recuperación de USB **reseteaba el puerto 0** en vez del puerto real | `patches/libxenon/` | Si la USB estaba en otro puerto, al reconectarse no se recuperaba |
| Build reproducible: dependencias con commit fijo y compilación automática en GitHub Actions | `scripts/`, `.github/workflows/` | Cualquiera puede descargar el binario compilado desde la pestaña **Actions** |

---

## Cómo usarlo con ABadAvatar (consola sin chip, dashboard 17559)

### Necesitas
- Una Xbox 360 con **dashboard 2.0.17559.0** (cualquier modelo, incluido Winchester).
- Una USB formateada en **FAT32**.
- [ABadAvatar](https://github.com/shutterbug2000/ABadAvatar) y [FreeMyXe](https://tangled.org/invoxiplaygames.uk/FreeMyXe) (descárgalos de sus páginas oficiales).
- Un **XeLL Reloaded** que funcione en tu consola. En Winchester usa una versión compatible con esa placa.
- El paquete de este emulador. Lo descargas desde **Actions → la última ejecución en verde → artifact `mupen64-360-usb`**, o desde *Releases* cuando haya una.

### Prepara la USB
Copia todo en la raíz de la USB. Debe quedar así:

```
USB:/
├── BadUpdatePayload/      ← de ABadAvatar, con FreeMyXe dentro
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
3. ABadAvatar lanza el exploit y FreeMyXe aparece. Desde FreeMyXe, arranca **XeLL**.
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
- `patches/libxenon/`, `patches/zlx/`: parches sobre las librerías externas. Se aplican solos en `build-deps.sh`
- `scripts/`: compilación de dependencias y empaquetado
- `files/`: fondos, `.ini` y shaders

---

## Estado y pendientes
- [x] Compilar con libxenon / GCC actuales (GCC 16.2)
- [ ] Arreglos de USB y particiones (escritos, falta probar en consola)
- [ ] Confirmar video en Winchester (se sabe que la 0.993 beta2 de otro fork lo perdía; este fork parte de la 0.992)
- [ ] Probar compatibilidad juego por juego
- [ ] Versión `.xex` para lanzarla desde el dashboard o Aurora sin XeLL (en investigación, con OpenXeChain)

## Créditos
Equipos de Wii64 / Mupen64 · GliGli (port a Xbox 360) · Ced2911 (librería ZLX) · Razkar (fondos) · Free60 / libxenon · Grimdoomer (BadUpdate) · shutterbug2000 (ABadAvatar) · InvoxiPlayGames (FreeMyXe).

Licencia: GPLv2, igual que el proyecto original.
