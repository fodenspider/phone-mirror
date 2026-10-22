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
