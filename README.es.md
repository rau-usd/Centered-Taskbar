<p align="center">
  <img src="assets/icon.png" width="72" alt="Icono de Centered-Taskbar">
</p>

<h1 align="center">Centered-Taskbar</h1>

<p align="center">
  Una pequeña app de bandeja que centra los iconos de la barra de tareas de <b>Windows 10</b> — con animación suave y casi sin consumo de recursos.
</p>

<p align="center">
  <a href="README.md">English</a>
</p>

<!-- Consejo: agrega aquí una captura o un GIF corto, por ejemplo ![demo](docs/demo.gif) -->

---

## Características

- **Centra los botones de la barra de tareas** en la pantalla, como hace Windows 11, pero en Windows 10.
- **Animación suave** al abrir o cerrar apps: los iconos se deslizan a su nueva posición en lugar de saltar. La velocidad se puede ajustar (Off / Fast / Normal / Smooth).
- **Liviana**: un único ejecutable nativo en C++, sin runtime, sin instalador y sin permisos de administrador.
- **Poco uso en segundo plano**: cuando nada cambia, se despierta solo cada 1.5 segundos para hacer una medición. El temporizador de la animación a 60 FPS solo corre una fracción de segundo después de que cambia la barra.
- **Icono en la bandeja** con un pequeño menú oscuro para controlar todo.
- Opción de **iniciar con Windows** (por usuario, sin aviso de UAC).
- Devuelve la barra a su disposición original al desactivar la opción o al salir.

## Requisitos

- Windows 10 (64 bits) con la barra de tareas clásica.
- Barra de tareas **horizontal** (abajo o arriba). Las barras verticales (izquierda/derecha) no se tocan.

> **Windows 11 no está soportado**: su barra de tareas es un componente distinto.

## Instalación

1. Descarga `Centered-Taskbar.exe` desde la página de [Releases](../../releases).
2. Colócalo donde quieras y ejecútalo. Aparece un icono en la bandeja y tus iconos se mueven al centro.
3. *(Opcional)* Clic derecho en el icono de la bandeja → **Run at startup**.

No hay nada que instalar. Para quitarla, mira [Desinstalar](#desinstalar).

## Uso

Haz clic en el icono de la bandeja (botón izquierdo o derecho) para abrir el menú:

| Opción | Qué hace |
|---|---|
| **Center taskbar** | Activa o desactiva el centrado. Al desactivarlo, los iconos vuelven a la izquierda. |
| **Animation speed** | `Off` (instantáneo), `Fast`, `Normal` (por defecto) o `Smooth`. Se aplica al instante. |
| **Run at startup** | Inicia la app al iniciar sesión en Windows. |
| **Exit** | Cierra la app y restaura la disposición original de la barra. |

> **Consejo:** oculta el cuadro de *Búsqueda* y el botón de *Vista de tareas* (clic derecho en la barra) para que los iconos puedan llegar al centro exacto de la pantalla. La app nunca se superpone con esos elementos ni con el área de notificaciones.

### Archivo de configuración

Los ajustes se guardan en `%APPDATA%\Centered-Taskbar\config.ini` y normalmente se cambian desde el menú de la bandeja:

```ini
[CenteredTaskbar]
CenterTaskbar=1   ; 1 = activado, 0 = desactivado
AnimSpeed=2       ; 0 = Off, 1 = Fast, 2 = Normal, 3 = Smooth
```

## Cómo funciona

Windows 10 coloca los botones de la barra dentro de una ventana hija de Explorer (`MSTaskListWClass`) que siempre empieza a la izquierda. Centered-Taskbar:

1. Encuentra esa ventana (`Shell_TrayWnd` → `ReBarWindow32` → `MSTaskSwWClass` → `MSTaskListWClass`).
2. Lee la posición de cada botón con **UI Automation** (una sola llamada con caché) y calcula dónde debe estar el grupo de botones para quedar en el centro de la pantalla, sin pisar el área de notificaciones.
3. Mueve esa ventana con `SetWindowPos`, acercándola al destino con una curva suave.
4. Reacciona al shell: los eventos de ventana creada / cerrada / activada disparan una animación corta, y un hook WinEvent devuelve la lista a su sitio al instante si Explorer la reposiciona, así nunca se ve un salto a la izquierda.
5. Un sondeo lento (cada 1.5 s, una medición) cubre todo lo demás, como arrastrar botones para reordenarlos.

No se inyecta código en Explorer y la app no necesita privilegios elevados.

## Limitaciones

- Solo Windows 10; Windows 11 no está soportado.
- Solo se centra la barra del **monitor principal**.
- Las barras verticales se ignoran.
- El método depende de **detalles internos no documentados de Explorer** (nombres de clase y disposición). Una futura actualización de Windows podría romperlo.
- Si el proceso se cierra de golpe (por ejemplo desde el Administrador de tareas), los iconos se quedan donde estaban hasta que Explorer reorganice la barra. Volver a abrir la app y elegir **Exit**, o reiniciar Explorer, lo soluciona.
- Otras herramientas que también muevan los botones de la barra pueden entrar en conflicto con esta.

## Solución de problemas

- **Los iconos no se centran**: comprueba que *Center taskbar* esté marcado en el menú de la bandeja y que la barra esté abajo o arriba.
- **Los iconos saltan en vez de deslizarse**: pon *Animation speed* en *Normal* o *Smooth*.
- **No pasa nada después de que Explorer se cerró o reinició**: la app se vuelve a enganchar sola en un par de segundos; si no lo hace, ciérrala y ábrela de nuevo.

## Desinstalar

1. Clic derecho en el icono de la bandeja → desmarca **Run at startup** y luego elige **Exit**.
2. Borra `Centered-Taskbar.exe`.
3. *(Opcional)* Borra la carpeta `%APPDATA%\Centered-Taskbar`.

## Compilar desde el código fuente

Necesitas una toolchain MinGW-w64 (por ejemplo [w64devkit](https://github.com/skeeto/w64devkit), un zip portable que no requiere instalación) con `g++` y `windres` en el `PATH`.

```bat
git clone https://github.com/<tu-usuario>/Centered-Taskbar.git
cd Centered-Taskbar
build.bat
```

El ejecutable queda en `dist\Centered-Taskbar.exe`. El script son solo dos comandos, ejecutados desde la raíz del repositorio:

```bat
windres res/Centered-Taskbar.rc -O coff -o build/Centered-Taskbar.res
g++ -Os -s -municode -mwindows -static -ffunction-sections -fdata-sections -Wl,--gc-sections ^
  -o dist/Centered-Taskbar.exe src/Centered-Taskbar.cpp build/Centered-Taskbar.res ^
  -lole32 -loleaut32 -luuid -lshell32 -lgdi32 -luser32 -ladvapi32
```

Un workflow de GitHub Actions (`.github/workflows/build.yml`) compila el proyecto en cada push y, cuando subes una etiqueta como `v1.0.0`, adjunta `Centered-Taskbar.exe` a un Release de GitHub.

## Créditos

- Icono de la aplicación: icono *Taskbar* de [Icons8](https://icons8.com).

## Licencia

El código fuente se publica bajo la [licencia MIT](LICENSE). El icono es un recurso de terceros de Icons8 y se rige por la [licencia de Icons8](https://icons8.com/license), no por la MIT.
