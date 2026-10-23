# Auditoría de estabilidad v0.1.0 — 23 de octubre de 2026

> **Seguimiento A1/A2:** [resolución, pruebas negativas y nueva evidencia](a1-a2-resolution.md).
> El runtime corregido es `ba384fffbdef757e3a22c0dc00901af18c6c9512`; las ejecuciones
> descritas a continuación pertenecen al estado histórico y no se reasignan a ese SHA.
> El resultado del CI del head final se comprueba y registra por separado en PR #24.
> Resolver estos bloqueantes de integración no autoriza publicar v0.1.0 estable.

**Veredicto: INSUFFICIENT EVIDENCE para aprobar una versión estable.**
La validación Windows ejecutada es satisfactoria después de corregir un defecto Medium,
pero faltan la calificación Linux de la revisión final, los paquetes nuevos extraídos y
las campañas prolongadas/adversas restantes. Este informe no autoriza una publicación estable.

## Identidad y alcance

- Base auditada: `3cbf99849de57de09e4fa8317b9bb40f8e2d2f8f`.
- Revisión de código ejecutada: `6cf0c05893c79cb8dbdb0b54f2eeacf585ac3f91`, árbol limpio.
- Rama: `audit/v0.1.0-stability`. El commit posterior de documentación/evidencias no
  cambia los binarios probados. No se atribuyen esas ejecuciones a otro SHA.
- Corrección funcional: `6303e0f32cb6bc708b5902abda7d16c61d8cf726`.
- No se modificaron `master`, tags, releases, activos RC.1 ni servicios de producción.
  Las bases usadas fueron clústeres temporales limitados a loopback y detenidos al finalizar.
- Auditoría realizada por inspección y ejecución local; no se presenta como una nueva
  auditoría independiente del revisor histórico de `docs/independent-audit.md`.

## Resultado ejecutivo

| Verificación ejecutada | Resultado | Evidencia |
| --- | --- | --- |
| Windows x64, GCC 16.1, libpq 18.4, servidor oficial PostgreSQL 17.11/TLS | **17/17 CTest**, cero fallos/errores/omisiones; 226,20 s de CTest | [JUnit](stability-evidence/windows-x64.xml) |
| Windows x86, MSVC 19.51, libpq 17.11 con parche del proyecto, servidor PostgreSQL 17.11/TLS | **19/19 CTest**, cero fallos/errores/omisiones; aproximadamente 205,7 s | [JUnit](stability-evidence/windows-x86.xml), [log completo](stability-evidence/windows-x86-ctest.txt) |
| Python | **81 métodos**, 75,281 s | [Log](stability-evidence/python-final.txt) |
| open.mp oficial 1.5.8.3079 Windows x86, componente construido desde fuente | Smoke y GMX/unload/reload, callbacks, extensiones, bootstrap, sections, logging, performance y cuatro tutoriales: PASS | [Extractos](stability-evidence/openmp-source-excerpts.txt) |
| Pawn 3.10.11 | 13 ejemplos públicos; 29 fragmentos Markdown y 70 declaraciones compilados; un fragmento explícitamente incompleto no se trata como ejecutable | Compilación local con warnings como errores; no sustituye ejecución Pawn |
| Fuzzing semántico normal | 4.096 casos por suite, y **100.000 casos adicionales** sin sanitizadores; semilla extendida 1592594996 | [Log extendido](stability-evidence/extended-fuzz.txt) |
| Helpers con Clang 22 ASan/UBSan Windows x64 | **4.096 casos**, semilla 1096107081; sin diagnósticos, salida 0; 16,45 s | [Log](stability-evidence/asan-ubsan-helpers.txt) |
| Supervisor externo, ejecución reducida | Completó integración/carga y fuzz extendido, conserva SHA y estados; limpieza de respaldo innecesaria | [Resumen](stability-evidence/external-runner-summary.json) |

