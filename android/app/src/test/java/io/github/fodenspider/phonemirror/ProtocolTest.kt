package io.github.fodenspider.phonemirror

import org.junit.Assert.*
import org.junit.Test

class ProtocolTest {
    @Test fun sharedGoldenVector() {
        val hex = javaClass.classLoader!!.getResource("ping.hex")!!.readText().trim()
        val bytes = hex.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
        val h = Protocol.decode(bytes)
        assertEquals(Protocol.PING, h.type)
        assertEquals(1L, h.sequence)
        assertEquals(1_000_000L, h.timestampUs)
        assertArrayEquals(bytes, Protocol.encode(h))
    }
    @Test fun rejectHostileLengthsBeforeAllocation() {
        val h = Protocol.Header(31, payloadLength = 16385)
        assertThrows(IllegalArgumentException::class.java) { Protocol.encode(h) }
        val wire = Protocol.encode(Protocol.Header(Protocol.PING))
        wire[12] = 0xff.toByte()
        assertThrows(IllegalArgumentException::class.java) { Protocol.decode(wire) }
    }
    @Test fun rejectUnknownVersionAndType() {
        val wire = Protocol.encode(Protocol.Header(Protocol.PING))
        wire[5] = 2
        assertThrows(IllegalArgumentException::class.java) { Protocol.decode(wire) }
        assertThrows(IllegalArgumentException::class.java) { Protocol.encode(Protocol.Header(999)) }
    }
}
