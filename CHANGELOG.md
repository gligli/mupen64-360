# Cambios

## v0.992-wf1 (rama winchester-fixes)

⚠️ **Versión de prueba, todavía sin probar en consola real.** Si la pruebas, cuenta en un *issue* qué pasó (modelo, placa, si arrancó, si hubo video y sonido, qué juego).

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