ASan/UBSan local instrumentó `audit_fuzz.cpp`, `parameters.cpp`, `sql_sections.cpp` y
`statement.cpp`: **no** el Engine completo, libpq, OpenSSL ni el intérprete AMX. No hubo
una nueva ejecución local de TSan. La prueba x64 del Engine no califica un servidor x64.

## Cobertura histórica contrastada

Se revisaron `docs/validation.md`, `docs/rc-readiness.md`, `docs/independent-audit.md`,
el ledger de continuidad y los workflows. Además se recuperaron los logs completos de
los siete jobs de [CI 39666914034](https://github.com/itskoleban/omp-postgresql/actions/runs/39666914034),
exactamente en la base `3cbf998`: Linux x86 18, Windows x86 17, Windows x64 15 y
Linux x64 OFF/ASan-UBSan/TSan 18 cada uno: **104 casos CTest históricos**. También
constan los escenarios de servidor fuente/paquete y el job de distribuciones.
Los [extractos conservados](stability-evidence/historical-ci-excerpts.json) distinguen
esa ejecución remota histórica de las ejecuciones nuevas. No se detectaron firmas de
diagnósticos ASan/UBSan/TSan en los logs recuperados; las dependencias no estaban
completamente instrumentadas. No se volvieron a descargar/verificar los activos RC.1.

Ya existían pruebas de FIFO, snapshots/prepared/batches, savepoints, COMMIT rechazado
por constraints diferidos, pérdida de confirmación COMMIT, restart inmediato, cancelación,
streaming/backpressure, límites, fronteras nativas, unload y logging. Las nuevas pruebas
complementan esos escenarios; las suites existentes se conservaron.

## Revisión de arquitectura y límites

- **Engine y workers:** registros públicos en el hilo propietario; un `PGconn` y su
  destrucción pertenecen a cada worker. La cola está protegida y la publicación terminal
  atómica precede la lectura de resultados. `poll()` extrae completions antes de ejecutar
  callbacks; el join ocurre antes de destruir las colas. El análisis encontró D1 en la
  clasificación de una interrupción, no una prueba de carrera sobre resultados.
- **Pools/FIFO y ownership:** las lanes conservan orden local, no orden global. Los jobs
  aceptados poseen snapshots; el cierre de statements no invalida ese trabajo. La carga
  comprueba contadores SQL exactos, reutilización de capacidad y expiración de weak refs.
- **Transacciones/savepoints:** reserva de lane y de capacidad para finalizar; reconciliación
  de COMMIT rechazado ya existente. Las nuevas pruebas bloquean un trigger diferido dentro
  de COMMIT, terminan únicamente el PID obtenido de nuestra conexión y verifican cero filas,
  estado incierto, nueva sesión y recuperación. No hay reejecución automática de escrituras.
- **Scripts/callbacks:** generaciones AMX, argumentos copiados y dispatch sin locks de cola.
  Se conservaron pruebas de reentrada, cierre desde callbacks, sleep/error y unload. El
  reset sintético durante `Exec()` documentado en la revisión anterior sigue sin una
  reproducción alcanzable en el servidor oficial; no se convierte aquí en un crash confirmado.
- **Streaming/batches/prepared:** backpressure acotado por bloques, deadlines y cancelación;
  invalidación de prepared al cambiar sesión/plan. Se verifican orden y completitud de filas.
  Un bloque provisional no confirma el resultado SQL ni justifica replay.
- **Parser/archivos:** UTF-8 sin NUL, nombres/budgets, comentarios anidados, quotes/dollar
  delimiters, selección exacta y rechazo de secciones duplicadas/múltiples statements.
  Canonicalización y tamaño no protegen contra un administrador que cambie symlinks entre
  comprobación/apertura; los SQL son código confiado del operador.
- **Nativas/memoria:** se conservaron comprobaciones de aridad, direcciones/celdas, índices,
  preflight de salida y dimensiones del contrato de slots planos. El fuzz nuevo evalúa
  helpers/argumentos del componente, no el cargador ni el intérprete AMX.
- **Logging/shutdown:** colas limitadas, descartes contabilizados y writer asíncrono. Las
  pruebas históricas Linux EACCES/short-write/ENOSPC no son nuevas pruebas Windows. Llamadas
  bloqueadas del SO/DNS/auth/filesystem pueden retrasar el join: no hay límite absoluto
  de shutdown independiente del SO. Los drops observados bajo saturación son contabilizados,
  no se presentan como pérdida cero.
- **libpq:** protocolo extendido, single-row retrieval y recuperación sin replay. Una fila
  o mensaje puede asignarse dentro de libpq antes de aplicar el presupuesto del componente;
  los límites retenidos no son un límite estricto de RSS.

## Defecto D1 — Medium — corregido

**Problema:** una cancelación observada en `Lane::wait_socket()` podía publicarse como
`Error::connection` en lugar de `Error::cancelled`. El mismo camino descartaba la causa
de un deadline observado dentro del wait. El estado de SQL enviado permanecía incierto.

**Reproducción:** con Engine/libpq reales y PostgreSQL 17.11 desechable, enviar
`SELECT pg_sleep(1)`, esperar `Status::running`, activar `job->cancel` y despachar el
terminal. La carga mixta inicial falló; una regresión dedicada de 32 rondas volvió a
fallar en la primera ronda:

```text
cancel round=0 status=4 error=5 message=Interrupted connection
audit_faults FAIL Observed cancellation lost its cancellation error category
```

**Esperado:** causa `cancelled`; tras dispatch, incertidumbre conservada y ningún replay.
**Observado:** estado `uncertain` correcto pero causa de transporte incorrecta. Esto puede
desviar el manejo de errores de la aplicación; no se demostró corrupción ni confirmación falsa.

**Causa raíz:** el bucle comprueba flags/deadline antes de consumir input. Pueden cambiar
antes de la comprobación interna de `wait_socket()`. Su retorno falso se traducía
incondicionalmente a fallo de conexión. La corrección vuelve a leer cancel/stop/deadline
antes de usar el fallback de transporte. No cambia firmas, ABI, política de reconexión,
clasificación de incertidumbre ni replay. La rama de timeout se prueba ahora en ocho rondas;
la reproducción previa conservada demuestra específicamente la cancelación.

**Regresión:** [fallo previo](stability-evidence/cancel-before.txt),
[suite x64 posterior](stability-evidence/windows-x64.xml) y
[suite x86 posterior](stability-evidence/windows-x86.xml). El fallo previo corresponde
al runtime de la base con las nuevas pruebas antes del fix; no se presenta ese árbol
de trabajo inicial como un build limpio del SHA final.

No se confirmó un nuevo Critical/High del componente en este trabajo. Esa observación
no demuestra ausencia de defectos en todos los schedules o entornos.

## Carga reducida y medición

Se ejecutaron fases sostenidas de mínimo **120 segundos**, no 12 ni 24 horas, con semilla
1592594996. La misma instancia principal del Engine retuvo cuatro lanes durante cada fase.
Cada wave tiene 48 jobs; cada 32 waves hay transacciones read/write, streaming ordenado,
cancelación, reconexión y creación/cierre de una conexión adicional. Hay pausas de recuperación
deterministas y después se ejecutan los escenarios de lifecycle existentes.

Windows x86: **393 waves**, **182.365 operaciones de carga contabilizadas**, último sample
a **120,113289 s**, aproximadamente **1.518 operaciones/s** bajo esta carga y este host.
RSS tras la primera wave: **15.908 KiB**; final **16.524 KiB**; máximo observado **16.544 KiB**.
CPU del proceso hasta el último sample: **36,546875 s**. Medianas de los percentiles por wave:
p50 **131.979 us**, p95 **246.549 us**, p99 **258.938 us**. No son percentiles globales.

[CSV x86](stability-evidence/stress-x86.csv) y [resumen estructurado](stability-evidence/metrics-summary.json).
Los samples de pending=0/errors=0 son puntos de quiescencia comprobados; no muestreo continuo.
El contador omite setup/verificación y parte del SQL auxiliar. No se midieron bytes del
allocator. Las mediciones finitas no establecen una cota de fuga ni comparativas de rendimiento.

En x64, CTest aprobó la fase completa, pero el CTest posterior de fuzzing sobrescribió
`LastTest.log`; el JUnit conserva stdout truncado y solo tres muestras completas iniciales.
Se conserva explícitamente esa captura parcial, **sin inferir métricas finales x64**.
El texto x64 adjunto se deriva del stdout del JUnit y está identificado como tal.

## Fallos, bloqueos y trabajo no ejecutado

- **Fallo previo D1:** conservado y corregido; no se borra del historial de resultados.
- **Proveedor local PostgreSQL 18.4/MSYS2:** la suite base quedó 14/15. El servidor registró
  backend terminado por excepción `0xC0000028` al procesar `SELECT 1; SELECT 2`; el cliente
  obtuvo timeout. Con servidor oficial 17.11 ese caso pasó, usando también libpq 18.4.
  No se completó un repro standalone fuera del Engine; no se atribuye como nuevo defecto
  confirmado del componente ni se certifica ese proveedor. Logs originales en la entrega local.
- **ASan/UBSan 100.000 casos:** empezó pero se interrumpió al presupuesto aproximado de
  diez minutos de pared, sin resultado completo; no pasó. Una ejecución separada de 4.096
  casos sí terminó y es la única calificación sanitizada nueva declarada.
- **Sandbox:** initdb, sockets/Python y workers MSBuild tuvieron bloqueos de permisos.
  Esos intentos fallidos no cuentan como passes. Las ejecuciones válidas usaron permisos
  del host, carpetas nuevas y limpieza específica del fixture.
- **Linux final/TSan/ASan completo:** no ejecutados localmente; Docker sin daemon y WSL
  devolvió acceso denegado. Los resultados históricos corresponden a la base, no al fix.
- **Paquetes extraídos de la revisión nueva:** no ejecutados. La instalación desde fuente
  x86 oficial pasó; no se equipara a una nueva calificación de distribución.
- **12–24 horas, particiones largas, agotamiento sistemático de FDs/allocator, presión de
  memoria, storage físico/red congelado, AMX loader fuzz y playerid real reutilizado:**
  no calificados por este trabajo. Los tests de límites y SDK doubles son más acotados.

## Automatización y criterios pendientes

[Runbook](stability-runbook.md), [matriz por escenario](stability-matrix.csv) y
[workflow manual](../.github/workflows/audit-long.yml). El CI rápido y de release existente
se conserva. El workflow prolongado necesita un runner Linux desechable ya aprovisionado
con etiqueta `pg-audit-isolated`; no fue despachado ni se activaron recursos de pago.
El supervisor fue ejecutado con 120 s y rechaza builds configurados con una duración menor.

Para considerar READY se requiere, al menos: CI exacto del cambio integrado propuesto,
Linux x86 y sanitizadores de la revisión corregida, fresh package/server qualification en
ambos sistemas, binding de los artefactos nuevos al código evaluado y evaluación de los
límites de soporte que quedan. La campaña prolongada está preparada pero sin ejecutar.
La PR debe permanecer en borrador; un CI verde posterior tampoco autoriza merge o release.

Los archivos de evidencia son texto/CSV/JSON/XML, con rutas de workspace redactadas y
[ledger de hashes](stability-evidence/ledger.json). No contienen clústeres, dumps, perfiles
de conexión, claves TLS, tokens, binarios ni paquetes. `/audit/` ya está excluido de los
source archives e instalación pública por las reglas preexistentes del proyecto.
