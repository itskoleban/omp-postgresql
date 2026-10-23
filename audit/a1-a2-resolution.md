# Resolución de A1 y A2 — PR #24

Fecha: 23 de octubre de 2026. Código corregido: **`ba384fffbdef757e3a22c0dc00901af18c6c9512`**.
Base de esta revisión: `0fbea5f7c7a3e209be3f38b516490b4ea9414f21`; master permanece
`3cbf99849de57de09e4fa8317b9bb40f8e2d2f8f`. Este documento y sus evidencias no cambian el runtime.

## A1: conservar la causa observada

`Lane::wait_socket()` devuelve ahora `Error`, utilizando `Error::none` para continuar.
Captura cancelación/shutdown o deadline al entrar; socket inválido, descriptor fuera de
FD_SETSIZE y fallo no reintentable de select devuelven connection. `receive()` propaga
ese resultado sin volver a deducirlo de flags/reloj posteriores. Un error de transporte
observado por select sigue siendo connection aunque cancel se active o venza el plazo
durante la llamada. Si la interrupción se observa antes de intentar I/O, se conserva esa
interrupción; no se afirma diagnosticar una avería de red que todavía no se ha observado.

También se corrige el caso de EINTR/WSAEINTR: se reintenta mediante el bucle, que comprueba
de nuevo cancelación, stop y deadline. Una interrupción de señal no implica por sí sola
una conexión rota. Los errores de flush/consume siguen siendo connection.

No cambia la publicación terminal: SQL enviado sin respuesta definitiva sigue uncertain,
completed=0 e item uncertain; se cierra la sesión y se invalida su estado transaccional y
prepared. Recuperar la sesión no reenvía SQL. Una respuesta definitiva ya procesada puede
ganar frente a una cancelación posterior; solicitar cancelación no equivale a ROLLBACK.
Las firmas y constantes Pawn permanecen iguales.

## A2: demostrar el punto de ejecución

El nuevo `socket_wait_unit` compila el cuerpo real de `src/session.cpp` con seams de
libpq, reloj y select; Engine, colas, publicación y reconexión siguen siendo reales.
El reloj controlado avanza sin sleeps. La sincronización con condición publica el job
antes de la inyección. Contadores acreditan send, entrada a select y consumo de resultados.
El único selector de syscall nuevo, PG_SOCKET_SELECT, se resuelve directamente a ::select
en el componente normal: no añade hooks de runtime ni llamadas falsas al paquete.

Se comprueban 20 casos Windows y 21 Linux (incluye límite de descriptor): cancel/timeout/
stop al entrar; transporte solo y concurrente con cancel/deadline/ambos; socket inválido;
EINTR con recuperación/cancel/timeout; readiness y poll timeout; respuesta provisional y
cancel; respuesta completa que gana a cancel/deadline; cancelación previa al dispatch;
fallos de flush/consume coincidentes con cancel. Cada caso exige estado, causa, metadata,
fase alcanzada, un solo envío (o cero en predispatch), una completion y recuperación
cuando corresponde. Un watchdog limita fallos; ningún punto de inyección depende de sleeps.

La prueba real conserva sus 32 rondas y ahora usa otro backend para observar en
pg_stat_activity el PID propio, SQL exacto, state=active y wait_event=PgSleep antes de
cancelar. RUNNING ya no sirve como prueba de dispatch. Se exige uncertain/cancelled,
completed=0 e item uncertain. El polling solo espera la condición observable; no decide
el schedule crítico. La prueba determinista complementa a PostgreSQL real, no lo sustituye.

## Pruebas negativas y reproducción original

Se construyeron copias desechables con runtime histórico y el nuevo test, añadiendo solo
el selector de syscall necesario para la inyección (sin cambiar wait/receive históricos).

