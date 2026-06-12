Implementás un servidor HTTP/1.1 funcional sobre POSIX sockets. Features mínimas que lo hacen impresionante:

Parsing de request/response headers a mano
Manejo de múltiples conexiones concurrentes (threadpool o epoll)
Soporte de keep-alive y chunked transfer
Servir archivos estáticos + routing básico

El diferenciador frente a otros: si le agregás TLS con OpenSSL o un benchmark comparándolo con nginx en conexiones simples, ya es un proyecto que te hace quedar en la memoria.
