package io.github.fodenspider.phonemirror

import java.io.File
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.security.MessageDigest
import java.security.SecureRandom
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/** Single-use USB session. Only loopback; no LAN listener and no control injection. */
class UsbStreamer(
    private val sessionFile: File,
    private val onConnected: () -> Unit,
    private val onEnded: (String) -> Unit
) : AutoCloseable {
    private val secret = ByteArray(16).also { SecureRandom().nextBytes(it) }
    private val server = ServerSocket(0, 1, InetAddress.getByName("127.0.0.1"))
    private val closed = AtomicBoolean(false)
    private val finishing = AtomicBoolean(false)
    private val failed = AtomicBoolean(false)
    private val budget = PacketBudget()
    private val watchdog = Executors.newSingleThreadScheduledExecutor()
    @Volatile private var socket: Socket? = null
    @Volatile private var writingSince = 0L
    private val born = System.nanoTime()
    @Volatile private var authenticated = false
    private val sessionText: String

    init {
        try {
            val token = secret.joinToString("") { "%02x".format(it.toInt() and 255) }
            sessionText = "{\"version\":1,\"port\":${server.localPort},\"token\":\"$token\"}"
            val temporary = File(sessionFile.parentFile, "usb-session.tmp")
            synchronized(UsbStreamer::class.java) {
                temporary.writeText(sessionText)
                check(temporary.renameTo(sessionFile)) { "无法保存 USB 会话" }
            }
        } catch (e: Exception) { server.close(); watchdog.shutdownNow(); throw e }
    }

    fun start() {
        watchdog.scheduleAtFixedRate({
            if (!closed.get()) {
                val now = System.nanoTime()
                if (!authenticated && now - born > 120_000_000_000L) fail("等待电脑连接超时")
                if (writingSince != 0L && now - writingSince > 2_000_000_000L) fail("USB 发送超时，请重新连接")
            }
        }, 250, 250, TimeUnit.MILLISECONDS)
        Thread({ runSession() }, "usb-video-send").start()
    }

    fun offer(header: ByteArray, payload: ByteArray) {
        check(!closed.get() && !finishing.get()) { "USB 连接已结束" }
        check(budget.offer(header + payload)) { "USB 队列超限，请重新连接" }
    }

    fun finish(stopHeader: ByteArray) {
        if (!finishing.compareAndSet(false, true) || closed.get()) return
        removeSessionFile()
        if (!authenticated || !budget.offer(stopHeader, true)) { close(); return }
        runCatching { watchdog.schedule({ close() }, 500, TimeUnit.MILLISECONDS) }
    }

    private fun runSession() {
        try {
            var accepted: Socket? = null
            repeat(5) {
                if (accepted == null && !closed.get()) {
                    val candidate = server.accept()
                    socket = candidate
                    candidate.soTimeout = 3000
                    val valid = runCatching {
                        val hello = ByteArray(24)
                        java.io.DataInputStream(candidate.getInputStream()).readFully(hello)
                        hello.copyOfRange(0, 8).contentEquals("PMUSB001".toByteArray(Charsets.US_ASCII)) &&
                            MessageDigest.isEqual(hello.copyOfRange(8, 24), secret)
                    }.getOrDefault(false)
                    if (valid) accepted = candidate else { candidate.close(); socket = null }
                }
            }
            val client = accepted ?: error("USB 会话认证失败")
            server.close(); removeSessionFile(); secret.fill(0)
            client.soTimeout = 0; client.tcpNoDelay = true; client.sendBufferSize = 64 * 1024
            client.getOutputStream().write("PMOK0001".toByteArray(Charsets.US_ASCII))
            authenticated = true
            Thread({
                try {
                    val command = client.getInputStream().read()
                    if (!finishing.get() && !closed.get()) fail(if (command == 4 || command == -1) "电脑已断开" else "不支持的 USB 控制消息")
                } catch (_: Exception) { if (!finishing.get()) fail("USB 连接中断") }
            }, "usb-peer-monitor").start()
            onConnected()
            while (!closed.get()) {
                val packet = budget.poll(200) ?: continue
                writingSince = System.nanoTime()
                client.getOutputStream().write(packet.data)
                writingSince = 0
                if (packet.terminal) { close(); return }
            }
        } catch (e: Exception) {
            if (!finishing.get() && !closed.get()) fail("USB 传输结束：${e.javaClass.simpleName}")
        }
    }

    private fun fail(reason: String) {
        if (!closed.get() && failed.compareAndSet(false, true)) {
            close(); onEnded(reason)
        }
    }
    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        runCatching { server.close() }; runCatching { socket?.close() }
        removeSessionFile(); secret.fill(0); budget.clear(); watchdog.shutdownNow()
    }
    private fun removeSessionFile() = synchronized(UsbStreamer::class.java) {
        runCatching { if (sessionFile.exists() && sessionFile.readText() == sessionText) sessionFile.delete() }
        Unit
    }
}
