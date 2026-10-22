package io.github.fodenspider.phonemirror

import org.junit.Assert.*
import org.junit.Test

class QualityProfileTest {
    @Test fun boundedEvenDimensionsAndDefaults() {
        for (id in 0..2) {
            val p = QualityProfile.fromId(id)
            for ((w, h) in listOf(1260 to 2800, 2800 to 1260, 640 to 480)) {
                val (x, y) = p.dimensions(w, h)
                assertTrue(x > 0 && y > 0 && x % 2 == 0 && y % 2 == 0)
                assertTrue(maxOf(x, y) <= p.longEdge)
                assertTrue(x <= w && y <= h)
                assertTrue(kotlin.math.abs(x.toDouble() / y - w.toDouble() / h) < 0.01)
            }
        }
        assertEquals(QualityProfile.fromId(1), QualityProfile.fromId(-100))
        assertEquals(1280, QualityProfile.fromId(1).longEdge)
        assertEquals(4_000_000, QualityProfile.fromId(1).bitrate)
    }
    @Test(expected = IllegalArgumentException::class) fun rejectEmptyDisplay() {
        QualityProfile.fromId(1).dimensions(0, 100)
    }
}
