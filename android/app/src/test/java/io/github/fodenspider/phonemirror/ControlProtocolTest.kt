package io.github.fodenspider.phonemirror

import java.io.ByteArrayInputStream
import java.io.DataInputStream
import org.junit.Assert.*
import org.junit.Test

class ControlProtocolTest {
    private fun fixture(): ByteArray = javaClass.classLoader!!.getResourceAsStream("gesture.hex")!!
        .bufferedReader().use { it.readText().trim() }.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    private fun read(bytes: ByteArray, sequence: Long = 7) = ControlProtocol.read(DataInputStream(ByteArrayInputStream(bytes)), sequence)
    @Test fun sharedFixtureAndStop() {
        assertEquals(ControlProtocol.Gesture(2,1000,2000,3000,4000,180), read(fixture()))
        assertNull(read(byteArrayOf(4)))
        assertNull(read(byteArrayOf()))
    }
    @Test fun continuousActionsUseSinglePosition() {
        for(action in 3..5) {
            val bytes=fixture()
            val b=java.nio.ByteBuffer.wrap(bytes).order(java.nio.ByteOrder.BIG_ENDIAN)
            b.putInt(40,action); b.putInt(52,1000); b.putInt(56,2000)
            assertEquals(action,read(bytes)!!.action)
            b.putInt(52,1001)
            assertThrows(IllegalArgumentException::class.java) { read(bytes) }
        }
    }
    @Test fun rejectsOrderingLengthsCoordinatesAndTruncation() {
        val good = fixture()
        assertThrows(IllegalArgumentException::class.java) { read(good, 0) }
        for (length in 1 until good.size) assertThrows(Exception::class.java) { read(good.copyOf(length)) }
        for (offset in listOf(12,16,20,44,48,52,56,60,64)) {
            val bad = good.copyOf(); bad[offset] = 0x7f
            assertThrows(IllegalArgumentException::class.java) { read(bad) }
        }
    }
}
