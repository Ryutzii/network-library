# Documentación de valoración de requisitos

Este documento explica cómo se ha implementado cada sección del apartado de valoración en la librería. La arquitectura separa las responsabilidades de red, ejecución de manejadores nativos y ejecución cooperativa de manejadores Lua, pero centraliza el trabajo paralelo en un único `ThreadPool` para reducir competencia por CPU.

## 1. Hilo para aceptar y cerrar conexiones HTTP

**Requisito:** se debe implementar un hilo para aceptar y cerrar las conexiones HTTP.

**Implementación:**

- `HttpServer::run()` arranca `accept_thread`, que ejecuta `HttpServer::accept_thread_run()`.
- `accept_thread_run()` tiene dos responsabilidades:
  1. aceptar conexiones nuevas con `listener.accept()`;
  2. cerrar conexiones pendientes llamando periódicamente a `close_pending_connections()`.
- Los demás hilos no cierran físicamente sockets. Cuando detectan que una conexión debe cerrarse, llaman a `request_connection_close(handle)`, que inserta el handle en `close_queue`.
- `close_pending_connections()` consume `close_queue`, elimina la corrutina Lua asociada si existe, hace `shutdown_send()`, `shutdown_receive()` y finalmente `socket.close()`.
- El estado `CLOSING` evita que el worker de I/O siga leyendo o escribiendo sobre una conexión cuyo cierre ya se solicitó.

**Funciones y miembros principales:**

- `accept_thread`
- `close_queue`, `close_queue_mutex`, `close_queue_cv`
- `HttpServer::accept_thread_run()`
- `HttpServer::request_connection_close()`
- `HttpServer::close_pending_connections()`
- `ConnectionContext::CLOSING`

## 2. Hilo para leer y escribir en sockets

**Requisito:** se debe implementar un hilo para leer y escribir en sockets.

**Implementación:**

- `HttpServer::run()` arranca `worker_thread`, que ejecuta `HttpServer::transfer_data()`.
- `transfer_data()` consume sockets aceptados desde `accepted_queue` y los registra en `connections`.
- En cada iteración, `transfer_data()` procesa las conexiones según su estado:
  - `RECEIVING_REQUEST`: llama a `receive_request()` para leer datos del socket y alimentar el parser HTTP.
  - `WRITING_RESPONSE_HEADER`: llama a `write_response_header()` para enviar la cabecera HTTP.
  - `WRITING_RESPONSE_BODY`: llama a `write_response_body()` para enviar el cuerpo.
  - `RUNNING_HANDLER`: no hace I/O porque la conexión está esperando al manejador.
  - `CLOSING`: no hace I/O porque el cierre físico ya está delegado al hilo de aceptación/cierre.
- Si se detecta fin de conexión, error o timeout, el worker marca la conexión como cerrada o en cierre, y solicita el cierre físico mediante `request_connection_close()`.

**Funciones y miembros principales:**

- `worker_thread`
- `accepted_queue`, `accepted_queue_mutex`, `accepted_cv`
- `HttpServer::transfer_data()`
- `HttpServer::receive_request()`
- `HttpServer::write_response_header()`
- `HttpServer::write_response_body()`
- `HttpServer::close_inactive_connections()`

## 3. Hilo para ejecutar la máquina virtual de Lua

**Requisito:** se debe implementar un hilo para ejecutar la máquina virtual de Lua.

**Implementación:**

- `HttpServer::run()` arranca `lua_thread`, que ejecuta `HttpServer::lua_thread_run()`.
- `lua_thread_run()` crea un `LuaRequestHandler::StateScope` al inicio de la función. Ese objeto inicializa el estado Lua compartido y lo mantiene vivo mientras existe el hilo Lua.
- El estado único de Lua no se gestiona en `HttpServer`, sino en `LuaRequestHandler`, cumpliendo la restricción de que la máquina virtual de Lua esté centralizada en la clase de manejadores Lua.
- `LuaRequestHandler::StateScope` llama a `initialize_lua_state()` en su constructor y a `destroy_lua_state()` en su destructor.
- `LuaRequestHandler` mantiene un único `lua_State*` compartido (`shared_lua_state`) y lo expone mediante `get_lua_state()` para que las clases hijas creen corrutinas reales de Lua.
- Si el runtime/cabeceras de Lua no están disponibles al compilar, `LuaRequestHandler::is_lua_available()` devuelve `false` y el servidor responde con HTTP 500 para las rutas Lua.

**Funciones y clases principales:**

- `lua_thread`
- `HttpServer::lua_thread_run()`
- `LuaRequestHandler`
- `LuaRequestHandler::StateScope`
- `LuaRequestHandler::initialize_lua_state()`
- `LuaRequestHandler::destroy_lua_state()`
- `LuaRequestHandler::get_lua_state()`
- `LuaRequestHandler::is_lua_available()`

## 4. Tareas asíncronas en Lua usando corrutinas

