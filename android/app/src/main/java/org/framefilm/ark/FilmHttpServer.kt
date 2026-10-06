package org.framefilm.ark

import android.content.Context
import java.io.Closeable
import java.net.Inet4Address
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketException
import java.util.concurrent.Executors

/** Serves one packaged film through the phone's Wi-Fi Direct GO address. */
class FilmHttpServer(context: Context, address: Inet4Address) : Closeable {
    private val bytes = context.assets.open("ark_direct_test.film").use { it.readBytes() }
    private val filename = "ark_direct_${System.currentTimeMillis()}.film"
    private val server = ServerSocket().apply {
        reuseAddress = true
        bind(InetSocketAddress(address, 0))
        soTimeout = 1000
    }
    private val worker = Executors.newSingleThreadExecutor()
    @Volatile private var closed = false
    @Volatile private var client: Socket? = null

    val size: Int get() = bytes.size
    val url: String = "http://${address.hostAddress}:${server.localPort}/$filename"

    fun start() {
        worker.execute {
            while (!closed) {
                try {
                    server.accept().also {
                        client = it
                        if (closed) { it.close(); throw SocketException("server closed") }
                    }.use { socket ->
                        socket.soTimeout = 5_000
                        val input = socket.getInputStream().bufferedReader(Charsets.US_ASCII)
                        val first = input.readLine() ?: return@use
                        var line: String?
                        do { line = input.readLine() } while (line != null && line.isNotEmpty())
                        val output = socket.getOutputStream()
                        if (first == "GET /$filename HTTP/1.1" || first == "GET /$filename HTTP/1.0") {
                            val header = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: ${bytes.size}\r\nConnection: close\r\n\r\n"
                            output.write(header.toByteArray(Charsets.US_ASCII))
                            output.write(bytes)
                        } else output.write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n".toByteArray(Charsets.US_ASCII))
                        output.flush()
                    }
                    client = null
                } catch (_: java.net.SocketTimeoutException) { }
                catch (_: SocketException) { if (!closed) break }
                catch (_: Exception) { if (!closed) break }
                finally { client = null }
            }
        }
    }

    override fun close() {
        closed = true
        server.close()
        try { client?.close() } catch (_: Exception) { }
        worker.shutdownNow()
    }
}
