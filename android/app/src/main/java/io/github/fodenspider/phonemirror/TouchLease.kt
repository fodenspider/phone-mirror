package io.github.fodenspider.phonemirror

/** Latest target only; no movement backlog. All methods run on the service thread. */
class TouchLease {
    var active = false; private set
    var ending = false; private set
    var cancelled = false; private set
    var x = 0; private set
    var y = 0; private set
    private var seen = 0L
    fun accept(action: Int, px: Int, py: Int, now: Long): Boolean {
        if (action == 3) {
            if (active) return false
            active = true; ending = false; cancelled = false
        } else if (!active || ending) return false
        x = px; y = py; seen = now
        if (action == 5) ending = true
        return true
    }
    fun mustEnd(now: Long, authorized: Boolean): Boolean {
        if (active && (!authorized || now - seen >= 500)) release()
        return ending
    }
    fun release() { if (active) { ending = true; cancelled = true } }
    fun clear() { active = false; ending = false }
}
