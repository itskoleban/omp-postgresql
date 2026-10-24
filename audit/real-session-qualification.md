# Integración real de open.mp — omp-postgresql v0.1.0

Fecha: 24 de octubre de 2026 (America/Bogota). Rama: `codex/real-player-qualification`.
PR: https://github.com/itskoleban/omp-postgresql/pull/27.

## Identidad y alcance

El `master` inspeccionado y vuelto a consultar es
`79cf0e659e426af559ecc305f4009104d77eb64b`. Entre `177c95f` y este master
sólo cambiaron cuatro archivos de auditoría; las 227 ejecuciones históricas
siguen vinculadas a su commit original y no se suman a estos resultados.
Las corridas completas usan `28933d11ef3edbfd5cc24f929109c4961ddeb5b2` (Linux)
y `063191e23f06e5df3bcc7349f77bade9805abb11` (Windows). El segundo añade la
espera determinista de agotamiento, la procedencia inicial y la comprobación de
no replay por secuencia; esta última también se ejecutó separadamente en Linux.
Los manifiestos registran el checkout limpio, los hashes de entradas y del arnés.
El componente se recompiló desde master; `src/`, `include/` y `pawn/` son idénticos
en la rama de pruebas. No hay cambio de implementación, API Pawn ni ABI.

Se revisaron `docs/accounts.md`, `examples/accounts.pwn`, `examples/custom.pwn`,
`docs/validation.md`, las dos calificaciones históricas solicitadas y los workflows.
La autenticación predeterminada sigue fallando cerrada sin proveedor. La nueva
suite habilita y ejecuta realmente `samp-bcrypt`; no reemplaza bcrypt por un hash
ficticio ni llama sintéticamente a `OnPlayerConnect` o `OnPlayerDisconnect`.

## Viabilidad comprobada

| Capacidad | Evidencia y conclusión |
| --- | --- |
| Servidor oficial | open.mp **1.5.8.3079 x86** arrancó y cargó el componente y Pawn en Windows y Linux. Es la versión fijada por el proyecto; no se extiende la conclusión a otros servidores. |
| Cliente automatizado | RakSAMP-cli `f75af4cb97fed341cb45b49aff489f9f20665e99`, compilado desde fuente. Usa handshake RakNet y RPC normales de jugador, responde a los diálogos por red y ejecuta en proceso externo. El servidor observó `IsPlayerNPC=0`, conexiones, desconexiones y nuevas sesiones. |
| Adaptación del cliente | El parche conservado permite stdin por pipe, evita UI interactiva para diálogos y evita interpretar fragmentos `-n/-p` de rutas Windows como opciones antiguas. No cambia el protocolo. LuaJIT se compiló desde `a4c064539ea931274d407e1249b4bb575424a0f4`; se descartó el precompilado que requería MSVCR120 ausente. |
| Reutilización real | A y B fueron procesos de cliente distintos; se observó el mismo `playerid=0`, tokens diferentes y el callback de A después de conectar B. Se comprobó para lookup, hash, verify, INSERT, carga y guardado custom. |
| NPC oficial | `NPC_Create`/`NPC_Destroy` se ejecutaron y se observaron `npc=1`, slot 7 y su retirada diferida. Sirven para ejercitar callbacks y limpieza de entidades del servidor; estos resultados no se cuentan como conexiones de jugadores. |
| Proveedor | Binarios x86 de **samp-bcrypt 0.4.1**, revisión `c60e68d483a95d109855fdd7517425c0363fd10c`. Se observaron callbacks de hash y verificación real, con los tokens transmitidos por `args`. |
| GitHub Actions | Los jobs x86 Linux/Windows preparan clientes/proveedor fijados, compilan las dos fixtures y ejecutan la suite acotada. No requieren GTA, escritorio ni runner privado para este protocolo de compatibilidad. |
| Runner específico | Un cliente gráfico oficial y la ruta de cifrado del cliente open.mp necesitan una instalación autorizada del juego y un runner Windows adecuado. Esa ruta no se evaluó y no se infiere de RakSAMP. |

