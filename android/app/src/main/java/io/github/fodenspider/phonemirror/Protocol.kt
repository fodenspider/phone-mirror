package io.github.fodenspider.phonemirror

import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Wire framing only; no networking or authentication is enabled by the probe. */
object Protocol {
    const val HEADER_SIZE = 40
    const val MAGIC = 0x504d4952
    const val VIDEO_CONFIG = 10
    const val VIDEO_ACCESS_UNIT = 11
    const val PING = 50
    data class Header(
        val type: Int, val flags: Int = 0, val payloadLength: Int = 0,
        val streamId: Int = 0, val streamEpoch: Int = 0,
        val sequence: Long = 0, val timestampUs: Long = 0
    )
    fun limit(type: Int): Int = when (type) {
        VIDEO_ACCESS_UNIT -> 8 * 1024 * 1024
        31 -> 16 * 1024
        1, 2, 3, 4, VIDEO_CONFIG, 20, 21, 30, 32, 40, 41, PING, 51, 52 -> 64 * 1024
        else -> throw IllegalArgumentException("Unknown message type")
    }
    private fun validate(h: Header) {
        require(h.payloadLength in 0..limit(h.type)) { "Payload exceeds type limit" }
        val allowed = if (h.type == VIDEO_ACCESS_UNIT) 1 else 0
        require(h.flags and allowed.inv() == 0) { "Unsupported flags" }
    }
    fun encode(h: Header): ByteArray {
        validate(h)
        return ByteBuffer.allocate(HEADER_SIZE).order(ByteOrder.BIG_ENDIAN).apply {
            putInt(MAGIC); putShort(1); putShort(h.type.toShort())
            putInt(h.flags); putInt(h.payloadLength); putInt(h.streamId); putInt(h.streamEpoch)
            putLong(h.sequence); putLong(h.timestampUs)
        }.array()
    }
    fun decode(bytes: ByteArray): Header {
        require(bytes.size == HEADER_SIZE)
        val b = ByteBuffer.wrap(bytes).order(ByteOrder.BIG_ENDIAN)
        require(b.int == MAGIC); require(b.short.toInt() == 1)
        return Header(b.short.toInt() and 0xffff, b.int, b.int, b.int, b.int, b.long, b.long)
            .also(::validate)
    }
}
