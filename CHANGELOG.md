# Cambios

## v0.992-wf2 (rama winchester-fixes)

✅ **Probado en una Xbox 360 Winchester** (ABadAvatar → FreeMyXe → XeLL Reloaded v0.993, disco USB de 1 TB en FAT32): menú con video, lista de archivos correcta y Super Mario 64 jugable.

- Se quitó el parche de detección "superfloppy" de wf1: en el disco de 1 TB hacía que `uda0:` se montara en el sector equivocado (nombres de archivo basura, sin fondo).
- Se mantienen la carpeta automática, los arreglos del driver USB y la compilación con GCC 16.

**Problema conocido:** en Winchester la consola se **sobrecalienta** y puede encender la **luz roja de advertencia** (aviso de temperatura, no el "aro rojo de la muerte"). XeLL/libxenon no están bien adaptados a esta placa: no administran la temperatura como el sistema oficial y el emulador lleva la CPU al 100 %. Juega en sesiones cortas, con buena ventilación, y apaga la consola si aparece la luz roja.

SHA-256 de `xenon.elf`: `0c8dd257f1e8eefed30983f3c71c798d9e0efb2492419705782f18087b751419`

## v0.992-wf1

❌ En la Winchester con disco de 1 TB la lista de archivos salía con basura (corregido en wf2).

Compilado desde la rama `winchester-fixes` con GCC 16.2 y la libxenon actual.

### Cambios
- La carpeta `mupen64-360/` se busca en cualquier unidad, no solo en `uda0:`
- Las USB sin tabla de particiones ("superfloppy") ahora montan bien
- libxenon/EHCI: arreglado un cuelgue por puntero nulo cuando falla una transferencia USB
- libxenon/EHCI: la recuperación de USB resetea el puerto correcto
- Compila con GCC moderno (C23 → gnu17)

### Instalación
1. Descomprime `mupen64-360-usb.zip` en la **raíz** de una USB FAT32.
2. En la misma USB, copia los archivos de **ABadAvatar** y mete **FreeMyXe** dentro de `BadUpdatePayload/`.
3. Pon tus ROMs en `mupen64-360/roms/`.
4. Enciende la consola con la USB conectada. ABadAvatar lanza FreeMyXe; desde ahí arranca XeLL, y XeLL abre `xenon.elf` solo.

Guía completa en el README de la rama.

SHA-256 de `xenon.elf`: `6b5889bcb8f1b3bb0cebc1cca34b8d353ec92a067f5252ce68fb086f028eee35`
