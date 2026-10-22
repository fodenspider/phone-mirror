package io.github.fodenspider.phonemirror

import java.util.ArrayDeque

/** Bounded encoded packets: overflow ends the stream, never drops dependencies. */
class PacketBudget(
    private val maxBytes: Int = 4 * 1024 * 1024,
    private val maxPackets: Int = 12,
    private val maxAgeNs: Long = 100_000_000,
    private val clock: () -> Long = System::nanoTime
) {
    data class Packet(val data: ByteArray, val terminal: Boolean, val created: Long)
    private val queue = ArrayDeque<Packet>()
    private val lock = Object()
    private var bytes = 0
    fun offer(data: ByteArray, terminal: Boolean = false): Boolean = synchronized(lock) {
        val now = clock()
        if (data.size > maxBytes - bytes || queue.peek()?.let { now - it.created > maxAgeNs } == true) return false
        if (queue.size >= maxPackets) return false
        queue.addLast(Packet(data, terminal, now))
        bytes += data.size
        lock.notifyAll()
        return true
    }
    fun poll(timeoutMs: Long): Packet? = synchronized(lock) {
        val deadline = System.nanoTime() + timeoutMs * 1_000_000
        while (queue.isEmpty()) {
            val remaining = deadline - System.nanoTime()
            if (remaining <= 0) return null
            lock.wait(remaining / 1_000_000, (remaining % 1_000_000).toInt())
        }
        val packet = queue.removeFirst()
        bytes -= packet.data.size
        check(clock() - packet.created <= maxAgeNs) { "USB 发送队列积压，请重连或降低画质" }
        return packet
    }
    fun clear() = synchronized(lock) { queue.clear(); bytes = 0; lock.notifyAll() }
}