| Runtime | Caso | Fase comprobada | Resultado esperado y observado |
| --- | --- | --- | --- |
| 3cbf99849de57de09e4fa8317b9bb40f8e2d2f8f | cancel_entry | sends=1, select=0 | Exit 1: uncertain/connection, debía conservar cancelled |
| 0fbea5f7c7a3e209be3f38b516490b4ea9414f21 | transport_cancel | sends=1, select=1 | Exit 1: uncertain/cancelled, debía conservar connection |
| 3cbf99849de57de09e4fa8317b9bb40f8e2d2f8f | timeout_entry | sends=1, select=0 | Exit 1: connection en vez de timeout |
| 0fbea5f7c7a3e209be3f38b516490b4ea9414f21 | transport_timeout | sends=1, select=1 | Exit 1: timeout en vez de connection |
| 0fbea5f7c7a3e209be3f38b516490b4ea9414f21 | eintr_retry | sends=1, select=1 | Exit 1: uncertain/connection en vez de completar tras reintento |
| ba384fffbdef757e3a22c0dc00901af18c6c9512 | Todos | 20 casos en Windows | PASS; metadata y completion verificadas |

La reproducción original de la auditoría, enlazada con el pg_core corregido, publica
connection en sus tres modos de error de select, incluso con cancel/deadline concurrentes;
su cuarto modo conserva cancelación predispatch con cero envíos. Véanse
[evidencias y hashes](stability-evidence/a1-a2/SHA256SUMS.txt).

## Validación local y CI

Las evidencias locales incluyen JUnit Windows x64 (18/18) y Windows x86/MSVC (20/20),
81 métodos Python, cinco ejecuciones negativas y la reproducción original. Ambos builds se configuraron
con source_tree=clean en el commit de código indicado, PostgreSQL oficial 17.11/TLS y
PG_STRESS_SECONDS=120. Incluyen COMMIT, ROLLBACK, reconexión y las suites anteriores.
La transformación pública redacta rutas de los workspaces y hostname; el ledger registra
hash de origen y de la copia pública. Los logs históricos anteriores no se reinterpretan.

La puerta final de integración es el **CI del head actual de la PR**, incluido su merge
candidate: [checks de PR #24](https://github.com/itskoleban/omp-postgresql/pull/24/checks).
Este documento registra la validación local antes del push; no anticipa un pass remoto.
La descripción de la PR y el informe de seguimiento registran el run y SHA finales
después de esperar a GitHub Actions. Se requieren Linux x86/open.mp, Windows x86/open.mp,
Windows x64 y Linux OFF/ASan-UBSan/TSan, además del gate de paquetes.

Reproducción en un entorno de desarrollo aislado:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPG_BUILD_BENCHMARK=ON -DPG_STRESS_SECONDS=120
cmake --build build --parallel 4
ctest --test-dir build -R '^socket_wait_unit$' --output-on-failure
python tests/run_postgres.py --build build --config Debug --doc-sql
python -m unittest discover -s tests -p 'test_*.py' -v
```

En Windows, seleccionar toolchain y libpq de la misma arquitectura y proporcionar
--postgres-bin/--openssl como indica el runbook. El test admite un nombre de caso como
argumento de pg_socket_wait_tests para reproducir una intercalación individual.

## Riesgos restantes y decisión

A1 y A2 quedan corregidos y cubiertos por oráculos deterministas más integración real.
La recomendación técnica local es **APPROVE WITH CONDITIONS** hasta que pase el CI del
head final; el veredicto posterior se registra en la descripción de PR tras comprobarlo.
No se ha hecho merge, force push, tag, release ni modificación de activos RC.1.

La publicación estable v0.1.0 sigue siendo una decisión independiente: no se ejecutó una
campaña de 12–24 h, agotamiento sistemático de allocator/FD, toda partición de red, fuzz
del loader AMX ni reutilización de playerid real. R1 (asignación en publicación terminal)
permanece un riesgo no reproducido, no un nuevo defecto confirmado. Dependencias externas
no completamente instrumentadas y shutdown sujeto al SO conservan sus límites anteriores.
Los metadatos de hostname de evidencias históricas siguen siendo un hallazgo Low.
