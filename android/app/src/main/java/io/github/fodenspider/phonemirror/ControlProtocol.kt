package io.github.fodenspider.phonemirror

import java.io.DataInputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

object ControlProtocol {
    data class Gesture(val action: Int, val x0: Int, val y0: Int, val x1: Int, val y1: Int, val durationMs: Int)
    /** Actions 1/2: completed tap/swipe; 3/4/5: touch down/update/up. */
    fun read(input: DataInputStream, sequence: Long): Gesture? {
        val first = input.read()
        if (first == -1 || first == 4) return null
        val bytes = ByteArray(Protocol.HEADER_SIZE); bytes[0] = first.toByte()
        input.readFully(bytes, 1, bytes.size - 1)
        val h = Protocol.decode(bytes)
        require(h.type == 30 && h.payloadLength == 28 && h.streamId == 2 && h.streamEpoch == 1 &&
            h.sequence == sequence && h.timestampUs == 0L) { "Invalid control header" }
        val body = ByteArray(28); input.readFully(body)
        val b = ByteBuffer.wrap(body).order(ByteOrder.BIG_ENDIAN)
        val result = Gesture(b.int, b.int, b.int, b.int, b.int, b.int)
        require(b.int == 1 && result.action in 1..5 && result.durationMs in 50..500)
        require(listOf(result.x0, result.y0, result.x1, result.y1).all { it in 0..65535 })
        if (result.action != 2) require(result.x0 == result.x1 && result.y0 == result.y1)
        return result
    }
}
