package io.github.fodenspider.phonemirror
import org.junit.Assert.*
import org.junit.Test

class TouchLeaseTest {
    @Test fun followsLatestAndEndsWithoutReplaying() {
        val state = TouchLease()
        assertFalse(state.accept(4,1,2,0))
        assertTrue(state.accept(3,1,2,0))
        assertFalse(state.accept(3,9,9,1))
        assertTrue(state.accept(4,30,40,100))
        assertTrue(state.accept(4,50,60,150))
        assertEquals(50,state.x); assertEquals(60,state.y)
        assertFalse(state.mustEnd(649,true))
        assertTrue(state.accept(5,70,80,649))
        assertTrue(state.mustEnd(650,true)); assertFalse(state.cancelled)
        assertFalse(state.accept(4,90,90,651))
        state.clear(); assertTrue(state.accept(3,1,2,700))
    }
    @Test fun expiresAndRevokes() {
        for (revoked in listOf(false,true)) {
            val state = TouchLease(); state.accept(3,1,2,1000)
            assertTrue(state.mustEnd(if(revoked) 1001 else 1500,!revoked))
            assertTrue(state.cancelled)
            assertFalse(state.accept(4,3,4,1501))
        }
        val state=TouchLease(); state.accept(3,1,2,0); state.release()
        assertTrue(state.ending); assertTrue(state.cancelled)
    }
}
