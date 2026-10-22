package io.github.fodenspider.phonemirror

import org.junit.Assert.*
import org.junit.Test

class PacketBudgetTest {
    @Test fun capsBytesAndPacketsWithoutDropping() {
        val q = PacketBudget(8, 2, 100) { 0L }
        assertTrue(q.offer(byteArrayOf(1, 2, 3)))
        assertTrue(q.offer(byteArrayOf(4, 5, 6)))
        assertFalse(q.offer(byteArrayOf(7)))
        assertArrayEquals(byteArrayOf(1, 2, 3), q.poll(0)!!.data)
        assertFalse(q.offer(ByteArray(6)))
        assertTrue(q.offer(ByteArray(5), true))
        assertArrayEquals(byteArrayOf(4, 5, 6), q.poll(0)!!.data)
        assertTrue(q.poll(0)!!.terminal)
    }
    @Test fun rejectsOldPackets() {
        var now = 0L
        val q = PacketBudget(8, 2, 100) { now }
        assertTrue(q.offer(byteArrayOf(1)))
        now = 101
        assertFalse(q.offer(byteArrayOf(2)))
        assertThrows(IllegalStateException::class.java) { q.poll(0) }
    }
}
