package io.github.fodenspider.phonemirror

import java.io.DataInputStream
import java.net.Socket
import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Test

class UsbStreamerTest {
    @Test fun authenticatesOnceStreamsAndStops() {
        val directory = Files.createTempDirectory("usb-test").toFile()
        val file = directory.resolve("usb-session.json")
        val connected = CountDownLatch(1)
        val ended = CountDownLatch(1)
        val server = UsbStreamer(file, { connected.countDown() }, { ended.countDown() })
        try {
            val json = file.readText()
            val port = Regex("\"port\":(\\d+)").find(json)!!.groupValues[1].toInt()
            val token = Regex("\"token\":\"([0-9a-f]+)\"").find(json)!!.groupValues[1]
            val secret = token.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
            server.start()
            Socket("127.0.0.1", port).use { wrong ->
                wrong.soTimeout = 2000
                wrong.getOutputStream().write("PMUSB001".toByteArray() + ByteArray(16))
                assertEquals(-1, wrong.getInputStream().read())
                assertEquals(1L, connected.count)
            }
            Socket("127.0.0.1", port).use { socket ->
                socket.soTimeout = 2000
                socket.getOutputStream().write("PMUSB001".toByteArray() + secret)
                val input = DataInputStream(socket.getInputStream())
                val ack = ByteArray(8); input.readFully(ack)
                assertArrayEquals("PMOK0001".toByteArray(), ack)
                assertTrue(connected.await(2, TimeUnit.SECONDS))
                assertFalse(file.exists())
                server.offer(byteArrayOf(1, 2), byteArrayOf(3))
                val packet = ByteArray(3); input.readFully(packet)
                assertArrayEquals(byteArrayOf(1, 2, 3), packet)
                server.finish(byteArrayOf(4))
                assertEquals(4, input.read())
                assertEquals(-1, input.read())
                assertEquals(1L, ended.count)
            }
        } finally { server.close(); directory.deleteRecursively() }
    }
    @Test fun oldSessionCannotDeleteNewDescriptor() {
        val directory = Files.createTempDirectory("usb-ownership").toFile()
        val file = directory.resolve("usb-session.json")
        val old = UsbStreamer(file, {}, {})
        val next = UsbStreamer(file, {}, {})
        try {
            val expected = file.readText()
            old.close()
            assertEquals(expected, file.readText())
            next.close()
            assertFalse(file.exists())
        } finally { old.close(); next.close(); directory.deleteRecursively() }
    }
}