Fuentes primarias:
[release oficial](https://github.com/openmultiplayer/open.mp/releases/tag/v1.5.8.3079),
[NPC_Create](https://open.mp/docs/scripting/functions/NPC_Create),
[IsPlayerNPC](https://open.mp/docs/scripting/functions/IsPlayerNPC),
[cliente fijado](https://github.com/pavetr1337/RakSAMP-cli/tree/f75af4cb97fed341cb45b49aff489f9f20665e99),
[proveedor y dispatcher](https://github.com/Sreyas-Sreelal/samp-bcrypt/tree/c60e68d483a95d109855fdd7517425c0363fd10c).

## Entorno aislado y dependencias

Servidor y PostgreSQL se enlazaron exclusivamente a `127.0.0.1`. No hubo anuncio,
query pública, RCON remoto ni puertos Docker publicados. Cada ejecución creó un
cluster nuevo y lo detuvo al terminar. Sólo se usaron cuentas ficticias. El rol
`qa_runtime` recibió SELECT/INSERT y USAGE en la secuencia del tutorial, y los
permisos SELECT/UPDATE necesarios para custom; no fue propietario ni superusuario.

Las conexiones normales usaron TLS `verify-full`. La única excepción fue el peer
de pérdida de confirmación, en loopback sin TLS para identificar y descartar
exactamente `CommandComplete INSERT 0 1`. Esa excepción se limita al fallo probado.
No hubo reintentos de SQL de escritura desde el arnés después del resultado incierto.

| Dependencia | Windows local | Linux local |
| --- | --- | --- |
| Arquitectura servidor/componente | PE32 x86 / MSVC 19.51.36248 | ELF32 x86 / GCC 13.3.0 |
| Servidor open.mp | 1.5.8.3079 oficial | 1.5.8.3079 oficial dynssl |
| libpq | 17.11, parche de mutex del repositorio | 17.11, TLS y `-m32` |
| Backend PostgreSQL | 17.11, herramientas EDB x64 conservadas | 16.15 de Ubuntu, proceso aislado; CI usa PostgreSQL 17 |
| SDK | `473b732b2de159773b2e2908fd11aa99f07db284` | mismo |
| Pawn | 3.10.11 / `29f1a8c7fd2e01929807cd8c50353fbc93bbd651` | mismo |
| stdlib | `46a5d1d7ac8985e4104f745c462a247a0c2eb6a0` | mismo |
| bcrypt | 0.4.1 x86, coste normal 12 | mismo |

El contenedor fue Ubuntu 24.04, imagen
`sha256:68ee22004a9109b44973313b02ff7b1de250f1de0f5ecb628d73f579aaff33ac`.
Se utilizó un usuario sin privilegios para initdb, PostgreSQL y las pruebas Linux.
No se utilizaron servicios remotos del usuario.

## Método y controles de las pruebas

Las fixtures incluyen los ejemplos originales con callbacks renombrados y los
envuelven para registrar la sesión y observar el cierre de cada trabajo. Los
eventos de conexión, desconexión, comandos y diálogos los produce open.mp al
recibir tráfico del cliente. Los callbacks SQL los produce el componente; los
de contraseñas, el proveedor externo.

Para retener SQL se adquirieron locks reales en PostgreSQL. La variante `retain`
no transmite `pg_cancel` al desconectar: representa trabajo aceptado que llega a
terminar pese a la salida de A y permite observar el callback confirmado después
de la entrada de B. Se prueba aparte la cancelación predeterminada y su cierre.
No se presenta esa variante como prueba de que la cancelación predeterminada
entrega tarde: en la corrida observada su callback puede llegar antes de B.

Las carreras de hash/verify usan bcrypt real de coste 16 para dar tiempo a la
reutilización. GMX usa coste 18 para que el proveedor termine después del intervalo
de recarga; se observa el callback antiguo en la nueva VM con otro token. El
timeout conserva los 30 segundos reales del ejemplo: un adaptador de fallo
descarta el callback de bcrypt después de que éste haya calculado su resultado.
Esto prueba una respuesta ausente, no un bloqueo interno espontáneo del proveedor.

La concurrencia de registro se ejecuta con dos servidores y el mismo nombre:
open.mp no permite dos jugadores homónimos en una sola instancia. Ambos pasan
el lookup vacío antes de que se liberen los INSERT: uno autentica y el otro recibe
`23505`; queda exactamente una fila. La verificación concurrente exige observar
los dos comienzos antes del primer callback de finalización.

Custom usa su lógica original de pertenencia de sesión/solicitud y dos lanes.
La fixture permite su puerta RCON local y conserva los trabajos al desconectar;
ambos controles están confinados al gamemode de pruebas. No califica una política
de autorización de producción. Se observan los callbacks de save de las solicitudes
2 y 3, el nuevo token de B, ausencia de cuenta/puntuación heredadas y ambos trabajos
cerrados.

La recuperación de PostgreSQL detiene y arranca el proceso real. La pérdida de
readiness invalida la autenticación. Cuando el pool llega a `PG_FAILED`, el control
del arnés cierra y crea explícitamente el pool, tal como exige la documentación;
el transporte recuperado sólo abre un lookup y solicita otra prueba de contraseña.

Las contraseñas se generan en memoria y se envían por stdin al cliente. No se
escriben en archivos ni se pasan como argumentos del proceso. Se deshabilitan
logs SQL/chat del servidor, parámetros SQL en errores y statements de PostgreSQL.
El arnés comprueba que la salida del servidor no contiene las contraseñas generadas
ni hashes bcrypt. El paquete de evidencias se vuelve a inspeccionar por hashes;
no se incluye el cluster, sus claves TLS ni perfiles de servicio.

## Resultados

La matriz CSV contiene una fila por escenario y plataforma, con commit, método,
resultado y ruta del log. Los JSON conservan aserciones, SQL de control y resultados
no sensibles; los logs conservan eventos ordenados, tokens, handles y códigos.

| Ejecución local | Commit de pruebas | Resultado |
| --- | --- | --- |
| Linux x86, suite completa | `28933d1` | **19/19 PASS**, checkout limpio |
| Windows x86, suite completa final | `063191e` | **19/19 PASS**, checkout limpio |
| Linux, INSERT incierto y secuencia | `063191e` | **2/2 PASS**, registro de fixture y pérdida de confirmación |
| Windows, INSERT incierto y secuencia | `063191e` | **2/2 PASS**, repetición independiente |

Son **19 escenarios distintos por plataforma**, no 42 escenarios distintos.
Las repeticiones no amplían la cobertura por sí mismas. La secuencia de identidad
avanzó una sola vez tras el INSERT incierto y siguió sin avanzar durante la
reconciliación: no hubo ni segunda fila ni intento adicional de INSERT.
La matriz conserva asimismo los FAIL de preparación y los NOT RUN de alcance.

La CI de código `44d5de9` confirmó los 19 escenarios en Linux x86 con backend
PostgreSQL 17 y sus gates de paquetes. El estado más reciente de toda la CI está
en la PR; no se atribuye a un commit posterior el resultado de un SHA anterior.

Se incluyen además las regresiones de base locales: **21/21 CTest Windows x86**,
**22/22 CTest Linux x86** y **89 métodos Python**. Son suites independientes de los
19 escenarios de red por plataforma. La diferencia de cantidad CTest obedece a
la prueba Linux de agotamiento de descriptores; los builds locales no habilitaron
el benchmark opcional ni el tmpfs ENOSPC. La CI conserva sus gates adicionales.

## Fallos de preparación y límites conservados

Los intentos previos se conservaron como desarrollo, no como calificación final:
MSVCR120 ausente en el precompilado LuaJIT, dependencia libwinpthread ausente,
parser antiguo del cliente que alteraba el nickname al encontrar `-n` en la ruta,
respuesta de diálogo con listitem incorrecto, SQLSTATE interpretado como entero,
expectativa errónea de reconexión de un pool `PG_FAILED`, callback del proveedor
terminado durante el intervalo sin VM de GMX y comprobación demasiado temprana
de `NPC_Destroy`. Se corrigieron los clientes/controles/expectativas del arnés.
No son defectos reproducidos del componente y no motivaron cambios de API o ABI.

En Windows hubo tres observaciones de arranque sin readiness: dos en custom y una
al iniciar el segundo servidor de registro concurrente. Los logs PostgreSQL de los
arranques custom mostraron abortos de socket TLS (Winsock 10053). No se estableció
su causa y no se atribuyen al componente. Se conservaron los logs fallidos.
Los dos casos custom pasaron en un cluster de control; registro concurrente,
recuperación y custom pasaron en otro seguimiento, y la corrida completa final
pasó 19/19. Esto demuestra el funcionamiento observado, no explica ni borra los
abortos previos. Antes de congelar el candidato conviene contrastar la CI Windows
y repetir si vuelve a aparecer el fallo en un runner con recursos aislados.

No se infiere liberación byte a byte del heap externo de bcrypt o de libpq a partir
del cierre de handles. Se comprueba invalidación de trabajos completados y salida
limpia de los procesos; los sanitizadores históricos y la CI actual tienen su
propio alcance y no instrumentan todos los binarios oficiales de terceros.

## Pendientes y recomendación

Los caminos funcionales críticos solicitados se observaron con clientes de red
reales y proveedor real en ambas plataformas. La evidencia permite pasar a revisión
de la PR y selección de un candidato; **no constituye aprobación estable** ni
autoriza iniciar 24 horas. La congelación debe usar un SHA revisado y su CI verde,
con la incidencia de arranque Windows contrastada y aceptada o resuelta.

Permanecen fuera de esta calificación: cliente gráfico oficial y cifrado propio de
open.mp; bloqueo interno espontáneo de bcrypt (se probó respuesta descartada);
instrumentación de heap de terceros; interacción del dispatcher bcrypt entre varias
VM con los mismos nombres de callback; políticas de permisos/exclusividad de una
aplicación de producción; combinaciones no instrumentadas de fallos de red/storage.
La cancelación por defecto no recibe un PASS de callback posterior a B si éste llegó
antes: la carrera confirmada posterior a B es la variante retain explícita.

No hay en esta fase un defecto reproducido de implementación del componente que
justifique alterar la API o la ABI. Los cambios son pruebas, preparación y documentación.

No se inició, programó ni despachó la campaña de 24 horas. Tampoco se creó un tag,
se publicó un release, se fusionó la PR ni se modificó directamente master.

## Reproducción

Partir del commit indicado en un checkout limpio. Construir el componente x86 y
Pawn siguiendo los jobs x86 de `.github/workflows/ci.yml`; usar el servidor oficial
1.5.8.3079 y verificar el archivo antes de extraerlo en un directorio desechable.
El script descarga y verifica el proveedor, compila las fixtures y crea un cluster
nuevo por ejecución. El directorio de salida debe ser nuevo.

Linux, con libpq32 y herramientas PostgreSQL en PATH:

```sh
python3 scripts/run_real_session_ci.py --output build-real-sessions \
  --component build-linux32/omp-postgresql.so --server /tmp/openmp/Server \
  --compiler build-pawn32/pawncc --stdlib build-stdlib \
  --pawn-include build-linux32/_deps/pawn-src/include \
  --runtime "$PWD/libpq32/lib/libpq.so.5"
```

Windows PowerShell, con MSYS2 MINGW32 y herramientas PostgreSQL explícitas:

```powershell
python scripts/run_real_session_ci.py --output build-real-sessions `
  --component build-msvc32/Release/omp-postgresql.dll --server build-openmp/Server `
  --compiler build-pawn32/Release/pawncc.exe --stdlib build-stdlib `
  --pawn-include build-msvc32/_deps/pawn-src/include --postgres-bin build-server/pgsql/bin `
  --mingw-bin C:/msys64/mingw32/bin --bash C:/msys64/usr/bin/bash.exe `
  --runtime libpq32/bin/libpq.dll --runtime libpq32/bin/libssl-3.dll --runtime libpq32/bin/libcrypto-3.dll
```

El timeout externo es de 600 segundos; las esperas por evento son acotadas. Los
costes 16/18 sólo se usan para estas carreras, no para una campaña de carga.
`--client` permite reutilizar un cliente ya construido por
`scripts/prepare_real_clients.py` cuyo manifiesto y hashes se hayan conservado.

SHA-256 de entradas descargadas:

| Entrada | SHA-256 |
| --- | --- |
| open.mp Windows | `d1b5ea1d09e7de0f144bf0055183e3ceaad5ff8a0a8e791021141e8973f84e03` |
| open.mp Linux dynssl | `2be12550e3aaff0e60510ae8823e18b9759990eca3f02c101a49bcb3c985d222` |
| samp-bcrypt Windows | `673e7511699a8e92452a323d4e35567e2c990c0779e1ef2d20b298940cd031c3` |
| samp-bcrypt Linux | `ebf76b12d799046a605e3063bb7b64b08e1fad9bdb3c299b357ec215a0793b5d` |
| PostgreSQL 17.11 source | `5367f6fb2ec97efe1eb2e0c7926bb33438e51b0bd3a9733b88498056a7dc9a7e` |

Los hashes de componente, AMX, cliente, proveedor, servidor y runtimes realmente
ejecutados figuran en cada `results.json`; no se sustituyen por hashes de releases
anteriores.
