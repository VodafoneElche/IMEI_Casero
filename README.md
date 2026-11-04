# Comprobador IMEI

Aplicación de Windows para el equipo: escribes o pegas un IMEI y muestra al momento la marca, el modelo, la fecha de lanzamiento y la ficha técnica. Si es un dispositivo Apple, abre la comprobación de iCloud (Buscar mi iPhone) en iUnlocker con el IMEI ya copiado y permite dejar anotado el resultado antes de enviar el equipo al servicio técnico.

Está escrita en C con la API nativa de Windows: arranca al instante, el instalador ocupa menos de 2 MB y no necesita nada más instalado.

## Instalar

1. Ve a [Releases](https://github.com/VodafoneElche/IMEI_Casero/releases/latest) y descarga **ComprobadorIMEI-Setup.exe**.
2. Ejecútalo. Se instala solo para tu usuario (no pide permisos de administrador) y crea un acceso directo en el escritorio.
   La primera vez Windows puede mostrar «Windows protegió su PC», porque el instalador no tiene firma digital: pulsa *Más información → Ejecutar de todas formas*.

Si tenías la versión anterior (la de 107 MB), el instalador la quita y conserva el historial y el logo.

También puedes usar **ComprobadorIMEI.exe** sin instalar: es un único archivo que se puede copiar donde quieras.

## Actualizaciones

Al arrancar, la app consulta la última versión publicada en este repositorio. Si hay una nueva, aparece el botón **Actualizar a la versión X** en la cabecera: con un clic se descarga, sustituye al programa y se vuelve a abrir. También se puede comprobar a mano en *Ajustes → Buscar actualizaciones*.

## Publicar una versión nueva

No hace falta compilar nada en tu PC. Desde la web de GitHub:

1. Haz los cambios que quieras (código, base de datos de modelos, etc.).
2. Edita el archivo [`VERSION`](VERSION) y sube el número, por ejemplo de `1.2.0` a `1.2.1`.
3. Añade un apartado `## 1.2.1` en [`CAMBIOS.md`](CAMBIOS.md) con lo que cambia (será la nota de la versión).
4. Pulsa *Commit changes*.

GitHub Actions compila la app en Linux, la prueba en un Windows real y, si todo va bien, publica la versión en *Releases* en unos 5 minutos. Desde ese momento, las apps del equipo ofrecen la actualización al arrancar. El progreso se ve en la pestaña [Actions](https://github.com/VodafoneElche/IMEI_Casero/actions).

Si se sube un cambio sin tocar `VERSION`, se compila y se prueba, pero no se publica nada.

## Qué hace

| Dato | De dónde sale |
|---|---|
| IMEI válido o no | Dígito de control (algoritmo de Luhn). Acepta 14, 15 o 16 dígitos (IMEISV). |
| Marca, modelo, referencia y año | Los 8 primeros dígitos (TAC), en la base incluida en el programa (~255.000 TAC). |
| Fecha de lanzamiento y ficha técnica | Wikipedia (en inglés y, si no hay artículo, en español). Se guarda en caché. |
| Ficha en la web del fabricante | Botón «Ver ficha en la web oficial». |
| Buscar mi iPhone (iCloud) | Botón que abre iUnlocker con el IMEI copiado; el resultado se marca en la app. |

El historial se guarda en el propio equipo y se puede exportar a Excel (CSV). Nada se envía a ningún servidor salvo las consultas a Wikipedia y la comprobación de actualizaciones en GitHub.

`ComprobadorIMEI.exe --imei 351091214899373` abre la app comprobando directamente ese IMEI.

### Base de datos de modelos

- La base incluida se genera desde [`data/tac_full.csv`](data/tac_full.csv). Para corregir o añadir modelos, edita ese CSV y publica una versión nueva.
- Si la empresa tiene acceso a la base oficial de la GSMA, se puede cargar en *Ajustes → Importar CSV*. Lo importado tiene prioridad sobre la base incluida.

### Logo

*Ajustes → Cargar logo…* admite PNG o JPG (mejor en blanco o con fondo transparente). Se guarda solo en ese equipo: el repositorio es público y no incluye ningún logo.

### Datos locales

En `%APPDATA%\Comprobador IMEI`: `historial.tsv`, `fichas\` (caché), `logo.img` y la base TAC importada, si la hay. Desinstalar la app no borra esta carpeta.

## Compilar en local

Con MinGW-w64, Python 3 y (opcional) NSIS, en Linux o en Windows con MSYS2:

```sh
make              # dist/ComprobadorIMEI.exe
make installer    # dist/ComprobadorIMEI-Setup.exe
make selftest && wine build/selftest.exe   # pruebas (en Windows: build\selftest.exe [--red])
```

## Estructura

```
src/        código de la app (main.c ventana principal, settings.c ajustes, wiki.c Wikipedia,
            tacdb.c base TAC, store.c historial y caché, update.c actualizaciones, net.c HTTPS)
tests/      pruebas del núcleo
tools/      generador de la base TAC binaria
data/       base TAC de origen (CSV)
installer/  script del instalador (NSIS)
vendor/     cJSON y stb_image
assets/     icono y tipografía
```

## Licencias de terceros

- Base TAC: [tac-database](https://github.com/MoazEb/tac-database) (MIT), ver `data/LICENSE-tac-database.txt`.
- Fichas: Wikipedia, bajo licencia CC BY-SA (la app enlaza siempre al artículo).
- [cJSON](https://github.com/DaveGamble/cJSON) (MIT) y [stb_image](https://github.com/nothings/stb) (dominio público / MIT).
- Tipografía Atkinson Hyperlegible, de Braille Institute (SIL Open Font License), ver `assets/fonts/OFL.txt`.