**Requisito:** se deben implementar tareas asíncronas en Lua usando corrutinas.

**Implementación:**

- Los manejadores Lua deben heredar de `LuaRequestHandler`.
- `LuaRequestHandler::requires_lua()` devuelve `true`, por lo que `HttpServer::handler_thread_run()` detecta que el manejador debe ejecutarse en el hilo Lua.
- Para cada conexión Lua, `HttpServer` pide al handler una corrutina mediante `create_lua_coroutine()`.
- En `LuaRequestHandler`, `create_lua_coroutine()` llama a la sobrecarga `create_lua_coroutine(lua_State*)`, pasando el único estado Lua compartido.
- Las clases hijas de `LuaRequestHandler` pueden crear corrutinas reales con la API de Lua, por ejemplo `lua_newthread()` y `lua_resume()`.
- El servidor almacena la función de reanudación en `lua_coroutines`, indexada por el handle del socket.
- Si la corrutina devuelve `false`, se interpreta como suspensión/yield; el servidor llama a `schedule_lua_resume(handle)` para reprogramarla.
- Si la corrutina devuelve `true`, se interpreta como finalización; el servidor elimina la entrada de `lua_coroutines` y pasa la conexión a escritura de respuesta.
- La ejecución es cooperativa: cuando una corrutina se suspende, el hilo Lua puede continuar con la siguiente corrutina pendiente en `lua_task_queue`.

**Funciones y miembros principales:**

- `LuaRequestHandler::create_lua_coroutine()`
- `LuaRequestHandler::create_lua_coroutine(lua_State*)`
- `HttpServer::lua_thread_run()`
- `lua_task_queue`, `lua_task_queue_mutex`, `lua_task_queue_cv`
- `lua_coroutines`, `lua_coroutines_mutex`
- `HttpServer::schedule_lua_resume()`

## 5. Thread pool para ejecutar manejadores HTTP nativos

**Requisito:** se debe implementar un thread pool que ejecute los manejadores HTTP nativos.

**Implementación:**

- `HttpServer` contiene un único `ThreadPool thread_pool`.
- `handler_thread_run()` crea el manejador HTTP con `RequestHandlerManager`.
- Si `handler->requires_lua()` devuelve `false`, el manejador se considera nativo.
- Para los manejadores nativos, el servidor mueve la petición a un `request_snapshot`, crea una tarea (`work_fn`) y la envía al pool con `thread_pool.submit(std::move(work_fn))`.
- Cuando la tarea termina, actualiza la respuesta y el estado de la conexión bajo los mutex correspondientes.
- Si el manejador devuelve `false`, la request se restaura en el contexto y se reencola el handle para intentarlo más adelante.

**Funciones y miembros principales:**

- `ThreadPool`
- `ThreadPool::submit()`
- `HttpServer::thread_pool`
- `HttpServer::handler_thread_run()`
- `HttpRequestHandler::process()`

## 6. Thread pool para tareas asíncronas de Lua

**Requisito:** se debe implementar un thread pool que ejecute las tareas asíncronas de Lua.

**Implementación:**

- Se usa el mismo `ThreadPool thread_pool` para evitar tener más de un pool compitiendo por CPU.
- Las tareas asíncronas de Lua no ejecutan directamente la VM desde el pool, porque el requisito de Lua exige un único hilo propietario de la VM.
- El pool ejecuta la tarea auxiliar de reprogramar la reanudación: `schedule_lua_resume()` envía una tarea a `thread_pool` que inserta el handle en `lua_task_queue` y despierta `lua_thread`.
- `lua_thread` es quien ejecuta/reanuda la corrutina de Lua de forma segura y serializada.
- `ThreadPool::submit_exclusive()` permite ejecutar tareas marcadas como exclusivas si en el futuro una tarea necesita no solaparse con otras tareas exclusivas.

**Funciones y miembros principales:**

- `ThreadPool::submit()`
- `ThreadPool::submit_exclusive()`
- `HttpServer::thread_pool`
- `HttpServer::schedule_lua_resume()`
- `HttpServer::lua_thread_run()`

## 7. Papel de `HttpServer::run()` en la arquitectura

**Requisito de diseño:** en `HttpServer::run()` se aceptan y cierran conexiones, se lee y escribe en sockets, y se ejecutan todos los handlers llamando a funciones encargadas de ello.

**Implementación:**

`HttpServer::run()` no ejecuta en línea todo el bucle del servidor, sino que arranca los hilos especializados y delega cada responsabilidad en la función correspondiente:

- `accept_thread` → `accept_thread_run()` para aceptar y cerrar conexiones.
- `worker_thread` → `transfer_data()` para leer/escribir sockets.
- `handler_thread` → `handler_thread_run()` para crear handlers y despachar nativos o Lua.
- `lua_thread` → `lua_thread_run()` para ejecutar cooperativamente manejadores Lua.

Esta organización mantiene `run()` como punto central de orquestación y permite que las funciones específicas tengan responsabilidades claras.
