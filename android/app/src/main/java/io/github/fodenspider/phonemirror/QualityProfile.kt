package io.github.fodenspider.phonemirror

data class QualityProfile(val longEdge: Int, val fps: Int, val bitrate: Int, val label: String) {
    fun dimensions(width: Int, height: Int): Pair<Int, Int> {
        require(width > 0 && height > 0)
        val scale = minOf(1.0, longEdge.toDouble() / maxOf(width, height))
        return maxOf(2, (width * scale).toInt() / 2 * 2) to maxOf(2, (height * scale).toInt() / 2 * 2)
    }
    companion object {
        fun fromId(id: Int) = when (id) {
            0 -> QualityProfile(960, 24, 2_000_000, "省资源")
            2 -> QualityProfile(1920, 30, 8_000_000, "清晰")
            else -> QualityProfile(1280, 30, 4_000_000, "均衡")
        }
    }
}
